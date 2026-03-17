# SEP600 – Bonus Individual Deliverable Plan
**Name:** Rajini Paranagamage
**Course:** SEP600 – Embedded Systems
**Professor:** Jacky Lau
**Submitted:** March 10, 2026

---

## Project Overview

The Ember Air Quality Monitoring System uses an NXP FRDM-K64F microcontroller
with multiple sensors (PMS5003, BME680, MQ gas sensor) to monitor indoor air
quality. A trained Reinforcement Learning (DQN) agent makes adaptive alarm
decisions. Rajini's five bonus milestones cover: RL training data logging on
K64F, on-board RL inference in C, dual-mode cloud/local failover, embedded alert
history with event replay, and embedded sensor calibration. All involve K64F
firmware for AI/RL deployment, data pipelines, and sensor preprocessing.

No overlap with Miraç's milestones (OLED driver, ESP32 HTTP, SoftAP, web
control panel, fire detection).

---

## Milestone 1 — RL Training Data Logger with Ground-Truth Labeling on K64F [1%]
**Demo:** March 12, 2–3 PM, Room A3058
**Estimated effort:** 5 hours

### Description
Implement firmware on the FRDM-K64F that computes and logs the full 16-feature
RL state vector (not raw sensor values) to a dedicated `training_data.csv` on the
microSD card via SPI every 2.5 seconds. The 16 features include: PM1.0, PM2.5,
PM10, temperature, humidity, pressure, gas resistance, MQ analog, MQ digital,
TVOC, eCO2, AQI category, PM2.5 delta, PM10 delta, temp-humidity index, and
gas ratio — all computed in real time from the raw sensor readings. Two buttons
provide ground-truth labeling: press SW2 briefly to label current readings as
DANGER (1), press SW3 to label as SAFE (0). Each CSV row includes a timestamp,
all 16 features, and the human-assigned label. An RGB LED shows green during
SAFE logging and red during DANGER logging. This creates real labeled hardware
training data for retraining the RL model. This is distinct from Miraç's raw
sensor CSV logging — this logs the processed feature vector with human labels.

### Deliverables
- `k64f/ember_training_logger.h` — header: feature struct, function declarations
- `k64f/ember_training_logger.c` — implementation: 16-feature computation, SD write, button labeling, RGB LED
- `training_data.csv` format: `timestamp,pm1,pm2_5,pm10,temp,hum,pres,gas,mq_a,mq_d,tvoc,eco2,aqi_cat,delta_pm25,delta_pm10,thi,gas_ratio,label`

### Completion Criteria (Yes/No)
- [ ] K64F computes all 16 RL features from raw sensor readings each cycle
- [ ] Features + timestamp written to training_data.csv on microSD via SPI
- [ ] Press SW2 → current and subsequent readings labeled DANGER (LED turns red)
- [ ] Press SW3 → current and subsequent readings labeled SAFE (LED turns green)
- [ ] CSV contains: timestamp, 16 features, label column (0 or 1)
- [ ] Serial terminal shows computed features and current label each cycle

---

## Milestone 2 — Port RL Inference to C on FRDM-K64F — On-Board AI Decision Making [1%]
**Demo:** March 17, regular lab time
**Estimated effort:** 5 hours

### Description
Port the trained DQN agent onto the FRDM-K64F microcontroller by implementing
the RL inference engine in embedded C. The Q-table from `ember_rl_policy.h` is
loaded into flash memory. Each sensor cycle (every 2.5 seconds), the K64F
collects readings from all sensors (PMS5003, BME680, MQ), constructs the
16-feature state vector in memory, performs Q-value lookup from the flash-stored
policy table, and selects the action (ALARM ON or OFF) with the highest Q-value.
The alarm decision is made entirely on-board with zero network dependency. The
result is displayed on the serial terminal showing: raw sensor values, computed
state features, Q-values for both actions, and selected action. The physical
alarm output (GPIO PTA2) activates based on the on-board decision.

