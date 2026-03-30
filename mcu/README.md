# Ember — Real-Time Air Quality & Fire Detection System

> **SEP600 Capstone Project** — Seneca Polytechnic, School of Software Design & Data Science

A bare-metal embedded firmware for the **NXP FRDM-K64F** (ARM Cortex-M4) that fuses data from five sensors, runs on-board reinforcement-learning inference, detects fire/smoke events in real time, and streams everything to a cloud dashboard over Wi-Fi.

---

## Table of Contents

- [Overview](#overview)
- [System Architecture](#system-architecture)
- [Hardware Components](#hardware-components)
- [Pin Mapping](#pin-mapping)
- [Software Modules](#software-modules)
- [Features](#features)
- [OLED Display Pages](#oled-display-pages)
- [Build & Flash](#build--flash)
- [Repository Structure](#repository-structure)
- [Configuration](#configuration)
- [Serial Commands](#serial-commands)
- [GenAI Declaration](#genai-declaration)
- [Team](#team)
- [License](#license)

---

## Overview

**Ember** monitors indoor air quality using five different sensors, computes a composite AQI (Air Quality Index) score, and makes alarm decisions using a trained Deep Q-Network (DQN) running directly on the MCU — no cloud required for inference. A multi-sensor fire/smoke detection algorithm provides a secondary safety layer with its own alert pipeline.

Key capabilities:
- **5-sensor fusion**: PM2.5/PM10 (PMS5003), VOC/eCO2 (ENS160), temperature/humidity/pressure/gas resistance (BME680), combustible gas (MQ sensor)
- **On-board RL inference**: 10-input → 64-neuron → 2-output DQN with ReLU activation
- **Fire & smoke detection**: Weighted rate-of-change scoring with debounce, cooldown, and absolute-threshold override
- **Sensor calibration**: Automated baseline calibration routine with SD card persistence
- **Wi-Fi connectivity**: ESP8266/ESP32 AT-command driver with SoftAP captive portal for provisioning
- **OLED dashboard**: 4-page 128×64 SH1106 display with button cycling
- **SD card logging**: Daily CSV rotation with 30 GB cap and automatic file cleanup
- **Alert history**: 20-event ring buffer with REPLAY and STATS serial commands
- **Cloud API integration**: HTTP POST of AQI scores, fire alerts, and sensor snapshots
- **Remote config**: Arm/disarm alarm and trigger test alerts from the web dashboard

---

## System Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    FRDM-K64F (Cortex-M4)                 │
│                                                          │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌───────────┐  │
│  │ PMS5003 │  │  ENS160 │  │ BME680  │  │  MQ Gas   │  │
│  │  UART1  │  │   I2C   │  │   I2C   │  │  Analog   │  │
│  └────┬────┘  └────┬────┘  └────┬────┘  └─────┬─────┘  │
│       │            │            │              │         │
│       └────────────┴─────┬──────┴──────────────┘         │
│                          │                               │
│              ┌───────────▼───────────┐                   │
│              │   Sensor Fusion &     │                   │
│              │   AQI Computation     │                   │
│              └───────────┬───────────┘                   │
│                          │                               │
│         ┌────────────────┼────────────────┐              │
│         │                │                │              │
│  ┌──────▼──────┐  ┌──────▼──────┐  ┌──────▼──────┐      │
│  │  DQN RL     │  │    Fire     │  │   Alert     │      │
│  │  Inference  │  │  Detection  │  │   Logger    │      │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘      │
│         │                │                │              │
│         └────────────────┼────────────────┘              │
│                          │                               │
│         ┌────────────────┼─────────────────┐             │
│         │                │                 │             │
│  ┌──────▼──────┐  ┌──────▼──────┐  ┌───────▼─────┐      │
│  │  SH1106     │  │  MicroSD    │  │   ESP8266   │      │
│  │  OLED       │  │  Card       │  │   Wi-Fi     │      │
│  │  Display    │  │  Logger     │  │   Module    │      │
│  └─────────────┘  └─────────────┘  └──────┬──────┘      │
│                                           │              │
└───────────────────────────────────────────┼──────────────┘
                                            │
                                   ┌────────▼────────┐
                                   │  Cloud API /    │
                                   │  Web Dashboard  │
                                   └─────────────────┘
```

---

## Hardware Components

| Component | Model | Interface | Purpose |
|-----------|-------|-----------|---------|
| MCU | NXP FRDM-K64F | — | ARM Cortex-M4 @ 120 MHz, 256 KB RAM |
| PM Sensor | PMS5003 | UART (9600) | PM1.0, PM2.5, PM10 particulate matter |
| AQ Sensor | ENS160 | I2C (0x53) | TVOC (ppb) and eCO2 (ppm) |
| Env Sensor | BME680 | I2C (0x76) | Temperature, humidity, pressure, gas resistance |
| Gas Sensor | Flying Fish MQ | Analog + Digital | Combustible gas detection |
| Display | SH1106 1.3" | I2C (0x3C) | 128×64 OLED dashboard |
| Wi-Fi | ESP8266 / ESP32 | UART (115200) | AT-command Wi-Fi with SoftAP |
| Storage | MicroSD Card | SPI | Daily CSV data logging (up to 30 GB) |
| Alarm | Piezo Buzzer | Digital Out | Audible AQI/fire alert |

---

## Pin Mapping

```
FRDM-K64F Pin Assignments
─────────────────────────
UART3 (ESP8266/ESP32 Wi-Fi):
  TX → PTC17        RX → PTC16        Baud: 115200

UART1 (PMS5003 PM Sensor):
  TX → PTC4         RX → PTC3         Baud: 9600

I2C0 (ENS160 + BME680 + SH1106 OLED):
  SDA → PTE25 (D14)    SCL → PTE24 (D15)

SPI0 (MicroSD Card):
  MOSI → PTD2       MISO → PTD3
  SCK  → PTD1       CS   → PTD0

Analog (MQ Gas Sensor):
  AO → PTB2 (A0)    DO → PTB3 (A1)

GPIO:
  Buzzer  → PTC12 (D8)
  OLED Button → PTC2 (D6) with internal PullUp
  On-board LEDs → LED1 (PTB22), LED2 (PTB21), LED3 (PTE26)
```

---

## Software Modules

### `main.cpp` — Core Firmware (3,478 lines)
The main application loop: sensor polling, AQI computation, alarm management, OLED rendering, Wi-Fi communication, SoftAP captive portal, SD card logging, and orchestration of all ember/ modules.

### `ember/ember_rl_inference` — DQN Reinforcement Learning Engine
Runs a pre-trained Deep Q-Network on the MCU. Takes a 10-dimensional observation vector (normalized sensor readings) and outputs Q-values for SAFE vs. DANGER actions. Includes optional calibration bias correction.

### `ember/ember_rl_policy.h` — Exported DQN Weights
Contains the trained neural network weights (3 layers: 10→64→64→2) exported from Python training via `export_policy.py`. Auto-generated file.

### `ember/ember_rl_training` — On-Device Training Data Collection
Collects 16-feature training vectors from live sensor data with manual SW2/SW3 button labeling (SAFE/DANGER). Writes labeled CSV files to SD card for offline RL training.

### `ember/ember_fire_detection` — Fire & Smoke Detection
Multi-sensor weighted rate-of-change algorithm. Computes a fire confidence score from PM2.5 spikes, MQ gas surges, and temperature rises against a rolling baseline. Features debounce (3 consecutive cycles), cooldown (60s), and absolute PM2.5 override (≥150 µg/m³ → +0.20 boost).

### `ember/ember_calibration` — Sensor Calibration
Automated 60-second baseline calibration routine. Samples all sensors 24 times, computes per-feature mean/min/max, and persists calibration parameters to SD card (`/sd/calibration.csv`). Applied as bias correction during RL inference.

### `ember/ember_alert_log` — Alert History & Event Replay
Ring buffer storing the last 20 alert events with timestamps, AQI scores, and Q-values. Supports serial commands for forensic replay and statistical summaries.

---

## Features

### Composite AQI Calculation
Converts raw sensor values to EPA-standard AQI breakpoints for PM2.5, PM10, and gas readings. The highest sub-index becomes the reported composite AQI.

### RL-Based Alarm Decision
Instead of simple threshold alarms, a trained DQN evaluates the full sensor context and decides SAFE (action 0) or DANGER (action 1). This reduces false positives from single-sensor spikes.

### Fire/Smoke Detection
A dedicated fire detection pipeline runs in parallel with AQI monitoring:
- **PM2.5 weight**: 0.55 — primary indicator of smoke particulates
- **MQ gas weight**: 0.25 — combustible gas detection
- **Temperature weight**: 0.20 — thermal anomaly detection
- **Fire threshold**: 0.50 composite score triggers alert
- **Absolute override**: PM2.5 ≥ 150 µg/m³ adds +0.20 boost regardless of rate

### SoftAP Wi-Fi Provisioning
On first boot (or when no credentials are saved), the ESP module starts a SoftAP access point with a captive HTML portal:
1. User connects to the AP and enters a PIN
2. Portal shows scanned Wi-Fi networks
3. User selects network and enters password
4. Credentials are saved to SD card for automatic reconnection

### OLED Dashboard (4 Pages)
| Page | Content |
|------|---------|
| 0 | AQI score + PM2.5 / PM10 readings |
| 1 | AQI score + Temperature / Humidity |
| 2 | AQI score + MQ Gas voltage / TVOC |
| 3 | AQI score + Pressure / eCO2 |

Press the button on D6 to cycle pages. During FIRE or DANGER alerts, the display overrides with flashing warning screens.

### SD Card Data Logging
- Daily CSV files: `/sd/data/YYYY-MM-DD.csv`
- Columns: timestamp, PM1.0, PM2.5, PM10, TVOC, eCO2, temp, humidity, pressure, gas resistance, MQ analog, MQ digital, AQI, alarm state
- Automatic oldest-file deletion when storage exceeds 30 GB
- Fire events logged separately with full baseline snapshots

---

## Build & Flash

### Prerequisites
- [Mbed CLI](https://os.mbed.com/docs/mbed-os/latest/build-tools/mbed-cli-1.html) with GCC ARM toolchain
- FRDM-K64F board connected via USB

### Compile
```bash
mbed compile -m K64F -t GCC_ARM
```

### Flash
```bash
cp BUILD/K64F/GCC_ARM/ember_project.bin /Volumes/DAPLINK/
```
Or drag-and-drop the `.bin` file to the DAPLINK USB drive.

### Serial Monitor
```bash
screen /dev/tty.usbmodem* 115200
```

---

## Repository Structure

```
ember-air-quality-monitor/
├── main.cpp                 # Core firmware (sensors, WiFi, OLED, SD, alarms)
├── mbed_app.json            # Mbed OS target configuration
├── .mbedignore              # Exclude unused Mbed OS components
├── ember/
│   ├── ember_rl_inference.cpp/.h    # DQN forward pass & AQI computation
│   ├── ember_rl_policy.h            # Exported neural network weights
│   ├── ember_rl_training.cpp/.h     # Training data collection
│   ├── ember_fire_detection.cpp/.h  # Fire & smoke detection algorithm
│   ├── ember_calibration.cpp/.h     # Sensor calibration & persistence
│   └── ember_alert_log.cpp/.h       # Alert ring buffer & serial replay
├── dashboard.html           # Web-based monitoring dashboard
├── test_data.sh             # Sensor data test scripts
├── report/                  # Project documentation & reports
├── SYSTEM_DOCUMENTATION.md  # Detailed system design document
├── PMS5003_PM25_SENSOR_GUIDE.md  # PMS5003 sensor reference
└── mbed-os/                 # Mbed OS 6 RTOS (submodule)
```

---

## Configuration

### `mbed_app.json`
```json
{
    "target_overrides": {
        "*": {
            "platform.minimal-printf-enable-floating-point": true,
            "rtos.main-thread-stack-size": 8192
        },
        "K64F": {
            "sd.SPI_MOSI": "PTD2",
            "sd.SPI_MISO": "PTD3",
            "sd.SPI_CLK": "PTD1",
            "sd.SPI_CS": "PTD0"
        }
    }
}
```

### Key Compile-Time Parameters

| Parameter | File | Default | Description |
|-----------|------|---------|-------------|
| `FIRE_THRESHOLD` | `ember_fire_detection.cpp` | 0.50 | Fire confidence score to trigger alert |
| `W_PM / W_MQ / W_TEMP` | `ember_fire_detection.cpp` | 0.55 / 0.25 / 0.20 | Sensor fusion weights |
| `BASELINE_WINDOW` | `ember_fire_detection.cpp` | 15 | Rolling baseline history (cycles) |
| `CALIB_SAMPLE_COUNT` | `ember_calibration.cpp` | 24 | Calibration samples to collect |
| `MAX_ALERT_ENTRIES` | `ember_alert_log.cpp` | 20 | Alert ring buffer capacity |
| `ALARM_COOLDOWN_SEC` | `main.cpp` | 30 | Seconds after manual alarm dismiss |

---

## Serial Commands

Connect at **115200 baud** and type:

| Command | Description |
|---------|-------------|
| `REPLAY` | Print the last 20 alert events with timestamps and Q-values |
| `STATS` | Show alert statistics: count, average AQI, peak AQI, Q-value ranges |
| `CALIB` | Trigger a 60-second sensor calibration routine |

---

## GenAI Declaration

This project used **Claude AI (Anthropic)** and **GitHub Copilot** as coding assistants during development. Every source file contains an inline `GenAI Declaration` block at the top specifying which line ranges received AI assistance and to what extent.

All AI-generated and AI-assisted code was reviewed, tested, and validated on physical FRDM-K64F hardware by the development team. AI tools were used primarily for:
- I2C/SPI/UART driver boilerplate and register configurations
- AT-command sequence scaffolding
- Linear algebra and AQI breakpoint lookup tables
- State machine scaffolding and file I/O patterns
- HTML/CSS generation for the SoftAP captive portal

The overall system architecture, sensor fusion strategy, RL observation design, fire detection algorithm, and hardware integration were designed by the team.

---

## Team

**Ember** — SEP600 Capstone, Seneca Polytechnic

- **Mirac Ozcan** — MCU Firmware (FRDM-K64F, sensors, RL inference, fire detection, SD logging, Wi-Fi)
- **Rajini** — Cloud API, AI/ML Training Pipeline, Web Dashboard

---

## License

This project was developed as part of the SEP600 capstone course at Seneca Polytechnic.
