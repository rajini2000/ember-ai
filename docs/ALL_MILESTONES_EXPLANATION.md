# Ember AI — All Milestones Explained
How every milestone works and how they connect.

---

## The Big Picture First

All 5 milestones are about one thing: **getting the AI to run on the K64F hardware**.

The AI was trained in Python on a laptop. The milestones move it step by step
onto the embedded hardware — collecting real data, running inference in C,
handling failures, logging events, and self-calibrating.

They build on each other in this order:

```
M5 (calibrate sensors)
    ↓ feeds into
M1 (compute 16 features from sensor readings)
    ↓ same features used in
M2 (run AI inference on K64F — no internet needed)
    ↓ extended by
M3 (use cloud AI when WiFi works, fall back to M2 when it doesn't)
    ↓ generates events that are logged by
M4 (record every alarm event, replay and analyze on K64F)
```

---

## Milestone 1 — RL Training Data Logger
**Demo: Mar 12 | What: K64F writes labeled CSV to SD card**

### What problem does it solve?
The DQN model was trained on synthetic data (fake readings we generated in Python)
and Beijing real-world data. Neither of these came from the actual K64F hardware.
Real sensor hardware has its own noise, drift, and quirks that synthetic data
doesn't capture. M1 creates a tool to collect real labeled data from the hardware.

### What does the K64F actually do?
Every 2.5 seconds:
1. Reads raw values from PMS5003, BME680, and MQ sensors
2. Computes 16 features (the same 16 the neural network was trained on)
3. Writes one row to `training_data.csv` on the SD card
4. The row includes the features AND a human label (0=SAFE or 1=DANGER)

### What are the 16 features?

```
Raw sensors (11 features):
  PM1.0, PM2.5, PM10          ← from PMS5003
  temperature, humidity,
  pressure, gas_resistance    ← from BME680
  mq_analog, mq_digital       ← from MQ gas sensor
  tvoc, eco2                  ← from ENS160 (set 0, it's offline)

Derived / computed (5 features):
  aqi_category    ← 0–5 scale from PM2.5 (0=GOOD, 5=HAZARDOUS)
  delta_pm25      ← PM2.5[now] − PM2.5[last cycle]  (rate of rise)
  delta_pm10      ← PM10[now]  − PM10[last cycle]
  thi             ← temp + 0.33×humidity − 4.0       (heat index)
  gas_ratio       ← mq_analog / log10(gas_resistance) (cross-sensor)
```

**Why derived features?** Raw PM2.5 = 100 could mean stable pollution (less urgent)
or rapidly rising smoke (very urgent). `delta_pm25` tells the difference.

### The buttons
- **SW2 brief press** → label switches to DANGER (1), LED turns red
- **SW3 brief press** → label switches to SAFE (0), LED turns green
- You physically watch the sensor and decide what the air quality is
- This is called "ground truth labeling" — human tells the AI what's right

### What the CSV looks like
```
timestamp,pm1,pm2_5,pm10,temp,hum,pres,gas,mq_a,mq_d,...,label
2026-03-12 14:23:05,8,15,18,27.3,18.2,990.9,14523678,0.031,0,...,0
2026-03-12 14:23:07,450,709,812,27.5,18.1,990.5,250000,0.439,1,...,1
```

### Connection to the AI
The CSV from M1 goes into `data/real/` → `train.py` retrains the DQN model
on real hardware data → new `best_model.zip` is more accurate on this hardware.

### Files
- `k64f/ember_training_logger.h` — structs, function declarations
- `k64f/ember_training_logger.c` — feature computation, SD write, button logic

---

## Milestone 2 — On-Board RL Inference in C
**Demo: Mar 17 | What: AI runs on K64F with no internet**

### What problem does it solve?
The trained DQN model lives in `best_model.zip` — a Python/PyTorch file.
That can't run on a microcontroller. M2 extracts the decision logic from
the trained model and implements it in C so the K64F can make AI decisions
**without any network connection**.

### How does a neural network become C code?
The DQN model during training learns Q-values — numbers that represent
"how good is this action in this situation." After training, we extract
those into a lookup table (`ember_rl_policy.h`) as a C array in flash memory.

```
Python training output:
  best_model.zip (PyTorch neural network weights)
        ↓
  export_policy.py runs (we write this Python script)
        ↓
  ember_rl_policy.h (C header with Q-values as a float array)
        ↓
  Compiled into K64F flash memory
```