### Deliverables
- `k64f/ember_rl_policy.h` — Q-table exported from trained DQN model, compiled into K64F flash
- `k64f/ember_rl_inference.h/.c` — 16-feature state vector construction and Q-value lookup
- GPIO PTA2 output driven by on-board RL decision

### Completion Criteria (Yes/No)
- [ ] `ember_rl_policy.h` compiled and loaded into K64F flash memory
- [ ] K64F reads all sensors and constructs 16-feature state vector each cycle
- [ ] Q-value lookup performed from flash-stored policy table on-board
- [ ] Alarm decision made locally without any network call
- [ ] Serial terminal displays: sensor values, state features, Q-values, action
- [ ] Physical alarm on PTA2 activates/deactivates based on on-board RL decision

---

## Milestone 3 — Dual-Mode RL — Cloud API + Local Fallback with Automatic WiFi Failover [1%]
**Demo:** March 19, 2–3 PM, Room A3058
**Estimated effort:** 5 hours

### Description
Implement a dual-mode inference system on the K64F that uses the cloud API
(Render.com) when WiFi is available and automatically falls back to the on-board
C policy (from Milestone 2) when WiFi is disconnected or the API is unreachable.
The K64F monitors ESP32 WiFi connection status each cycle using AT+CWJAP? query.
When connected, sensor data is sent to the cloud API via ESP32 HTTP POST and the
cloud prediction is used. When WiFi drops or the API returns an error or times
out (>3 seconds), the system seamlessly switches to local RL inference using the
flash-stored Q-table with no interruption in alarm monitoring. A mode indicator
on the serial terminal shows CLOUD or LOCAL for each decision cycle. When WiFi
reconnects, the system automatically switches back to cloud mode.

### Deliverables
- `k64f/ember_dual_mode.h/.c` — dual-mode state machine with AT+CWJAP? WiFi polling
- Automatic fallback to `ember_rl_policy.h` on WiFi loss or API timeout
- Serial terminal mode indicator: CLOUD or LOCAL each cycle

### Completion Criteria (Yes/No)
- [ ] System uses cloud API prediction when WiFi is connected
- [ ] Disconnect WiFi → system switches to local RL within one cycle (2.5s)
- [ ] Alarm decisions continue without interruption during WiFi loss
- [ ] Serial terminal shows mode indicator: CLOUD or LOCAL each cycle
- [ ] Reconnect WiFi → system automatically returns to cloud mode
- [ ] API timeout (>3 seconds) also triggers fallback to local mode

---

## Milestone 4 — Embedded Alert History + Event Replay on K64F [1%]
**Demo:** March 24, regular lab time
**Estimated effort:** 5 hours

### Description
When the on-board RL agent (from Milestone 2) triggers an alarm, the K64F logs
the full event to `alert_log.csv` on the microSD card via SPI. Each alert entry
includes: timestamp, all 16 RL features at the moment of alarm, Q-values for
both actions (ON and OFF), the selected action, and the inference mode (CLOUD or
LOCAL from Milestone 3). The firmware also implements two serial commands for
post-incident analysis: typing "REPLAY" reads back and prints the last 10 alert
events from the SD card in a formatted table, and typing "STATS" computes and
displays summary statistics — total alert count, average alert duration, most
frequent time-of-day for alerts, and the sensor feature with the highest average
value during alarm events (identifying the primary trigger). All processing
happens on the K64F with no server dependency.

### Deliverables
- `k64f/ember_alert_log.h/.c` — SD card alert logging on alarm trigger
- Serial command "REPLAY" → prints last 10 alert events
- Serial command "STATS" → summary statistics (count, duration, peak time, primary trigger)

### Completion Criteria (Yes/No)
- [ ] RL alarm event triggers full log entry to alert_log.csv on microSD via SPI
- [ ] Each entry contains: timestamp, 16 features, Q-values, action, mode
- [ ] Serial command "REPLAY" prints last 10 alert events from SD card
- [ ] Serial command "STATS" computes and displays alert summary statistics
- [ ] Stats include: total count, avg duration, peak time, primary trigger feature
- [ ] All processing runs on K64F — no server or network required

