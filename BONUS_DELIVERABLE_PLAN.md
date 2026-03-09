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
decisions. My bonus milestones extend the existing system with a REST API,
real-time web dashboard, Twilio SMS alerts, digital twin interface, and an
automated model retraining pipeline.

---

## Milestone 1 — RL Model REST API Deployed on Render.com [1%]
**Demo:** March 12, 2–3 PM, Room A3058
**Estimated effort:** 5 hours

### Description
Build a Flask REST API server that exposes the trained RL model as a live online
service, deployed on Render.com (free tier). Render.com is used instead of Vercel
because Vercel's 50MB serverless limit is too small for PyTorch + stable-baselines3.
The server accepts live sensor readings via HTTP POST and returns the AI's alarm
decision. All predictions are logged to a SQLite database with timestamps.

### Deliverables
- `api/server.py` — Flask server with the following endpoints:
  - `POST /predict` — accepts sensor JSON, returns alarm decision + AQI estimate
  - `GET /history` — returns last 50 predictions with timestamps as JSON
  - `GET /status` — returns server uptime and model version
- `api/database.py` — SQLite logging of all predictions
- Server live and accessible via public URL on Render.com

### Completion Criteria (Yes/No)
- [ ] POST /predict receives sensor JSON and returns `{ "alarm": "ON/OFF", "aqi": X }`
- [ ] API accessible via public Render.com URL from any browser
- [ ] All predictions stored in SQLite with timestamp
- [ ] GET /history returns last 50 logged predictions

---

## Milestone 2 — Real-Time Web Dashboard with Live Sensor Charts [1%]
**Demo:** March 17, regular lab time
**Estimated effort:** 5 hours

### Description
Build a web dashboard served from the Flask API server that displays incoming
sensor data in real-time graphical format. The dashboard polls the API every
2.5 seconds and updates live charts without page refresh. This satisfies
requirement (a): connectivity to a server with real-time graphical display.

### Deliverables
- `api/templates/dashboard.html` — web dashboard with:
  - Live line chart for PM2.5 readings (last 60 data points)
  - Live line chart for AQI score over time
  - Live line chart for MQ gas voltage
  - Alarm status indicator (green = SAFE, red = DANGER)
  - Sensor readings table (last 10 rows)
- Charts built with Chart.js, updating via JavaScript polling every 2.5 seconds

### Completion Criteria (Yes/No)
- [ ] Open browser → dashboard loads with live charts
- [ ] Charts update automatically every 2.5 seconds with new sensor data
- [ ] Alarm indicator changes colour when AI detects danger

---

## Milestone 3 — Twilio SMS Alert + AI-Driven Physical Alarm [1%]
**Demo:** March 19, 2–3 PM, Room A3058
**Estimated effort:** 5 hours

### Description
When the AI model returns `alarm = ON`, two actions are triggered automatically:
(1) A Twilio SMS is sent to the building owner's phone with the AQI value and
danger category. (2) A command is sent back to the K64F hardware to activate
the physical alarm output (GPIO PTA2) — replacing the old rule-based trigger
with the AI decision. This satisfies requirement (b): AI causes action on an
external physical device.

### Deliverables
- `api/alert.py` — Twilio integration module
  - Sends SMS only when alarm first transitions from OFF → ON (not every reading)
  - SMS format: `"EMBER ALERT: AQI=287 (VERY_UNHEALTHY). Danger detected at [time]."`
- Modified `POST /predict` endpoint — after AI returns alarm=ON, triggers SMS
- Demonstration: simulate high PM2.5 reading → AI detects danger → SMS received
  on phone within 10 seconds + physical alarm LED activates

### Completion Criteria (Yes/No)
- [ ] Simulate danger reading → Twilio SMS received on phone
- [ ] SMS contains correct AQI value and danger category
- [ ] No repeated SMS for sustained danger (only triggers on state change)
- [ ] Physical alarm LED activates based on AI decision

---

## Milestone 4 — Digital Twin Interface [1%]
**Demo:** March 24, regular lab time
**Estimated effort:** 5 hours

### Description
Add a digital twin panel to the web dashboard that displays a virtual
representation of the physical Ember sensor station. The virtual device mirrors
the real hardware state in real time — when physical sensor readings change,
the virtual gauges update within 5 seconds. This satisfies requirement (c):
digital twin with real-time synchronisation of state and behaviour.

### Deliverables
- `api/templates/digital_twin.html` — digital twin panel with:
  - Virtual PM2.5 gauge (needle/bar matching real reading)
  - Virtual temperature and humidity dials
  - Virtual alarm indicator (OFF = grey, ON = flashing red)
  - Virtual MQ gas voltage bar
- All virtual components update automatically from the API database
- Visual match between physical hardware state and virtual representation

### Completion Criteria (Yes/No)
- [ ] Physical PM2.5 reading changes → virtual gauge updates within 5 seconds
- [ ] Physical alarm turns ON → virtual alarm indicator turns red
- [ ] All four sensor values displayed on virtual panel match live hardware values

---

## Milestone 5 — Automated Model Retraining Pipeline [1%]
**Demo:** March 26, 2–3 PM, Room A3058
**Estimated effort:** 5 hours

### Description
Build an automated pipeline that watches for new hardware CSV files from the
SD card. When a new CSV is dropped into `data/real/`, the pipeline automatically
retrains the RL model, evaluates it, and if performance improves, deploys the
new model to the live API server without manual intervention. The dashboard
displays the current model version, last training date, and accuracy metrics.

### Deliverables
- `api/retrain_watcher.py` — file watcher script using Python `watchdog` library
  - Detects new CSV files added to `data/real/`
  - Triggers `train.py` automatically
  - Evaluates new model — if accuracy improves, replaces `best_model.zip`
  - Logs retraining history to `retraining_log.csv`
- Dashboard updated with new section:
  - "Model Version", "Last Trained", "Accuracy", "False Negative Rate"
- Demonstration: drop a new hardware CSV → model retrains → dashboard updates
  with new metrics automatically

### Completion Criteria (Yes/No)
- [ ] Drop new CSV into `data/real/` → retraining starts automatically
- [ ] New model deployed to API server if accuracy improves
- [ ] Dashboard shows updated model metrics after retraining completes

---

## How All Three Requirements Are Met

| Requirement | Milestone |
|---|---|
| (a) Server + graphical real-time display | Milestone 1 + Milestone 2 |
| (b) AI causes action on external device | Milestone 3 (SMS + physical alarm) |
| (c) Digital twin with real-time sync | Milestone 4 |

---

## Summary Table

| Milestone | Description | Demo Date | Hours |
|---|---|---|---|
| 1 | RL Model REST API on Render.com | Mar 12 | 5 hrs |
| 2 | Real-Time Web Dashboard | Mar 17 | 5 hrs |
| 3 | Twilio SMS + Physical Alarm | Mar 19 | 5 hrs |
| 4 | Digital Twin Interface | Mar 24 | 5 hrs |
| 5 | Automated Retraining Pipeline | Mar 26 | 5 hrs |
| **Total** | | | **25 hrs** |
