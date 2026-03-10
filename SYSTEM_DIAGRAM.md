# Ember AI — Full System Diagram
How everything connects together.

---

## The Big Picture

```
┌─────────────────────────────────────────────────────────────────┐
│                        PHYSICAL HARDWARE                        │
│                         (Mirac's part)                          │
│                                                                 │
│   PMS5003 ──┐                                                   │
│   BME680  ──┼──► FRDM-K64F ──► ESP32 ──► WiFi (internet)       │
│   MQ Gas  ──┘    (reads sensors    (AT+CIPSEND   (Render.com)   │
│                   every 2.5s)       HTTP POST)                  │
└─────────────────────────────────────────────────────────────────┘
                                            │
                                            │ HTTP POST
                                            │ (sensor JSON)
                                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                    RENDER.COM SERVER                            │
│               https://ember-ai-ews2.onrender.com               │
│                                                                 │
│   ┌─────────────────────────────────────────────────────┐      │
│   │              MILESTONE 1 — REST API + DASHBOARD      │      │
│   │                   api/server.py                     │      │
│   │                                                     │      │
│   │   POST /predict ──► AI Model ──► alarm ON/OFF       │      │
│   │   GET  /history ──► SQLite DB ──► last 50 results   │      │
│   │   GET  /status  ──► uptime + model version          │      │
│   │   GET  /        ──► Live Chart.js Dashboard         │      │
│   └──────────────────────┬──────────────────────────────┘      │
│                          │ JSON response includes:             │
│                          │ { alarm, aqi, command }             │
│                          ▼                                      │
│   ┌─────────────────────────────────────────────────────┐      │
│   │       MILESTONE 4 — Digital Twin Panel              │      │
│   │         api/templates/digital_twin.html             │      │
│   │                                                     │      │
│   │   Virtual PM2.5 gauge  ──► mirrors K64F reading     │      │
│   │   Virtual temp/humidity dials                       │      │
│   │   Virtual alarm indicator (grey/flashing red)       │      │
│   │   Arm/Disarm toggle ──► POST /arm ──► K64F          │      │
│   └─────────────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────────────┘
                          │
                          │ JSON response (alarm + command)
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                    FRDM-K64F FIRMWARE                           │
│                        (Rajini's part)                          │
│                                                                 │
│   ┌────────────────────────────────────────────────────┐        │
│   │    MILESTONE 2 — On-Board RL Inference (C)         │        │
│   │                                                    │        │
│   │   ember_rl_policy.h loaded into flash              │        │
│   │   16-feature state vector → Q-value lookup         │        │
│   │   → ALARM ON/OFF decision (no network needed)      │        │
│   │   → GPIO PTA2 activates physical alarm             │        │
│   └────────────────────────────────────────────────────┘        │
│                                                                 │
│   ┌────────────────────────────────────────────────────┐        │
│   │    MILESTONE 3 — Dual-Mode WiFi Failover           │        │
│   │                                                    │        │
│   │   AT+CWJAP? → WiFi connected?                      │        │
│   │     YES → use cloud API prediction (CLOUD mode)    │        │
│   │     NO  → use local Q-table (LOCAL mode)           │        │
│   │   Serial: [CLOUD] or [LOCAL] each cycle            │        │
│   └────────────────────────────────────────────────────┘        │
│                                                                 │
│   ┌────────────────────────────────────────────────────┐        │
│   │    MILESTONE 4 — Command Parser (bidirectional)    │        │
│   │                                                    │        │
│   │   Parse command field in API response JSON         │        │
│   │   "arm"    → enable PTA2 alarm output              │        │
│   │   "disarm" → disable PTA2 alarm output             │        │
│   │   "status" → dump all sensor values over serial    │        │
│   └────────────────────────────────────────────────────┘        │
│                                                                 │
│   ┌────────────────────────────────────────────────────┐        │
│   │    MILESTONE 5 — Sensor Calibration (SW2 + SD)     │        │
│   │                                                    │        │
│   │   Hold SW2 3s → enter calibration mode (LED blink) │        │
│   │   60s window → collect min/max/mean/std per sensor │        │
│   │   Write calibration.csv to microSD via SPI         │        │
│   │   On boot → read calibration.csv → normalize RL   │        │
│   │   input features before Q-value lookup             │        │
│   └────────────────────────────────────────────────────┘        │
└─────────────────────────────────────────────────────────────────┘
```

