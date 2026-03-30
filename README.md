# Ember — Real-Time Air Quality & Fire Detection System

> **SEP600 Capstone Project** — Seneca Polytechnic, School of Software Design & Data Science

Ember is a full-stack IoT system that monitors indoor air quality using five sensors, runs reinforcement-learning inference both on-device and in the cloud, detects fire/smoke events in real time, and streams everything to a live web dashboard.

---

## Table of Contents

- [Overview](#overview)
- [System Architecture](#system-architecture)
- [Repository Structure](#repository-structure)
- [MCU Firmware (FRDM-K64F)](#mcu-firmware-frdm-k64f)
- [Cloud API & AI/ML Pipeline](#cloud-api--aiml-pipeline)
- [Hardware Components](#hardware-components)
- [Pin Mapping](#pin-mapping)
- [API Endpoints](#api-endpoints)
- [Build & Deploy](#build--deploy)
- [Configuration](#configuration)
- [Serial Commands](#serial-commands)
- [GenAI Declaration](#genai-declaration)
- [Team](#team)

---

## Overview

Ember monitors indoor air quality using five different sensors and makes alarm decisions through two parallel paths:

1. **On-board RL inference** — A trained Deep Q-Network (DQN) runs directly on the FRDM-K64F MCU with no cloud dependency
2. **Cloud AI prediction** — Sensor data is POSTed to a Flask REST API running a DQN model on Render.com

The system also runs a dedicated **fire/smoke detection** algorithm using multi-sensor weighted rate-of-change scoring, completely independent of the AQI pipeline.

### Key Capabilities

| Feature | Component |
|---------|-----------|
| 5-sensor fusion (PM2.5, VOC, temp/hum, gas, pressure) | MCU |
| On-board DQN reinforcement learning inference | MCU |
| Fire & smoke detection with debounce/cooldown | MCU |
| Sensor calibration with SD card persistence | MCU |
| OLED 4-page dashboard with button cycling | MCU |
| SD card CSV logging with daily rotation | MCU |
| ESP8266 Wi-Fi with SoftAP captive portal | MCU |
| Alert history ring buffer (REPLAY/STATS) | MCU |
| Flask REST API with RL model serving | Cloud |
| Real-time Chart.js web dashboard | Cloud |
| Web control panel (arm/disarm/test alarm) | Cloud |
| DQN training pipeline with Stable-Baselines3 | Cloud |
| AQI scoring engine with EPA breakpoints | Cloud |
| SMS alerts via Twilio | Cloud |
| Remote config polling from MCU | Cloud |

---

## System Architecture

```
┌───────────────────────────────────────────────────────────────┐
│                     FRDM-K64F (Cortex-M4)                     │
│                                                               │
│  PMS5003 ──UART──┐                                            │
│  ENS160  ──I2C───┤   Sensor     DQN RL        Fire            │
│  BME680  ──I2C───┼─► Fusion ──► Inference  +  Detection       │
│  MQ Gas  ──ADC───┤      │            │            │           │
│                  │      │            ▼            ▼           │
│  SH1106  ◄─I2C──┘   SD Card     ALARM        FIRE ALERT      │
│  OLED            ◄── Logger      Decision     Decision        │
│                          │            │            │          │
│                          │            └─────┬──────┘          │
│                          │                  │                 │
│                     ESP8266/ESP32 ◄──────────┘                │
│                      Wi-Fi Module                             │
└──────────────────────────┬────────────────────────────────────┘
                           │ HTTP POST / GET
                           ▼
┌──────────────────────────────────────────────────────────────┐
│                  Cloud (Render.com)                            │
│                                                               │
│   Flask REST API                                              │
│   ├── POST /predict  ← sensor JSON → alarm + AQI             │
│   ├── GET  /history  ← last 50 predictions                   │
│   ├── GET  /status   ← uptime & model version                │
│   ├── GET  /         ← real-time Chart.js dashboard           │
│   ├── GET  /control  ← web control panel                      │
│   ├── POST /command  ← arm/disarm/test alarm                  │
│   └── POST /config   ← MCU polls for pending config           │
│                                                               │
│   DQN Model (Stable-Baselines3 + PyTorch)                     │
│   AQI Engine (EPA breakpoint tables)                          │
│   SQLite Database (prediction history)                        │
│   Twilio SMS Alerts                                           │
└───────────────────────────────────────────────────────────────┘
```

---

## Repository Structure

```
ember-ai/
├── README.md                    # This file
│
├── mcu/                         # ── MCU Firmware (Mirac) ──
│   ├── main.cpp                 # Core firmware (3,478 lines)
│   ├── mbed_app.json            # Mbed OS target config
│   ├── .mbedignore              # Exclude unused Mbed OS components
│   ├── README.md                # Detailed MCU documentation
│   ├── SYSTEM_DOCUMENTATION.md  # Full system design document
│   ├── PMS5003_PM25_SENSOR_GUIDE.md
│   └── ember/                   # Modular firmware libraries
│       ├── ember_rl_inference.cpp/.h    # DQN forward pass
│       ├── ember_rl_policy.h            # Exported neural network weights
│       ├── ember_rl_training.cpp/.h     # Training data collection
│       ├── ember_fire_detection.cpp/.h  # Fire & smoke algorithm
│       ├── ember_calibration.cpp/.h     # Sensor calibration
│       └── ember_alert_log.cpp/.h       # Alert ring buffer & replay
│
├── api/                         # ── Cloud REST API (Rajini) ──
│   ├── server.py                # Flask app with all endpoints
│   ├── database.py              # SQLite prediction history
│   ├── simulate_hardware.py     # Hardware simulation for testing
│   ├── __init__.py
│   └── templates/
│       ├── dashboard.html       # Real-time Chart.js dashboard
│       └── control.html         # Web control panel
│
├── src/                         # ── AI/ML Pipeline (Rajini) ──
│   ├── train.py                 # DQN training with Stable-Baselines3
│   ├── predict.py               # Model inference
│   ├── evaluate.py              # Model evaluation
│   ├── aqi_engine.py            # Rule-based AQI calculation
│   ├── env.py                   # Gymnasium RL environment
│   ├── data_generator.py        # Synthetic training data
│   └── data_loader.py           # CSV data loading
│
├── models/                      # Trained model artifacts
│   └── best_model.zip           # DQN model checkpoint
│
├── data/                        # Training datasets
│   └── DATASETS.md
│
├── Procfile                     # Render.com deployment config
└── requirements.txt             # Python dependencies
```

---

## MCU Firmware (FRDM-K64F)

The embedded firmware runs on an **NXP FRDM-K64F** (ARM Cortex-M4 @ 120 MHz, 256 KB RAM) using Mbed OS.

### Sensor Suite

| Sensor | Interface | Measurements |
|--------|-----------|-------------|
| PMS5003 | UART 9600 | PM1.0, PM2.5, PM10 (µg/m³) |
| ENS160 | I2C 0x53 | TVOC (ppb), eCO2 (ppm) |
| BME680 | I2C 0x76 | Temperature, humidity, pressure, gas resistance |
| MQ Gas (Flying Fish) | Analog | Combustible gas concentration |

### Firmware Modules

- **DQN RL Inference** — 10-input → 64-neuron → 2-output neural network with ReLU, runs on-device in <1 ms
- **Fire Detection** — Weighted scoring: PM2.5 (0.55) + MQ gas (0.25) + temperature (0.20) against rolling 15-cycle baseline. Debounce (3 cycles), cooldown (60s), absolute PM2.5 override (≥150 µg/m³)
- **Calibration** — 60-second baseline routine (24 samples), computes per-feature statistics, saves to SD card
- **Alert Logger** — 20-event ring buffer with REPLAY/STATS serial commands
- **SoftAP Portal** — ESP8266 captive Wi-Fi provisioning with PIN security
- **OLED Dashboard** — 4-page SH1106 display: PM, temperature/humidity, gas/TVOC, pressure/eCO2

See [mcu/README.md](mcu/README.md) for full pin mapping, build instructions, and configuration details.

---

## Cloud API & AI/ML Pipeline

The cloud backend runs on **Render.com** as a Flask application with a Stable-Baselines3 DQN model.

### API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/predict` | Accept sensor JSON, return alarm decision + AQI score |
| `GET` | `/history` | Return last 50 predictions with timestamps |
| `GET` | `/status` | Server uptime and model version |
| `GET` | `/devices` | List all connected device IDs |
| `GET` | `/` | Real-time Chart.js monitoring dashboard |
| `GET` | `/control` | Web control panel (arm/disarm, thresholds, test alarm) |
| `POST` | `/command` | Send configuration commands from web panel |
| `POST` | `/config` | MCU polls for pending config (consumed on read) |

### ML Pipeline

- **Framework**: Stable-Baselines3 with PyTorch
- **Algorithm**: Deep Q-Network (DQN)
- **Environment**: Custom Gymnasium env simulating air quality scenarios
- **Training**: `python src/train.py --generate` to generate data and train
- **Evaluation**: `python src/evaluate.py` for model performance metrics

---

## Hardware Components

| Component | Model | Purpose |
|-----------|-------|---------|
| MCU | NXP FRDM-K64F | ARM Cortex-M4 @ 120 MHz |
| PM Sensor | PMS5003 | Particulate matter (PM1.0, PM2.5, PM10) |
| AQ Sensor | ENS160 | TVOC and eCO2 |
| Env Sensor | BME680 | Temperature, humidity, pressure, gas resistance |
| Gas Sensor | Flying Fish MQ | Combustible gas detection |
| Display | SH1106 1.3" OLED | 128×64 pixel dashboard |
| Wi-Fi | ESP8266 / ESP32 | AT-command Wi-Fi module |
| Storage | MicroSD Card | CSV data logging (up to 30 GB) |
| Alarm | Piezo Buzzer | Audible alert |

---

## Pin Mapping

```
FRDM-K64F Pin Assignments
─────────────────────────
UART3 (ESP Wi-Fi):    TX → PTC17    RX → PTC16    115200 baud
UART1 (PMS5003):      TX → PTC4     RX → PTC3     9600 baud
I2C0  (ENS160/BME680/OLED): SDA → PTE25    SCL → PTE24
SPI0  (MicroSD):      MOSI → PTD2   MISO → PTD3   SCK → PTD1   CS → PTD0
Analog (MQ Gas):      AO → PTB2     DO → PTB3
GPIO:                 Buzzer → PTC12   Button → PTC2 (PullUp)
```

---

## Build & Deploy

### MCU Firmware

```bash
# Compile
cd mcu/
mbed compile -m K64F -t GCC_ARM

# Flash — copy .bin to DAPLINK USB drive
cp BUILD/K64F/GCC_ARM/*.bin /Volumes/DAPLINK/
```

### Cloud API (Local)

```bash
pip install -r requirements.txt
python -m api.server
# Opens on http://localhost:5000
```

### Cloud API (Render.com)

Deployed automatically via `Procfile`:
```
web: gunicorn api.server:app
```

---

## Configuration

### MCU Parameters (`mcu/ember/`)

| Parameter | File | Default | Description |
|-----------|------|---------|-------------|
| `FIRE_THRESHOLD` | `ember_fire_detection.cpp` | 0.50 | Fire score to trigger alert |
| `W_PM / W_MQ / W_TEMP` | `ember_fire_detection.cpp` | 0.55 / 0.25 / 0.20 | Sensor fusion weights |
| `BASELINE_WINDOW` | `ember_fire_detection.cpp` | 15 | Rolling baseline cycles |
| `CALIB_SAMPLE_COUNT` | `ember_calibration.cpp` | 24 | Calibration samples |
| `MAX_ALERT_ENTRIES` | `ember_alert_log.cpp` | 20 | Alert buffer capacity |

### Mbed OS (`mcu/mbed_app.json`)
- Floating-point printf enabled
- Main thread stack: 8192 bytes
- SD card SPI pins: PTD0-PTD3

---

## Serial Commands

Connect to FRDM-K64F at **115200 baud**:

| Command | Description |
|---------|-------------|
| `REPLAY` | Print last 20 alert events with timestamps and Q-values |
| `STATS` | Show alert statistics: count, average AQI, peak AQI |
| `CALIB` | Trigger 60-second sensor calibration routine |

---

## GenAI Declaration

This project used **Claude AI (Anthropic)** and **GitHub Copilot** as coding assistants. Every source file (both MCU C++ and cloud Python) contains an inline GenAI Declaration block specifying which sections received AI assistance.

All AI-generated and AI-assisted code was reviewed, tested, and validated by the development team:
- MCU firmware validated on physical FRDM-K64F hardware with real sensors
- Cloud API tested locally and deployed on Render.com

---

## Team

**Ember** — SEP600 Capstone, Seneca Polytechnic

- **Mirac Ozcan** — MCU Firmware (FRDM-K64F, sensors, RL inference, fire detection, calibration, Wi-Fi, SD logging)
- **Rajini** — Cloud API, AI/ML Training Pipeline, Web Dashboard, SMS Alerts