### What does the K64F do each cycle?
```
1. Read all sensors → same 16 features as M1
2. Discretize the state (convert float features to bucket indices)
3. Look up Q-values from the table in flash:
     Q_value[state][0] = value of doing ALARM OFF
     Q_value[state][1] = value of doing ALARM ON
4. Pick the action with the HIGHER Q-value
5. Drive GPIO PTA2:  HIGH = alarm ON,  LOW = alarm OFF
```

### Serial output
```
[LOCAL] PM2.5=709.0  Gas=250000  MQ=0.439
        State: [5][3][4]...[2]
        Q(OFF)=-12.4   Q(ON)=+47.8   → ACTION: ALARM ON
[GPIO]  PTA2 = HIGH
```

### Why is this important?
If the internet goes down, the alarm still works. The building is never
unprotected just because WiFi is offline. This is the "AI causes action
on external device" requirement — the AI directly controls hardware (PTA2).

### Connection to M1
The 16-feature computation from M1 (`ember_training_logger.c`) is reused
exactly. M2 calls the same feature function and feeds the result into the
Q-table lookup instead of into the SD card.

### Files
- `k64f/ember_rl_policy.h` — Q-table array (generated from best_model.zip)
- `k64f/ember_rl_inference.h/.c` — feature→state discretization, Q-value lookup

---

## Milestone 3 — Dual-Mode Cloud + Local Failover
**Demo: Mar 19 | What: use cloud AI normally, switch to M2 if WiFi drops**

### What problem does it solve?
The cloud API (Render.com) runs a much larger, more accurate DQN model.
The local Q-table in flash is a simplified version. Ideally we always use
the cloud. But if WiFi drops, we can't just stop monitoring. M3 gives the
best of both: cloud accuracy when available, local reliability as a backup.

### How it works each cycle
```
Step 1 — Read all sensors, compute 16 features (same as M1/M2)

Step 2 — Check WiFi:
   AT+CWJAP? command to ESP32
   ESP32 replies "+CWJAP:..." (connected) or "No AP" (disconnected)

Step 3a — CLOUD MODE (WiFi connected):
   Build JSON from sensor readings
   AT+CIPSTART="SSL","ember-ai-ews2.onrender.com",443
   AT+CIPSEND → send HTTP POST to /predict
   Parse response: { "alarm": "ON", "aqi": 500 }
   Serial: [CLOUD] alarm=ON  aqi=500  HTTP=200

Step 3b — LOCAL MODE (WiFi down or timeout >3s):
   Use M2's Q-table lookup instead
   Serial: [LOCAL] Q(OFF)=-12.4  Q(ON)=+47.8 → alarm=ON

Step 4 — Drive PTA2 GPIO based on whichever decision was made
```

### Automatic recovery
When WiFi reconnects, the system automatically goes back to cloud mode
on the next cycle. No restart needed.

### Connection to M2
M3 CONTAINS M2 — the local fallback IS the M2 inference engine.
M3 is just a wrapper that decides which mode to use.

### Connection to the Flask API
This is where the `api/server.py` (already deployed on Render.com) gets used
in embedded firmware. The K64F calls `POST /predict` and gets back the
DQN model's alarm decision.

### Files
- `k64f/ember_dual_mode.h/.c` — WiFi check, mode state machine, HTTP POST wrapper

---

## Milestone 4 — Embedded Alert History + Event Replay
**Demo: Mar 24 | What: log every alarm event to SD, analyze on K64F**

### What problem does it solve?
When an alarm fires at 3am, the building owner wants to know: what triggered it?
How long did it last? Which sensor was the main cause? M4 records every alarm
event in detail and lets you replay and analyze them directly on the K64F
via serial terminal — no laptop, no internet, no Python needed.

### What gets logged to alert_log.csv?
Every time the alarm transitions from OFF → ON, one row is written:

```
timestamp, all 16 feature values, Q(OFF), Q(ON), action, mode (CLOUD/LOCAL)
```

Example:
```
2026-03-12 02:47:11, 8,709,812, 27.5,18.1,..., -12.4,+47.8, ON, CLOUD
```

### The two serial commands

**Type "REPLAY" in serial terminal:**
```
=== ALERT HISTORY (last 10 events) ===
#1  2026-03-12 02:47:11  PM2.5=709  AQI=HAZ  Q(ON)=+47.8  Mode=CLOUD
#2  2026-03-11 18:23:05  PM2.5=412  AQI=HAZ  Q(ON)=+31.2  Mode=LOCAL
...
```

