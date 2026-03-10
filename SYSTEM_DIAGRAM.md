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
│   BME680  ──┼──► FRDM-K64F ──► ESP32 ──► "Mirac" WiFi Hotspot  │
│   MQ Gas  ──┘    (reads sensors    (sends data      (internet   │
│                   every 2.5s)       via WiFi)        via phone) │
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
│   │              MILESTONE 1 — REST API                 │      │
│   │                   api/server.py                     │      │
│   │                                                     │      │
│   │   POST /predict ──► AI Model ──► alarm ON/OFF       │      │
│   │   GET  /history ──► SQLite DB ──► last 50 results   │      │
│   │   GET  /status  ──► uptime + model version          │      │
│   └──────────────────────┬──────────────────────────────┘      │
│                          │                                      │
│              ┌───────────┼───────────┐                         │
│              ▼           ▼           ▼                         │
│   ┌──────────────┐ ┌──────────┐ ┌──────────────────────┐      │
│   │ MILESTONE 2  │ │MILESTONE │ │    MILESTONE 4        │      │
│   │  Dashboard   │ │    3     │ │    Digital Twin       │      │
│   │              │ │  Twilio  │ │                       │      │
│   │ Live charts  │ │   SMS    │ │ Virtual copy of       │      │
│   │ PM2.5 graph  │ │          │ │ physical hardware     │      │
│   │ AQI graph    │ │ alarm=ON │ │ Virtual PM2.5 gauge   │      │
│   │ MQ graph     │ │    ↓     │ │ Virtual temp dial     │      │
│   │ Alarm light  │ │ SMS sent │ │ Virtual alarm light   │      │
│   │              │ │ to phone │ │                       │      │
│   └──────────────┘ └──────────┘ └──────────────────────┘      │
│                                                                 │
│   ┌─────────────────────────────────────────────────────┐      │
│   │              MILESTONE 5 — Auto Retraining          │      │
│   │                                                     │      │
│   │  New CSV dropped → model retrains → deploys itself  │      │
│   └─────────────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────────────┘
                          │
                          │ AI Decision
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                    AI MODEL (Rajini's part)                     │
│                      models/best_model.zip                      │
│                                                                 │
│   Input: 16 sensor features                                     │
│   Algorithm: DQN (Deep Q-Network) Reinforcement Learning        │
│   Output: ALARM ON or ALARM OFF                                 │
│                                                                 │
│   Accuracy: 99.76%  |  False Negative Rate: 0.00%              │
└─────────────────────────────────────────────────────────────────┘
```

---

## How One Sensor Reading Flows Through the Whole System

```
Step 1 — Hardware reads sensors (every 2.5 seconds)
         PM2.5=709, PM10=812, Temp=27.5, MQ=0.439V ...

Step 2 — ESP32 sends to Render.com
         POST https://ember-ai-ews2.onrender.com/predict
         Body: { "PM2.5": 709, "PM10": 812, ... }

Step 3 — API receives the data (api/server.py)
         Passes it to the AI model

Step 4 — AI model decides (best_model.zip)
         16 features → neural network → ALARM ON

Step 5 — API stores result in database (api/database.py)
         Saves: timestamp, PM2.5, alarm=ON, AQI=500

Step 6 — API returns response to ESP32
         { "alarm": "ON", "aqi": 500, "category": "HAZARDOUS" }

Step 7a — ESP32 reads response
          alarm=ON → activates physical alarm GPIO on K64F

Step 7b — Dashboard (Milestone 2) polls /history
          Chart updates → alarm indicator turns RED

Step 7c — Twilio (Milestone 3) sends SMS
          "EMBER ALERT: AQI=500 (HAZARDOUS)"

Step 7d — Digital Twin (Milestone 4) polls /history
          Virtual PM2.5 gauge jumps to 709
          Virtual alarm indicator turns RED
```

---

## Who Does What

```
┌─────────────────────────────────────────────────────────────────┐
│  MIRAC                          │  RAJINI                       │
│─────────────────────────────────│───────────────────────────────│
│  FRDM-K64F hardware             │  AI model (DQN training)      │
│  PMS5003 sensor                 │  REST API (Flask server)      │
│  BME680 sensor                  │  Web dashboard (charts)       │
│  MQ gas sensor                  │  Twilio SMS alerts            │
│  ESP32 WiFi module              │  Digital twin interface       │
│  SD card logging                │  Auto retraining pipeline     │
│  Physical alarm (LED/buzzer)    │  Render.com deployment        │
│  "Mirac" WiFi hotspot           │  GitHub repository            │
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
Render.com API → AI Model → Dashboard / SMS / Digital Twin

Everything works exactly the same.
The only difference: data comes from the simulator instead of K64F.
```

---

## Milestone Progress

```
Milestone 1 — REST API          ✅ DONE  (demo Mar 12)
Milestone 2 — Dashboard         ⬜ TODO  (demo Mar 17)
Milestone 3 — Twilio SMS        ⬜ TODO  (demo Mar 19)
Milestone 4 — Digital Twin      ⬜ TODO  (demo Mar 24)
Milestone 5 — Auto Retraining   ⬜ TODO  (demo Mar 26)
```