---

## How One Sensor Reading Flows Through the Whole System

```
Step 1 — K64F reads sensors (every 2.5 seconds)
         PM2.5=709, PM10=812, Temp=27.5, MQ=0.439V ...
         (normalized using calibration.csv if available — M5)

Step 2 — Check WiFi mode (M3)
         AT+CWJAP? → connected?
           YES → go to Step 3 (CLOUD mode)
           NO  → go to Step 4b (LOCAL mode)

Step 3 — K64F sends to Render.com via ESP32 AT+CIPSEND (M1)
         POST https://ember-ai-ews2.onrender.com/predict
         Body: { "PM2.5": 709, "PM10": 812, ... }

Step 4a — Cloud: AI model decides (best_model.zip)
          16 features → neural network → ALARM ON
          Response: { "alarm": "ON", "aqi": 500, "command": null }

Step 4b — Local: Q-table lookup (ember_rl_policy.h) (M2)
          16 features → Q-value table → ALARM ON (no network)

Step 5 — K64F parses response (M1 + M4)
         Extract alarm → drive PTA2 GPIO
         Extract command → execute arm/disarm/status (M4)
         Serial: [CLOUD] alarm=ON  AQI=500  cmd=none

Step 6 — Server stores result in database (M1)
         Saves: timestamp, PM2.5, alarm=ON, AQI=500

Step 7a — Dashboard (M1) polls /history every 2.5s
          Chart.js updates → alarm indicator turns RED

Step 7b — Digital Twin (M4) polls /history
          Virtual PM2.5 gauge → 709
          Virtual alarm indicator → flashing RED
          User toggles disarm → POST /arm → K64F picks up next cycle
```

---

## Who Does What

```
┌─────────────────────────────────────────────────────────────────┐
│  MIRAC                          │  RAJINI                       │
│─────────────────────────────────│───────────────────────────────│
│  FRDM-K64F hardware             │  AI model (DQN training)      │
│  PMS5003 sensor                 │  REST API (Flask server)      │
│  BME680 sensor                  │  Real-time dashboard (M1)     │
│  MQ gas sensor                  │  K64F sensor pipeline (M1)    │
│  ESP32 WiFi module              │  On-board RL in C (M2)        │
│  SD card logging                │  Dual-mode WiFi failover (M3) │
│  Physical alarm (LED/buzzer)    │  Digital twin + cmd parse (M4)│
│  OLED display driver            │  SD calibration routine (M5)  │
│  SoftAP WiFi provisioning       │  Render.com deployment        │
│  Web control panel              │  GitHub repository            │
│  Fire detection (M5)            │                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## Without Real Hardware (Demo Mode)

```
simulate_hardware.py
(sends 7 fake readings)
        │
        │ HTTP POST (same format as real hardware)
        ▼
Render.com API → AI Model → Dashboard

Everything works exactly the same.
The only difference: data comes from the simulator instead of K64F.
```

---

## Milestone Progress

```
Milestone 1 — K64F Pipeline + Dashboard          ✅ DONE  (demo Mar 12)
Milestone 2 — On-Board RL Inference in C         ⬜ TODO  (demo Mar 17)
Milestone 3 — Dual-Mode Cloud/Local Failover     ⬜ TODO  (demo Mar 19)
Milestone 4 — Digital Twin + Bidir Cmd Parse     ⬜ TODO  (demo Mar 24)
Milestone 5 — Embedded Sensor Calibration        ⬜ TODO  (demo Mar 26)
```