**Type "STATS" in serial terminal:**
```
=== ALERT STATISTICS ===
Total alerts:         7
Avg alert duration:   4.2 minutes
Peak time of day:     02:00–04:00 (night)
Primary trigger:      PM2.5 (highest avg during alerts: 687 µg/m³)
```

### Why this matters
Post-incident analysis without any external tools. The K64F reads the SD card,
does the math (averages, counts, max-finding), and prints results over serial.
This is entirely on-board — zero network dependency.

### Connection to M2 and M3
M4 only runs when M2 or M3 triggers an alarm. It receives the Q-values and
the mode (CLOUD/LOCAL) from M3 and logs them. Without M2/M3, there are no
alarms to log.

### Files
- `k64f/ember_alert_log.h/.c` — SD card event logging + REPLAY/STATS command parser

---

## Milestone 5 — Embedded Sensor Calibration
**Demo: Mar 26 | What: learn this room's baseline, improve AI accuracy**

### What problem does it solve?
The DQN model was trained with normalized features (values scaled to 0–1).
The normalization was based on synthetic data ranges. Real hardware in a real
room may have slightly different baselines — a room at higher altitude has
lower pressure, a hot room has higher temperature baseline, etc.

If the raw values fed into the AI don't match what it was trained on,
accuracy drops. M5 fixes this: the K64F learns the baseline for THIS
specific room and uses it to normalize all sensor readings before the AI sees them.

### How calibration works

```
Hold SW2 for 3 seconds → enter calibration mode → LED starts blinking

Over 60 seconds (24 cycles × 2.5s):
  - Collect readings from all sensors
  - Keep running min, max, sum, sum-of-squares for each feature

After 60 seconds:
  - Compute: mean = sum/24
  - Compute: std  = sqrt(sum_sq/24 - mean²)
  - Write to calibration.csv on SD card
  - LED turns solid green → done
```

### calibration.csv format
```
feature,min,max,mean,std
pm2_5,3.2,28.4,12.1,6.3
temperature,24.1,26.8,25.4,0.7
gas_resistance,8234521,19823456,14234789,2341231
...
```

### How calibration is used on boot
On every boot after calibration:
1. Read `calibration.csv` from SD
2. For each raw sensor reading: `normalized = (raw - mean) / std`
3. Feed normalized values into the 16-feature computation (M1) and Q-table (M2)

### Connection to all other milestones
M5 is the first thing that runs on boot (before M1, M2, M3, M4).
It improves accuracy for ALL of them because the AI input features are
better scaled to match what the model was trained on.

### Files
- `k64f/ember_calibration.h/.c` — SW2 long-press detection, 60s collection, SD write/read

---

## How All 5 Connect — The Full Chain

```
BOOT
 │
 ▼
[M5] Read calibration.csv from SD
     Apply normalization parameters to all sensor readings
 │
 ▼
Every 2.5 seconds:
 │
 ├──[M1] Compute 16 RL features from sensors (normalized by M5)
 │       Check SW2/SW3 for label → write row to training_data.csv
 │
 ├──[M2/M3] Make alarm decision:
 │           WiFi OK?  → cloud API (POST /predict to Render.com)
 │           WiFi down? → local Q-table lookup (M2)
 │           → drive PTA2 GPIO HIGH or LOW
 │
 └──[M4] If alarm just turned ON:
          Write event to alert_log.csv (features, Q-values, mode)
          Listen for "REPLAY" or "STATS" on serial
```

---

## Connection to the Python AI (big picture)

```
Python side (laptop):
  train.py trains DQN on synthetic + Beijing data
        ↓
  best_model.zip
        ↓
  export_policy.py extracts Q-table
        ↓
  ember_rl_policy.h  → flashed into K64F (M2, M3)

K64F side (hardware):
  M1 collects labeled CSV
        ↓
  training_data.csv sent to laptop
        ↓
  train.py retrains on real data → better best_model.zip
        ↓
  better ember_rl_policy.h → reflash K64F
```

This is a feedback loop: the hardware collects better data (M1),
which improves the AI model, which improves the on-board inference (M2/M3).

---

## What Each Milestone Proves to the Professor

| Milestone | What it proves |
|---|---|
| M1 | You understand the AI feature engineering — you can compute the same features in C that the Python training used |
| M2 | You can deploy a trained AI model onto embedded hardware — this is the core "AI on microcontroller" deliverable |
| M3 | The system is robust — alarm never stops working even if internet fails |
| M4 | The system is auditable — every alarm event is recorded and analyzable on-device |
| M5 | The system self-adapts to its environment — input normalization matches the real hardware |
