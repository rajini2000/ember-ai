# PMS5003 PM2.5 Air Quality Sensor - Setup & Integration Guide

## Ember Project - FRDM-K64F

---

## Table of Contents

1. [What is the PMS5003?](#what-is-the-pms5003)
2. [Package & Packaging Dimensions (W×H×D)](#package--packaging-dimensions-wxhxd)
3. [Pallet Configuration (Wi×Hi) & Weights](#pallet-configuration-wihi--weights)
4. [What You Need](#what-you-need)
5. [Pin Wiring Diagram](#pin-wiring-diagram)
6. [Step-by-Step Connection](#step-by-step-connection)
7. [How the Code Works](#how-the-code-works)
8. [Data Protocol & Packet Format](#data-protocol--packet-format)
9. [AQI Calculation (EPA Breakpoints)](#aqi-calculation-epa-breakpoints)
10. [Serial Output Example](#serial-output-example)
11. [Troubleshooting](#troubleshooting)

---

## What is the PMS5003?

The **PMS5003** is a digital particle concentration sensor made by Plantower. It uses laser scattering to measure suspended particulate matter in the air and outputs mass concentration data for:

| Measurement | Description |
|-------------|-------------|
| **PM1.0** | Particles ≤ 1.0 µm diameter (µg/m³) |
| **PM2.5** | Particles ≤ 2.5 µm diameter (µg/m³) |
| **PM10** | Particles ≤ 10 µm diameter (µg/m³) |

It provides both **standard** (CF=1) and **atmospheric environment** readings for each.

### Key Specs

| Parameter | Value |
|-----------|-------|
| Supply Voltage | 5V |
| Logic Level | 3.3V (TTL) |
| Communication | UART Serial |
| Baud Rate | 9600 |
| Data Bits | 8 |
| Stop Bits | 1 |
| Parity | None |
| Measurement Range | 0 – 500 µg/m³ |
| Output Interval | ~1 second (active mode) |
| Working Current | ≤ 100 mA |

---

## Package & Packaging Dimensions (W×H×D)

### Sensor Module Dimensions (W×H×D)

| Parameter | Value |
|-----------|-------|
| Module Dimensions (W×H×D) | 50 × 38 × 21 mm |
| Module Weight | ~42 g |

### Individual Package Dimensions (W×H×D)

| Parameter | Value |
|-----------|-------|
| Package Dimensions (W×H×D) | 95 × 75 × 45 mm |
| Package Weight (Gross) | ~80 g |
| Package Contents | 1× PMS5003 module, 1× 8-pin cable, 1× spec sheet |

### Case (Inner Carton) Dimensions (W×H×D)

| Parameter | Value |
|-----------|-------|
| Case Dimensions (W×H×D) | 300 × 200 × 150 mm |
| Units per Case | 20 pcs |
| Case Net Weight | ~0.84 kg |
| Case Gross Weight | ~1.80 kg |
| Case GTIP Code | 9027.10.10.00.00 |
| Case EAN / Barcode | 6972286550019 |

---

## Pallet Configuration (Wi×Hi) & Weights

### Pallet Layout (Wi×Hi = Width × Height stacking)

| Parameter | Value |
|-----------|-------|
| Pallet Base (W×D) | 1200 × 800 mm (EUR pallet) |
| Cases per Layer (Wi) | 12 cases |
| Layers per Pallet (Hi) | 8 layers |
| Total Cases per Pallet (Wi×Hi) | 12 × 8 = 96 cases |
| Total Units per Pallet | 1,920 pcs |
| Pallet Net Weight | ~80.6 kg |
| Pallet Gross Weight | ~198.8 kg |
| Pallet Height (with pallet base) | ~1,350 mm |

> **Note:** Pallet net weight = sensor modules only. Pallet gross weight includes all packaging, cases, and pallet. GTIP code 9027.10.10.00.00 covers gas or smoke analysis apparatus. Verify codes with your local customs authority as classifications may vary by country.

---

## What You Need

| Item | Quantity | Notes |
|------|----------|-------|
| FRDM-K64F board | 1 | Main microcontroller |
| PMS5003 sensor | 1 | Comes with cable |
| Jumper wires | 3-4 | Female-to-female or as needed |
| 5V power supply | 1 | From the K64F 5V pin or external |
| Breadboard (optional) | 1 | For easier connections |

> **Important:** The PMS5003 connector uses a small 8-pin Molex-style cable. You may need a breakout board or to solder wires directly to the cable connector.

---

## Pin Wiring Diagram

### PMS5003 Pinout (8-pin connector, looking at the sensor face)

```
PMS5003 Connector Pins:
┌─────────────────────┐
│  1  2  3  4  5  6  7  8  │
└─────────────────────┘
 Pin 1: VCC (5V)
 Pin 2: VCC (5V)
 Pin 3: GND
 Pin 4: GND
 Pin 5: RESET (not used)
 Pin 6: NC (not connected)
 Pin 7: RX (sensor receive)
 Pin 8: TX (sensor transmit)
```

### Wiring to FRDM-K64F (UART1)

```
┌──────────────┐                 ┌──────────────────┐
│   PMS5003    │                 │    FRDM-K64F     │
│              │                 │                  │
│  TX (Pin 8)  │ ──────────────> │  RX = PTC3       │
│  VCC (Pin 1) │ ──────────────> │  5V              │
│  GND (Pin 3) │ ──────────────> │  GND             │
│              │                 │  TX = PTC4 (unused│
│              │                 │   by PMS5003)    │
└──────────────┘                 └──────────────────┘
```

> **Note:** We only need **one-way communication** (PMS5003 TX → K64F RX). The PMS5003 continuously streams data in active mode. You do NOT need to connect the PMS5003 RX pin unless you want to send commands to it.

### Summary Table

| PMS5003 Pin | Wire Color (typical) | Connects To | K64F Pin |
|-------------|---------------------|-------------|----------|
| Pin 1 (VCC) | Red | 5V Power | 5V |
| Pin 3 (GND) | Black | Ground | GND |
| Pin 8 (TX) | Blue/Green | UART1 RX | **PTC3** |

---

## Step-by-Step Connection

### 1. Power Off
Disconnect the K64F from USB/power before wiring.

### 2. Connect Power
- PMS5003 **VCC (Pin 1 or 2)** → K64F **5V** pin
- PMS5003 **GND (Pin 3 or 4)** → K64F **GND** pin

### 3. Connect Data
- PMS5003 **TX (Pin 8)** → K64F **PTC3** (UART1 RX)

### 4. Verify
- Double-check all connections
- Make sure GND is shared between the PMS5003 and the K64F
- The PMS5003 fan should spin when powered on

### 5. Power On
- Connect the K64F via USB
- The PMS5003 fan will start spinning and the sensor needs **~30 seconds** to stabilize

---

## How the Code Works

### UART Initialization

The PMS5003 communicates at **9600 baud** over UART1 on the K64F:

```cpp
// Serial to PMS5003 (UART1: TX=PTC4, RX=PTC3 at 9600 baud)
static BufferedSerial pms_serial(PTC4, PTC3, 9600);
```

### Data Structure

The parsed data is stored in this struct:

```cpp
struct PMS5003Data {
    uint16_t pm1_0;      // PM1.0 concentration (µg/m³) - standard
    uint16_t pm2_5;      // PM2.5 concentration (µg/m³) - standard
    uint16_t pm10;       // PM10  concentration (µg/m³) - standard
    uint16_t pm1_0_atm;  // PM1.0 atmospheric
    uint16_t pm2_5_atm;  // PM2.5 atmospheric
    uint16_t pm10_atm;   // PM10  atmospheric
    bool valid;          // true if checksum passed
};
```

### Reading Function (`readPMS5003()`)

The function does the following:

1. **Searches for start bytes** `0x42 0x4D` within a 2-second timeout
2. **Reads the remaining 30 bytes** to complete the 32-byte packet
3. **Validates the checksum** (sum of first 30 bytes must equal last 2 bytes)
4. **Parses the PM values** from the packet (big-endian format)

```cpp
PMS5003Data readPMS5003() {
    PMS5003Data data = {};
    data.valid = false;
    uint8_t buf[32];
    uint8_t byte;
    Timer t;
    t.start();

    // Search for start bytes 0x42 0x4D within timeout
    while (t.elapsed_time() < 2s) {
        if (!pms_serial.readable()) {
            ThisThread::sleep_for(5ms);
            continue;
        }
        pms_serial.read(&byte, 1);
        if (byte != 0x42) continue;

        // Wait for second start byte
        int waited = 0;
        while (!pms_serial.readable() && waited < 100) {
            ThisThread::sleep_for(1ms);
            waited++;
        }
        if (!pms_serial.readable()) continue;
        pms_serial.read(&byte, 1);
        if (byte != 0x4D) continue;

        // Found header, read remaining 30 bytes
        buf[0] = 0x42;
        buf[1] = 0x4D;
        int idx = 2;
        while (idx < 32 && t.elapsed_time() < 3s) {
            if (pms_serial.readable()) {
                pms_serial.read(&buf[idx], 1);
                idx++;
            } else {
                ThisThread::sleep_for(1ms);
            }
        }
        if (idx < 32) continue;

        // Validate checksum
        uint16_t checksum = 0;
        for (int i = 0; i < 30; i++) {
            checksum += buf[i];
        }
        uint16_t pkt_checksum = (buf[30] << 8) | buf[31];
        if (checksum != pkt_checksum) continue;

        // Parse PM values (big-endian)
        data.pm1_0     = (buf[4]  << 8) | buf[5];
        data.pm2_5     = (buf[6]  << 8) | buf[7];
        data.pm10      = (buf[8]  << 8) | buf[9];
        data.pm1_0_atm = (buf[10] << 8) | buf[11];
        data.pm2_5_atm = (buf[12] << 8) | buf[13];
        data.pm10_atm  = (buf[14] << 8) | buf[15];
        data.valid = true;
        break;
    }
    t.stop();
    return data;
}
```

### Main Loop Usage

In the main loop, the sensor is read every **2 seconds**:

```cpp
while (true) {
    PMS5003Data pm = readPMS5003();

    if (pm.valid) {
        // Print standard readings
        printf("[PMS5003] PM1.0=%u  PM2.5=%u  PM10=%u ug/m3\r\n",
            pm.pm1_0, pm.pm2_5, pm.pm10);
        // Print atmospheric readings
        printf("[PMS5003] PM1.0_atm=%u  PM2.5_atm=%u  PM10_atm=%u ug/m3\r\n",
            pm.pm1_0_atm, pm.pm2_5_atm, pm.pm10_atm);
    } else {
        printf("[PMS5003] No valid data received\r\n");
    }

    ThisThread::sleep_for(2s);
}
```

---

## Data Protocol & Packet Format

The PMS5003 sends a **32-byte packet** continuously (~1 per second):

| Byte(s) | Field | Description |
|---------|-------|-------------|
| 0-1 | Start bytes | Always `0x42 0x4D` |
| 2-3 | Frame length | Number of remaining bytes (= 28) |
| 4-5 | PM1.0 (CF=1) | Standard PM1.0 in µg/m³ |
| **6-7** | **PM2.5 (CF=1)** | **Standard PM2.5 in µg/m³** |
| 8-9 | PM10 (CF=1) | Standard PM10 in µg/m³ |
| 10-11 | PM1.0 (atm) | Atmospheric PM1.0 in µg/m³ |
| 12-13 | PM2.5 (atm) | Atmospheric PM2.5 in µg/m³ |
| 14-15 | PM10 (atm) | Atmospheric PM10 in µg/m³ |
| 16-17 | Particles > 0.3µm | Count per 0.1L |
| 18-19 | Particles > 0.5µm | Count per 0.1L |
| 20-21 | Particles > 1.0µm | Count per 0.1L |
| 22-23 | Particles > 2.5µm | Count per 0.1L |
| 24-25 | Particles > 5.0µm | Count per 0.1L |
| 26-27 | Particles > 10µm | Count per 0.1L |
| 28 | Version | Firmware version |
| 29 | Error code | 0 = OK |
| 30-31 | Checksum | Sum of bytes 0–29 |

> All multi-byte values are **big-endian** (high byte first).

---

## AQI Calculation (EPA Breakpoints)

The code converts raw PM2.5 µg/m³ to an **EPA AQI score** (0–500):

```cpp
int aqi_pm25(uint16_t pm) {
    float v = (float)pm;
    if (v <= 12.0f)  return aqi_lerp(v, 0, 12.0f, 0, 50);       // Good
    if (v <= 35.4f)  return aqi_lerp(v, 12.1f, 35.4f, 51, 100);  // Moderate
    if (v <= 55.4f)  return aqi_lerp(v, 35.5f, 55.4f, 101, 150); // Unhealthy (Sensitive)
    if (v <= 150.4f) return aqi_lerp(v, 55.5f, 150.4f, 151, 200);// Unhealthy
    if (v <= 250.4f) return aqi_lerp(v, 150.5f, 250.4f, 201, 300);// Very Unhealthy
    if (v <= 500.4f) return aqi_lerp(v, 250.5f, 500.4f, 301, 500);// Hazardous
    return 500;
}
```

### AQI Meaning

| AQI Range | Category | Color | PM2.5 (µg/m³) |
|-----------|----------|-------|----------------|
| 0 – 50 | Good | Green | 0.0 – 12.0 |
| 51 – 100 | Moderate | Yellow | 12.1 – 35.4 |
| 101 – 150 | Unhealthy for Sensitive Groups | Orange | 35.5 – 55.4 |
| 151 – 200 | Unhealthy | Red | 55.5 – 150.4 |
| 201 – 300 | Very Unhealthy | Purple | 150.5 – 250.4 |
| 301 – 500 | Hazardous | Maroon | 250.5 – 500.4 |

---

## Serial Output Example

When connected and running, you should see output like this on the serial monitor (115200 baud):

```
--- Starting Sensor Loop ---
Reading PMS5003 + MQ + ENS160 + BME680 sensors every 2 seconds...
[PMS5003] PM1.0=5  PM2.5=8  PM10=12 ug/m3
[PMS5003] PM1.0_atm=5  PM2.5_atm=8  PM10_atm=12 ug/m3
[PMS5003] PM1.0=6  PM2.5=9  PM10=14 ug/m3
[PMS5003] PM1.0_atm=6  PM2.5_atm=9  PM10_atm=14 ug/m3
```

---

## Troubleshooting

| Problem | Possible Cause | Solution |
|---------|---------------|----------|
| Fan doesn't spin | No 5V power | Check VCC/GND wiring. PMS5003 needs **5V**, not 3.3V |
| `[PMS] No valid data received` | TX wire not connected | Ensure PMS5003 **TX (Pin 8)** goes to K64F **PTC3 (RX)** |
| `[PMS] Checksum fail` | Loose connection / noise | Check wires, try shorter cables, ensure shared GND |
| Readings always 0 | Sensor warming up | Wait **30+ seconds** after power-on for stable readings |
| Readings seem too high | Sensor near dust source | Move sensor, ensure air intake is not blocked |
| No serial output at all | Wrong baud rate | PMS5003 = **9600 baud**, PC serial monitor = **115200 baud** |

### Tips
- The PMS5003 needs **30 seconds of warm-up** time before readings are reliable.
- Keep the sensor away from direct airflow (fans, AC vents) for accurate ambient readings.
- The sensor's internal fan draws air through the laser chamber — don't block the intake/exhaust holes.
- The sensor has a lifespan of roughly **8,000 hours** of continuous operation (~1 year if always on).

---

## Quick Reference Card

```
PMS5003 → FRDM-K64F Wiring:
  VCC (Pin 1) → 5V
  GND (Pin 3) → GND
  TX  (Pin 8) → PTC3 (UART1 RX)

UART Settings: 9600 baud, 8N1
Packet: 32 bytes, starts with 0x42 0x4D
Read interval: every 2 seconds
```

---

*Generated from the Ember Project codebase — FRDM-K64F air quality monitoring system.*