---

## Milestone 5 — Embedded Sensor Calibration Routine on K64F with SD Card Storage [1%]
**Demo:** March 26, 2–3 PM, Room A3058
**Estimated effort:** 5 hours

### Description
Implement a sensor calibration mode on the FRDM-K64F triggered by holding the
onboard button (SW2) for 3 seconds. When activated, the system enters calibration
mode and collects baseline readings from all sensors (PMS5003 PM values, BME680
temperature/humidity/pressure/gas resistance, MQ analog/digital) over a
configurable period (default: 60 seconds / 24 cycles at 2.5s intervals). The
routine computes per-feature normalization parameters (min, max, mean, standard
deviation) from the baseline samples. These parameters are written to a
calibration file (`calibration.csv`) on the microSD card via SPI. On subsequent
boots, the K64F reads `calibration.csv` from SD and uses the stored parameters to
normalize raw sensor readings before constructing the 16-feature state vector
for the RL agent. This improves RL accuracy by ensuring input features match
the scale the model was trained on. Serial terminal displays calibration
progress, computed parameters, and confirmation of SD card write. An RGB LED
blinks during calibration and turns solid green when complete.

### Deliverables
- `k64f/ember_calibration.h/.c` — SW2 long-press detection, calibration state machine
- Per-feature min/max/mean/std computation from 24-cycle baseline window
- SD card write/read of `calibration.csv` via SPI
- Normalization applied to RL state vector on every subsequent boot

### Completion Criteria (Yes/No)
- [ ] Hold SW2 for 3 seconds → system enters calibration mode (LED blinks)
- [ ] Baseline readings collected from all sensors over 60-second window
- [ ] Min, max, mean, std computed for each sensor feature
- [ ] Calibration parameters written to calibration.csv on microSD via SPI
- [ ] On boot, K64F reads calibration.csv and applies normalization to RL input
- [ ] Serial terminal shows calibration progress and computed parameters

---

## How All Three Requirements Are Met

| Requirement | Milestone |
|---|---|
| (a) Server + graphical real-time display | Milestone 3: cloud mode sends data to API + live dashboard |
| (b) AI causes action on external device | Milestone 2: on-board RL drives alarm GPIO (PTA2) |
| | Milestone 3: dual-mode RL drives alarm in both cloud and local modes |
| (c) Digital twin with real-time sync | Covered by Miraç's Milestone 4 (web control panel) + Milestone 3 cloud mode provides live state to dashboard |

---

## Overlap Check with Miraç

| Miraç owns | Rajini owns |
|---|---|
| OLED display driver | RL feature computation (16-feature vector) |
| ESP32 AT/HTTP sending | On-board RL inference in C |
| SoftAP WiFi provisioning | Dual-mode cloud/local failover |
| Web control panel + K64F config | Training data logger with human labels |
| Fire detection algorithm | Alert history + event replay |
| Raw sensor CSV logging | Sensor calibration routine |

**No overlap.** Miraç handles hardware drivers and connectivity. Rajini handles AI/RL deployment, data pipelines, and sensor preprocessing.

---

## Summary Table

| Milestone | Description | Demo Date | Hours |
|---|---|---|---|
| 1 | RL Training Data Logger with Ground-Truth Labeling | Mar 12 | 5 hrs |
| 2 | Port RL Inference to C on K64F | Mar 17 | 5 hrs |
| 3 | Dual-Mode Cloud + Local RL Failover | Mar 19 | 5 hrs |
| 4 | Embedded Alert History + Event Replay | Mar 24 | 5 hrs |
| 5 | Embedded Sensor Calibration with SD Card | Mar 26 | 5 hrs |
| **Total** | | | **25 hrs** |
