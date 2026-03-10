# Ember AI — Full System Diagram
How everything connects together.

---

## The Big Picture

```
┌─────────────────────────────────────────────────────────────────┐
│                        PHYSICAL HARDWARE                        │
│                                                                 │
│   PMS5003 ──┐                                                   │
│   BME680  ──┼──► FRDM-K64F                                      │
│   MQ Gas  ──┘                                                   │
│                                                                 │
│   ┌──────────────────────────────────────────────────────┐      │
│   │           RAJINI'S K64F FIRMWARE (AI side)           │      │
│   │                                                      │      │
│   │  M1: Compute 16 RL features → log training_data.csv │      │
│   │      SW2=DANGER / SW3=SAFE labels → RGB LED          │      │
│   │                                                      │      │
│   │  M2: Q-table lookup → ALARM ON/OFF → GPIO PTA2       │      │
│   │      (ember_rl_policy.h in flash, no network)        │      │
│   │                                                      │      │
│   │  M3: AT+CWJAP? → WiFi OK? → CLOUD mode               │      │
│   │                          → LOCAL mode (fallback)     │      │
│   │                                                      │      │
│   │  M4: RL alarm → log to alert_log.csv                 │      │
│   │      "REPLAY" / "STATS" serial commands              │      │
│   │                                                      │      │
│   │  M5: Hold SW2 3s → calibrate → write calibration.csv│      │
│   │      Boot → read calibration.csv → normalize input  │      │
│   └──────────────────────────────────────────────────────┘      │
│                                                                 │
│   ┌──────────────────────────────────────────────────────┐      │
│   │           MIRAC'S K64F FIRMWARE (hardware side)      │      │
│   │                                                      │      │
│   │  M1: OLED display (PM2.5, AQI, temp, alarm status)   │      │
│   │  M2: ESP32 HTTP POST to Render.com API               │      │
│   │  M3: SoftAP WiFi provisioning portal                 │      │
│   │  M4: Web control panel (arm/disarm/thresholds)       │      │
│   │  M5: Fire detection (multi-sensor fusion)            │      │
│   └──────────────────────────────────────────────────────┘      │
│                          │                                      │
│                          │ ESP32 HTTP POST (Mirac M2)           │
└──────────────────────────┼──────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                    RENDER.COM SERVER (Rajini)                   │
│               https://ember-ai-ews2.onrender.com               │
│                                                                 │
│   POST /predict ──► DQN AI Model ──► alarm ON/OFF              │
│   GET  /history ──► SQLite DB    ──► last 50 results           │
│   GET  /        ──► Chart.js Dashboard (live charts)           │
│   GET  /status  ──► uptime + model version                     │
│                                                                 │
│   Used by: M3 cloud mode + Mirac's M2 (HTTP POST)              │
└─────────────────────────────────────────────────────────────────┘
```

---

## Rajini's Milestones — K64F Firmware Flow

```
Every 2.5 seconds on K64F:

Step 1 — Read raw sensors
         PMS5003 → PM1.0, PM2.5, PM10
         BME680  → temp, humidity, pressure, gas_resistance
         MQ      → mq_analog, mq_digital

Step 2 — Compute 16 RL features (M1 + M2 + M3 + M4)
         delta_pm25 = PM2.5[now] - PM2.5[prev]
         delta_pm10 = PM10[now]  - PM10[prev]
         thi        = temp + 0.33×humidity - 4.0  (temp-humidity index)
         gas_ratio  = mq_analog / log10(gas_resistance)
         aqi_cat    = AQI category (0–5)
         + apply calibration.csv normalization (M5)

Step 3 — M1: Log features + label to training_data.csv on SD
         SW2 pressed → label=1 (DANGER), LED red
         SW3 pressed → label=0 (SAFE),   LED green

Step 4 — M2/M3: Decide alarm
         AT+CWJAP? → WiFi connected?
           YES → HTTP POST to API → use cloud prediction  [CLOUD]
           NO  → Q-table lookup in flash                  [LOCAL]

Step 5 — Drive PTA2 GPIO based on alarm decision

Step 6 — M4: If alarm ON → log event to alert_log.csv on SD
         Serial "REPLAY" → print last 10 events
         Serial "STATS"  → summary statistics
```

---

## Who Does What

```
┌─────────────────────────────────────────────────────────────────┐
│  MIRAC                          │  RAJINI                       │
│─────────────────────────────────│───────────────────────────────│
│  FRDM-K64F hardware             │  AI model (DQN training)      │
│  PMS5003 sensor driver          │  Flask REST API + dashboard   │
│  BME680 sensor driver           │  16-feature RL computation    │
│  MQ gas sensor driver           │  Training data logger (M1)    │
│  ESP32 HTTP POST to API         │  On-board RL in C (M2)        │
│  OLED display driver            │  Dual-mode failover (M3)      │
│  SoftAP WiFi provisioning       │  Alert history + replay (M4)  │
│  Web control panel              │  Sensor calibration (M5)      │
│  Raw sensor SD card logging     │  Render.com deployment        │
│  Fire detection algorithm       │  GitHub repository            │
└─────────────────────────────────────────────────────────────────┘
```

---

## SD Card Files (Rajini's firmware writes these)

```
microSD card:
├── training_data.csv   ← M1: 16 features + human label every 2.5s
├── alert_log.csv       ← M4: full event log every time alarm triggers
└── calibration.csv     ← M5: per-feature min/max/mean/std
```

---

## Milestone Progress

```
Milestone 1 — RL Training Data Logger + Labels      ⬜ TODO  (demo Mar 12)
Milestone 2 — On-Board RL Inference in C            ⬜ TODO  (demo Mar 17)
Milestone 3 — Dual-Mode Cloud/Local Failover        ⬜ TODO  (demo Mar 19)
Milestone 4 — Alert History + Event Replay          ⬜ TODO  (demo Mar 24)
Milestone 5 — Embedded Sensor Calibration           ⬜ TODO  (demo Mar 26)

Supporting infrastructure (already deployed):
Flask API + Chart.js Dashboard                      ✅ DONE  (Render.com)
```
