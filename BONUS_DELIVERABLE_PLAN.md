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
decisions. All five bonus milestones involve FRDM-K64F firmware: sensor-to-cloud
pipeline, on-board RL inference in C, dual-mode cloud/local failover,
bidirectional digital twin command parsing, and embedded sensor calibration.

---

## Milestone 1 — K64F-to-Cloud Sensor Pipeline + Real-Time Web Dashboard [1%]
**Demo:** March 12, 2–3 PM, Room A3058
**Estimated effort:** 5 hours
**Status: COMPLETED**

### Description
Write K64F firmware that packages all sensor readings (PMS5003, BME680, MQ) into
a JSON payload and transmits it to the Render.com Flask API via ESP32 AT+CIPSEND
HTTP POST every 2.5 seconds. The K64F parses the API JSON response to extract
the alarm decision and AQI estimate. On the server side, all predictions are
logged to SQLite and displayed on a real-time web dashboard with Chart.js
(PM2.5, AQI, MQ gas voltage charts updating every 2.5 seconds, alarm status
indicator green=SAFE / red=DANGER). The embedded deliverable is the K64F
firmware that constructs, transmits, and parses the HTTP exchange. Satisfies
requirement (a): server connectivity with real-time graphical display.

### Deliverables
- K64F firmware: constructs sensor JSON payload, sends via ESP32 AT+CIPSEND, parses response
- `api/server.py` — Flask server with `POST /predict`, `GET /history`, `GET /status`, `GET /devices`
- `api/database.py` — SQLite logging of all predictions
- `api/templates/dashboard.html` — real-time Chart.js dashboard
- Server live and accessible via public URL on Render.com

### Completion Criteria (Yes/No)
- [x] K64F firmware constructs sensor JSON payload from live readings each cycle
- [x] JSON transmitted to Render.com API via ESP32 AT+CIPSEND HTTP POST
- [x] K64F parses API response and extracts alarm decision + AQI value
- [x] Serial terminal shows: sent payload, HTTP status code, parsed response
- [x] Dashboard loads with live-updating charts from K64F sensor data
- [x] Alarm indicator changes colour when AI detects danger

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
- `ember_rl_policy.h` — Q-table exported from trained DQN model, compiled into K64F flash
- K64F firmware: 16-feature state vector construction and Q-value lookup loop
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
- K64F firmware: dual-mode state machine with AT+CWJAP? WiFi status polling
- Automatic fallback to `ember_rl_policy.h` on WiFi loss or API timeout
- Auto-recovery to cloud mode when WiFi reconnects
- Serial terminal mode indicator: CLOUD or LOCAL each cycle

### Completion Criteria (Yes/No)
- [ ] System uses cloud API prediction when WiFi is connected
- [ ] Disconnect WiFi → system switches to local RL within one cycle (2.5s)
- [ ] Alarm decisions continue without interruption during WiFi loss
- [ ] Serial terminal shows mode indicator: CLOUD or LOCAL each cycle
- [ ] Reconnect WiFi → system automatically returns to cloud mode
- [ ] API timeout (>3 seconds) also triggers fallback to local mode

---

## Milestone 4 — Digital Twin with Bidirectional K64F Command Parsing [1%]
**Demo:** March 24, regular lab time
**Estimated effort:** 5 hours

### Description
Implement bidirectional communication on the K64F for digital twin
synchronisation. The K64F firmware is extended to parse incoming commands from
the API response JSON (arm/disarm alarm, request sensor dump, set alarm mode).
When the API response includes a command field, the K64F executes it immediately:
"arm" enables the alarm output on PTA2, "disarm" disables it, "status" triggers
a full sensor dump over serial. On the web side, a digital twin panel displays
virtual gauges (PM2.5, temperature, humidity, MQ gas) and an alarm indicator
that mirrors the physical K64F state. A virtual alarm toggle sends the
arm/disarm command through the API, which the K64F picks up on the next polling
cycle and executes. The embedded deliverable is the K64F command parser and
executor. Satisfies requirement (c): digital twin with real-time synchronisation
of state and behaviour.

### Deliverables
- K64F firmware: JSON command parser for "arm", "disarm", "status" fields in API response
- GPIO PTA2 controlled by arm/disarm commands from web
- `api/templates/digital_twin.html` — virtual gauges + alarm toggle panel
- New `POST /arm` endpoint — receives arm/disarm from dashboard, queues command for K64F

### Completion Criteria (Yes/No)
- [ ] K64F parses command field from API JSON response each cycle
- [ ] "arm" command from web → K64F enables physical alarm on PTA2
- [ ] "disarm" command from web → K64F disables physical alarm
- [ ] Serial terminal confirms receipt and execution of each command
- [ ] Virtual gauges on web dashboard match physical sensor readings within 5 seconds
- [ ] Virtual alarm toggle → K64F alarm responds within one polling cycle

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
- K64F firmware: SW2 long-press detection, calibration state machine
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
| (a) Server + graphical real-time display | Milestone 1: K64F sends data to API + live dashboard |
| (b) AI causes action on external device | Milestone 2: On-board RL drives alarm GPIO (PTA2) |
| | Milestone 3: Dual-mode RL drives alarm in both cloud and local modes |
| (c) Digital twin with real-time sync | Milestone 4: Bidirectional K64F command parsing + web twin |

---

## Summary Table

| Milestone | Description | Demo Date | Hours |
|---|---|---|---|
| 1 | K64F-to-Cloud Sensor Pipeline + Dashboard | Mar 12 | 5 hrs |
| 2 | Port RL Inference to C on K64F | Mar 17 | 5 hrs |
| 3 | Dual-Mode RL — Cloud + Local Failover | Mar 19 | 5 hrs |
| 4 | Digital Twin with Bidirectional K64F Command Parsing | Mar 24 | 5 hrs |
| 5 | Embedded Sensor Calibration with SD Card | Mar 26 | 5 hrs |
| **Total** | | | **25 hrs** |
