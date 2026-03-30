# Ember Project — Air Quality Monitoring System Documentation

> **For: Reinforcement Learning AI Integration**
> **Hardware: FRDM-K64F (ARM Cortex-M4) + Multi-Sensor Array**
> **Last Updated: February 2026**

---

## 1. System Overview

An embedded IoT air quality monitoring station built on the **NXP FRDM-K64F** (ARM Cortex-M4, Mbed OS 6). It reads from 4 sensor types every ~2 seconds, computes a composite AQI score (0–500), controls an alarm output, logs all data to a MicroSD card as CSV, and connects to WiFi via an ESP32 module.

### Architecture Diagram

```
┌──────────────────────────────────────────────────────────┐
│                     FRDM-K64F (MCU)                      │
│                                                          │
│  UART3 ──── ESP32-WROOM-32 ──── WiFi (NTP time sync)    │
│  UART1 ──── PMS5003 (PM2.5/PM10 particulate sensor)     │
│  ADC   ──── MQ Gas Sensor (analog gas concentration)     │
│  I2C   ──── BME680 (temp/humidity/pressure/gas)          │
│  I2C   ──── ENS160 (TVOC/eCO2) [currently disconnected] │
│  SPI   ──── MicroSD Card (data logging, CSV)             │
│  GPIO  ──── Alarm Output (PTA2, active HIGH)             │
│  GPIO  ──── Onboard RGB LED (alarm + status indicator)   │
└──────────────────────────────────────────────────────────┘
```

---

## 2. Sensors & Data

### 2.1 PMS5003 — Particulate Matter Sensor

| Property         | Value                    |
|------------------|--------------------------|
| Interface        | UART1, 9600 baud         |
| Pins             | TX→PTC3 (RX), PTC4 (TX) |
| Power            | 5V                       |
| Protocol         | 32-byte binary packets, 0x42 0x4D header |
| Checksum         | Sum of first 30 bytes == bytes 30-31 |
| Read interval    | ~2 seconds               |

**Output fields:**

| Field        | Type     | Unit   | Description                     | Typical Clean Air | Vape Smoke  |
|-------------|----------|--------|---------------------------------|-------------------|-------------|
| `pm1_0`     | uint16   | µg/m³  | PM1.0 standard                  | 5–10              | 200–800+    |
| `pm2_5`     | uint16   | µg/m³  | PM2.5 standard (**primary**)    | 10–20             | 400–1000+   |
| `pm10`      | uint16   | µg/m³  | PM10 standard                   | 15–25             | 500–1000+   |
| `pm1_0_atm` | uint16   | µg/m³  | PM1.0 atmospheric               | ~same             | ~same       |
| `pm2_5_atm` | uint16   | µg/m³  | PM2.5 atmospheric               | ~same             | ~same       |
| `pm10_atm`  | uint16   | µg/m³  | PM10 atmospheric                | ~same             | ~same       |
| `pms_valid`  | bool    | —      | Was the packet valid this cycle? | true              | true        |

### 2.2 MQ Gas Sensor (Flying Fish Module)

| Property      | Value                     |
|---------------|---------------------------|
| Interface     | Analog (AO) + Digital (DO)|
| Pins          | AO→PTB2, DO→PTB3          |
| Power         | 5V                        |
| ADC range     | 0.0–1.0 (maps to 0–3.3V) |
| Sensitivity   | Combustible gases, smoke, alcohol, VOCs |

**Output fields:**

| Field         | Type   | Range       | Description                          | Clean Air | Vape Smoke |
|--------------|--------|-------------|--------------------------------------|-----------|------------|
| `mq_analog`  | float  | 0.0–1.0     | Raw ADC reading (multiply by 3.3 for voltage) | ~0.03 (0.1V) | 0.3–0.9 (1.0–3.0V) |
| `mq_digital` | int    | 0 or 1      | Digital threshold pin (module potentiometer) | 1         | 0          |

**Voltage = mq_analog × 3.3V**

### 2.3 BME680 — Environmental Sensor (Bosch)

| Property    | Value                           |
|-------------|----------------------------------|
| Interface   | I2C, 100 kHz                     |
| Address     | 0x76 (7-bit)                     |
| Pins        | SDA=PTE25, SCL=PTE24             |
| Power       | 3.3V                             |
| Mode        | Forced mode (trigger → read)     |

**Output fields:**

| Field         | Type   | Unit    | Description                    | Typical Indoor |
|--------------|--------|---------|--------------------------------|----------------|
| `temperature`| float  | °C      | Compensated temperature        | 20–30          |
| `humidity`   | float  | %RH     | Compensated relative humidity  | 15–60          |
| `pressure`   | float  | hPa     | Atmospheric pressure           | 980–1020       |
| `gas_res`    | float  | Ohms    | Gas resistance (higher=cleaner)| 5M–20M clean   |

**Calibration**: Uses full Bosch compensation formulas with factory calibration data read from NVM. Temperature, pressure, and humidity are interdependent (each compensation uses previous results).

**Gas resistance** is an important indirect air quality metric — lower resistance = more VOCs/contamination. In clean air it reads 8M–20M Ohms. With vape smoke it can drop to 50K–500K Ohms.

### 2.4 ENS160 — Air Quality Sensor (ScioSense)

| Property    | Value                           |
|-------------|----------------------------------|
| Interface   | I2C, 100 kHz                     |
| Address     | 0x52 (7-bit)                     |
| Pins        | Shared bus: SDA=PTE25, SCL=PTE24 |
| Power       | 3.3V (via VIN with onboard reg)  |
| **Status**  | **CURRENTLY NOT RESPONDING** — possibly damaged. Code skips it gracefully. |

**Output fields (when working):**

| Field   | Type     | Unit | Description                       | Good   | Bad    |
|---------|----------|------|-----------------------------------|--------|--------|
| `tvoc`  | uint16   | ppb  | Total Volatile Organic Compounds  | 0–65   | >660   |
| `eco2`  | uint16   | ppm  | Equivalent CO2                    | 400–600| >1500  |
| `aqi`   | int      | 1–5  | Onboard AQI (1=excellent, 5=bad)  | 1–2    | 4–5    |

---

## 3. Data Logging Format

### 3.1 Sensor Data CSV

**Path**: `/sd/data/YYYY-MM-DD.csv` (one file per day)
**Write interval**: Every ~2 seconds
**Max storage**: 30 GB (oldest files deleted when exceeded)

**CSV Header:**
```
timestamp,PM1.0,PM2.5,PM10,TVOC,eCO2,temperature,humidity,pressure,gas,MQ_analog,MQ_digital
```

**Example row:**
```
2026-02-22 14:30:15,8,15,18,29,411,27.3,18.2,990.9,14523678.0,0.031,1
```

| Column       | Type     | Description                               |
|-------------|----------|-------------------------------------------|
| timestamp   | string   | `YYYY-MM-DD HH:MM:SS` (NTP-synced UTC+local) |
| PM1.0       | uint16   | µg/m³                                     |
| PM2.5       | uint16   | µg/m³                                     |
| PM10        | uint16   | µg/m³                                     |
| TVOC        | uint16   | ppb (0 if ENS160 unavailable)             |
| eCO2        | uint16   | ppm (0 if ENS160 unavailable)             |
| temperature | float    | °C (0.0 if BME680 unavailable)            |
| humidity    | float    | %RH                                       |
| pressure    | float    | hPa                                       |
| gas         | float    | Ohms (BME680 gas resistance)              |
| MQ_analog   | float    | 0.0–1.0 raw ADC (×3.3 = Volts)           |
| MQ_digital  | int      | 0 or 1                                    |

### 3.2 Alarm Event CSV

**Path**: `/sd/data/alarms_YYYY-MM-DD.csv`

**CSV Header:**
```
timestamp,event,PM2.5,PM10,TVOC,eCO2,Temp,Hum,MQ_V
```

**Example rows:**
```
2026-02-22 14:31:05,ALARM_ON AQI=287 VERY_UNHEALTHY,709,812,0,0,27.5,18.1,1.45
2026-02-22 14:32:30,ALL_CLEAR,18,22,0,0,27.4,18.3,0.12
```

---

## 4. Current AQI Algorithm (Rule-Based — To Be Replaced by RL)

### 4.1 Sub-Score Calculation

Each pollution sensor maps to a 0–500 sub-score. Only **pollution sensors** are used for the composite AQI (temperature/humidity are informational only).

#### PM2.5 Sub-Score (EPA Breakpoints)

| PM2.5 (µg/m³)    | AQI Score | Category                      |
|-------------------|-----------|-------------------------------|
| 0.0 – 12.0       | 0 – 50    | Good                          |
| 12.1 – 35.4      | 51 – 100  | Moderate                      |
| 35.5 – 55.4      | 101 – 150 | Unhealthy for Sensitive Groups|
| 55.5 – 150.4     | 151 – 200 | Unhealthy                     |
| 150.5 – 250.4    | 201 – 300 | Very Unhealthy                |
| 250.5 – 500.4    | 301 – 500 | Hazardous                     |

#### PM10 Sub-Score (EPA Breakpoints)

| PM10 (µg/m³)     | AQI Score | Category                      |
|-------------------|-----------|-------------------------------|
| 0 – 54           | 0 – 50    | Good                          |
| 55 – 154         | 51 – 100  | Moderate                      |
| 155 – 254        | 101 – 150 | Unhealthy for Sensitive Groups|
| 255 – 354        | 151 – 200 | Unhealthy                     |
| 355 – 424        | 201 – 300 | Very Unhealthy                |
| 425 – 604        | 301 – 500 | Hazardous                     |

#### MQ Gas Sub-Score (Voltage-Based)

| Voltage (V)    | AQI Score | Category       |
|----------------|-----------|----------------|
| 0 – 0.3        | 0         | Clean air      |
| 0.3 – 0.6      | 50        | Slight         |
| 0.6 – 1.0      | 100       | Moderate       |
| 1.0 – 1.5      | 150       | Unhealthy      |
| 1.5 – 2.0      | 200       | Very Unhealthy |
| 2.0 – 2.5      | 300       | Hazardous      |
| > 2.5          | 500       | Emergency      |

#### TVOC Sub-Score (IAQ Guidelines, when ENS160 available)

| TVOC (ppb)       | AQI Score | Category                      |
|-------------------|-----------|-------------------------------|
| 0 – 65           | 0 – 50    | Good                          |
| 65 – 220         | 51 – 100  | Moderate                      |
| 220 – 660        | 101 – 150 | Unhealthy for Sensitive Groups|
| 660 – 2200       | 151 – 200 | Unhealthy                     |
| 2200 – 5500      | 201 – 300 | Very Unhealthy                |
| 5500 – 10000     | 301 – 500 | Hazardous                     |

#### eCO2 Sub-Score (IAQ Guidelines, when ENS160 available)

| eCO2 (ppm)        | AQI Score | Category                      |
|-------------------|-----------|-------------------------------|
| ≤ 400             | 0         | Outdoor baseline              |
| 400 – 600         | 0 – 50    | Good                          |
| 600 – 1000        | 51 – 100  | Moderate                      |
| 1000 – 1500       | 101 – 150 | Unhealthy for Sensitive Groups|
| 1500 – 2500       | 151 – 200 | Unhealthy                     |
| 2500 – 5000       | 201 – 300 | Very Unhealthy                |
| 5000 – 10000      | 301 – 500 | Hazardous                     |

### 4.2 Composite Score

```
composite = max(PM25_score, PM10_score, TVOC_score, eCO2_score, MQ_score)

if (count of scores > 100) >= 2:
    composite *= 1.20   # Multi-sensor correlation bonus
```

### 4.3 Alarm Logic

```
TRIGGER threshold: composite >= 151 (UNHEALTHY)
CLEAR threshold:   composite <  100 (MODERATE/GOOD)
Debounce:          2 consecutive cycles (~4 seconds)
Hysteresis band:   100–150 (maintains current alarm state)

To TRIGGER alarm:  Score >= 151 for 2 consecutive cycles
To CLEAR alarm:    Score < 100 for 2 consecutive cycles
```

### 4.4 Known Limitations of Current Algorithm

- **Static thresholds**: Does not adapt to environment, time of day, or seasonal changes
- **No learning**: Identical response regardless of history or pollution source
- **MQ sensor noise**: The MQ sensor drifts with temperature and humidity — no compensation
- **PMS5003 lag**: Particulate sensor has ~10 second response/decay time for aerosols
- **Binary alarm**: Only ON/OFF, no graduated response or severity persistence
- **No prediction**: Cannot anticipate deterioration trends
- **BME680 gas resistance not used in AQI**: Currently logged but not scored (it's an excellent indirect VOC indicator)

---

## 5. Timing & Loop Behavior

```
Boot sequence:
  1. ESP32 AT init (3s wait)
  2. WiFi connect to "Mirac" (up to 15s)
  3. NTP time sync via ESP32
  4. SD card init + mount
  5. I2C bus scan (detect available sensors)

Main loop (repeats every ~2 seconds):
  1. Read PMS5003 (UART, up to 3s timeout)
  2. Read MQ sensor (instant ADC read)
  3. Read BME680 (I2C, forced mode, ~100ms)
  4. Read ENS160 (I2C, if available)
  5. checkAirQuality() — compute AQI, control alarm
  6. saveToSD() — append CSV row
  7. Sleep 2 seconds
```

**Effective sample rate**: One complete reading every ~2.5 seconds (2s sleep + sensor read overhead).

---

## 6. Hardware Specifications

### 6.1 MCU — FRDM-K64F

| Property        | Value                              |
|-----------------|------------------------------------|
| Processor       | ARM Cortex-M4, 120 MHz             |
| Flash           | 1 MB                               |
| RAM             | 256 KB                             |
| OS              | Mbed OS 6                          |
| ADC             | 16-bit, 3.3V reference             |
| GPIO voltage    | 3.3V (max 4mA per pin)             |
| USB Serial      | /dev/cu.usbmodem102 at 115200 baud |

### 6.2 Pin Mapping

| Function     | Pin(s)         | Interface | Notes                    |
|-------------|----------------|-----------|--------------------------|
| ESP32 TX    | PTC17          | UART3     | 115200 baud              |
| ESP32 RX    | PTC16          | UART3     |                          |
| PMS5003 TX  | PTC4           | UART1     | 9600 baud                |
| PMS5003 RX  | PTC3           | UART1     |                          |
| MQ Analog   | PTB2           | ADC       | 0–3.3V                   |
| MQ Digital  | PTB3           | GPIO In   | 0 or 1                   |
| I2C SDA     | PTE25          | I2C       | BME680 + ENS160          |
| I2C SCL     | PTE24          | I2C       | 100 kHz                  |
| SD MOSI     | PTD2           | SPI       | 400 kHz                  |
| SD MISO     | PTD3           | SPI       |                          |
| SD SCK      | PTD1           | SPI       |                          |
| SD CS       | PTD0           | SPI       |                          |
| Alarm Out   | PTA2           | GPIO Out  | HIGH=on, 3.3V max        |
| Red LED     | LED1 (onboard) | GPIO Out  | Active LOW               |
| Green LED   | LED2 (onboard) | GPIO Out  | Active LOW               |
| Blue LED    | LED3 (onboard) | GPIO Out  | Active LOW               |

### 6.3 Wiring Diagram

```
ESP32-WROOM-32:
  P17 (TX) ──── PTC16 (K64F RX)
  P16 (RX) ──── PTC17 (K64F TX)
  GND      ──── GND
  5V       ──── 5V

PMS5003:
  TX       ──── PTC3 (K64F RX)
  5V       ──── 5V
  GND      ──── GND

MQ Gas Sensor (Flying Fish):
  AO       ──── PTB2
  DO       ──── PTB3
  VCC      ──── 5V
  GND      ──── GND

BME680:
  SDA      ──── PTE25 (via breadboard row 50)
  SCL      ──── PTE24 (via breadboard row 45)
  VCC      ──── 3.3V
  GND      ──── GND
  CS       ──── 3.3V (I2C mode)
  SDO      ──── GND  (sets address 0x76)

ENS160 (currently disconnected):
  SDA      ──── PTE25
  SCL      ──── PTE24
  VIN      ──── 5V
  GND      ──── GND
  ADD      ──── GND  (sets address 0x52)
  CS       ──── 3.3V (I2C mode)

MicroSD Card Module:
  CS       ──── PTD0
  SCK      ──── PTD1
  MOSI     ──── PTD2
  MISO     ──── PTD3
  VCC      ──── 5V
  GND      ──── GND

Alarm:
  PTA2     ──── External alarm (LED/buzzer, 3.3V max without transistor)
```

---

## 7. Reinforcement Learning Integration Guide

### 7.1 State Space (Observation Vector)

The RL agent receives this observation at each timestep (~2.5s):

| Index | Feature            | Type   | Range          | Notes                            |
|-------|-------------------|--------|----------------|----------------------------------|
| 0     | PM1.0             | uint16 | 0–1000+        | µg/m³                            |
| 1     | PM2.5             | uint16 | 0–1000+        | µg/m³ — **primary pollution indicator** |
| 2     | PM10              | uint16 | 0–1000+        | µg/m³                            |
| 3     | TVOC              | uint16 | 0–10000        | ppb (0 when ENS160 unavailable)  |
| 4     | eCO2              | uint16 | 400–10000      | ppm (0 when ENS160 unavailable)  |
| 5     | Temperature       | float  | -40 to 85      | °C                               |
| 6     | Humidity          | float  | 0–100          | %RH                              |
| 7     | Pressure          | float  | 300–1100       | hPa                              |
| 8     | Gas Resistance    | float  | 10K–20M        | Ohms (BME680 VOC indicator)      |
| 9     | MQ Voltage        | float  | 0.0–3.3        | Volts (mq_analog × 3.3)         |
| 10    | MQ Digital        | int    | 0 or 1         | Module threshold trigger         |
| 11    | PMS Valid         | bool   | 0 or 1         | Was PMS5003 packet valid?        |
| 12    | ENS160 Available  | bool   | 0 or 1         | Is ENS160 online?                |
| 13    | BME680 Available  | bool   | 0 or 1         | Is BME680 online?                |
| 14    | Current AQI Score | int    | 0–500          | From current rule-based system   |
| 15    | Alarm Active      | bool   | 0 or 1         | Current alarm state              |

**Total**: 16 features per timestep

### 7.2 Action Space

| Action | Value | Description                |
|--------|-------|----------------------------|
| 0      | OFF   | Alarm off (air is safe)    |
| 1      | ON    | Alarm on (danger detected) |

For a more sophisticated agent, consider a **multi-level** action space:

| Action | Description           | Physical Response          |
|--------|-----------------------|----------------------------|
| 0      | ALL_CLEAR             | Alarm off, green LED       |
| 1      | CAUTION               | Alarm off, yellow blink    |
| 2      | WARNING               | Alarm pulsing              |
| 3      | DANGER                | Alarm solid on, red LED    |

### 7.3 Reward Function Design Suggestions

```python
def reward(action, true_danger, previous_action):
    """
    Reward function for air quality alarm RL agent.
    
    Priorities:
    1. Never miss a real danger event (high penalty for false negative)
    2. Minimize false alarms (moderate penalty)
    3. Quick response to danger onset
    4. Quick recovery when danger passes
    """
    
    if true_danger and action == ALARM_ON:
        return +10.0    # Correct detection (true positive)
    
    elif true_danger and action == ALARM_OFF:
        return -50.0    # MISSED DANGER — highest penalty (false negative)
    
    elif not true_danger and action == ALARM_OFF:
        return +1.0     # Correct silence (true negative)
    
    elif not true_danger and action == ALARM_ON:
        return -5.0     # False alarm (false positive)
    
    # Bonus for fast transitions
    if true_danger and previous_action == ALARM_OFF and action == ALARM_ON:
        return +15.0    # Quick response bonus
    
    if not true_danger and previous_action == ALARM_ON and action == ALARM_OFF:
        return +5.0     # Quick recovery bonus
```

### 7.4 Training Data Collection

The SD card logs provide ready-made training data. To create labeled training data:

1. **Collect CSV data** from `/sd/data/YYYY-MM-DD.csv`
2. **Label danger events** manually or use the alarm log (`alarms_YYYY-MM-DD.csv`) as ground truth
3. **Create episodes** from the time-series data

**Data preprocessing suggestions:**
- Normalize MQ voltage by temperature/humidity (MQ sensors are temperature-sensitive)
- Use BME680 `gas_res` as a secondary VOC indicator — log-scale it: `log10(gas_res)`
- Handle missing ENS160 data (TVOC=0, eCO2=0) by masking or imputing
- Consider sliding window of 5–10 readings for temporal context
- PM2.5 and PM10 have ~10 second decay lag — account for this in temporal modeling

### 7.5 Feature Engineering Ideas

```python
# Derived features that could improve RL performance

# 1. Rate of change (delta between consecutive readings)
delta_pm25 = pm25[t] - pm25[t-1]

# 2. Moving averages (smooth noise)
pm25_avg_5 = mean(pm25[t-4:t+1])

# 3. BME680 gas resistance (log scale, inverse = more pollution)
log_gas = log10(gas_resistance)
gas_quality = 1.0 / log_gas  # Higher = worse air

# 4. MQ sensor temperature compensation
mq_compensated = mq_voltage / (1.0 + 0.02 * (temperature - 25.0))

# 5. PM ratio (helps identify pollution source)
pm_ratio = pm2_5 / max(pm10, 1)  # High ratio = fine particles (smoke)

# 6. Time features
hour_of_day = timestamp.hour  # Pollution patterns vary by time
is_night = hour_of_day < 6 or hour_of_day > 22

# 7. Sensor agreement score (how many sensors show elevated readings)
elevated = sum([pm25 > 35, mq_voltage > 0.5, gas_res < 1e6])
```

### 7.6 Deployment on K64F

The K64F has limited resources (256 KB RAM, 1 MB Flash). Options for deploying the trained model:

1. **Lookup Table**: Quantize the state space and pre-compute a Q-table. Fits easily in flash.
2. **Small Neural Network**: A 2-layer network (16→8→2) uses ~200 parameters = ~800 bytes. Use fixed-point math.
3. **Decision Tree**: Export a trained decision tree as C `if/else` statements. Very fast inference.
4. **Offload to ESP32**: Send sensor data via UART to ESP32, run inference there (ESP32 has 520KB RAM + WiFi for cloud inference).
5. **Cloud-based**: Send data via ESP32 WiFi to a server, receive alarm decision back.

**For on-device deployment**, replace the `checkAirQuality()` function in `main.cpp` (lines ~961–1085) with the trained model's inference code.

---

## 8. Communication Protocol

### 8.1 ESP32 AT Commands (WiFi)

The ESP32 runs ESP-AT v4.1.1.0 firmware and communicates via UART3 at 115200 baud.

| Command                              | Purpose                      |
|---------------------------------------|------------------------------|
| `AT`                                  | Test connection              |
| `AT+CWMODE=1`                         | Set station mode             |
| `AT+CWJAP="Mirac","281209MEN"`        | Connect to WiFi              |
| `AT+CIFSR`                            | Get IP address               |
| `AT+PING="google.com"`               | Test internet                |
| `AT+CIPSNTPCFG=1,0,"pool.ntp.org"`   | Configure NTP                |
| `AT+CIPSNTPTIME?`                     | Get current time             |

**WiFi credentials**: SSID=`Mirac`, Password=`281209MEN`, IP=`10.0.0.7`

### 8.2 Serial Output Format

The K64F outputs these lines to USB serial (115200 baud) every cycle:

```
[PMS5003] PM1.0=8  PM2.5=15  PM10=18 ug/m3
[PMS5003] PM1.0_atm=8  PM2.5_atm=15  PM10_atm=18 ug/m3
[MQ] Analog=0.031 (0.10V)  Digital=1
[BME680] Temp=27.3C  Hum=18.2%  Press=990.9hPa  Gas=14523678 Ohms
[AQI] Score=57 (GOOD) worst=PM2.5 [PM25=57 PM10=15 MQ=0 | Tmp=0 Hum=50(info)]
[SD] Data saved
```

During alarm events:
```
[AQI] Score=287 (VERY_UNHEALTHY) worst=PM2.5 [PM25=287 PM10=195 MQ=150 | Tmp=0 Hum=50(info)]
[ALARM] >>> DANGER - AQI=287 (VERY_UNHEALTHY) - ALARM ON <<<
[ALARM] Elevated: PM2.5(287) PM10(195) MQ_Gas(150)
```

---

## 9. Typical Sensor Values (Reference Data)

### Normal Indoor Air (No Pollution Source)

| Sensor       | Value           | AQI Sub-Score |
|-------------|-----------------|---------------|
| PM2.5       | 10–20 µg/m³     | 40–80         |
| PM10        | 15–25 µg/m³     | 13–23         |
| MQ Voltage  | 0.08–0.15 V     | 0             |
| Temperature | 22–28 °C        | 0 (info only) |
| Humidity    | 15–25 %RH       | 50–100 (info) |
| Gas Res     | 8M–15M Ohms     | not scored    |
| **Composite** | **40–80**      | **GOOD/MOD**  |

### Vape Smoke (Direct Blow at ~30cm)

| Sensor       | Peak Value      | AQI Sub-Score |
|-------------|-----------------|---------------|
| PM2.5       | 400–1000+ µg/m³ | 301–500+      |
| PM10        | 500–1000+ µg/m³ | 301–500+      |
| MQ Voltage  | 1.0–3.0 V       | 100–500       |
| Temperature | +1–2 °C rise    | 0             |
| Humidity    | slight rise     | varies        |
| Gas Res     | drops to 50K–1M | not scored    |
| **Composite** | **350–500+**   | **HAZARDOUS** |

### Decay Profile After Vape

```
Time    PM2.5    MQ(V)    Composite    Alarm
0s      709      1.45     ~400         ON
10s     350      0.90     ~280         ON
20s     120      0.55     ~165         ON
30s     55       0.35     ~101         ON (hysteresis)
45s     30       0.15     ~70          → ALL CLEAR
60s     18       0.10     ~55          OFF
```

---

## 10. Build & Flash Instructions

```bash
# Build
cd /Users/miracozcan/Desktop/ember_project
mbed compile -m K64F -t GCC_ARM

# Flash
pyocd flash ./BUILD/K64F/GCC_ARM/ember_project.bin --target k64f

# Serial monitor
python3 -c "
import serial
ser = serial.Serial('/dev/cu.usbmodem102', 115200, timeout=1)
while True:
    d = ser.read(512)
    if d: print(d.decode('utf-8', errors='replace'), end='', flush=True)
"
```

---

## 11. File Structure

```
ember_project/
├── main.cpp                  # All firmware code (~1296 lines)
├── mbed_app.json             # Build config (float printf, SD SPI pins)
├── SYSTEM_DOCUMENTATION.md   # This file
├── BUILD/K64F/GCC_ARM/       # Compiled binary
└── mbed-os/                  # Mbed OS 6 framework
```

---

## 12. Glossary

| Term             | Definition                                                     |
|------------------|----------------------------------------------------------------|
| AQI              | Air Quality Index (0–500 scale, EPA standard)                  |
| TVOC             | Total Volatile Organic Compounds (ppb)                         |
| eCO2             | Equivalent CO2, estimated from VOC levels (ppm)                |
| PM2.5            | Particulate Matter ≤2.5 µm diameter (µg/m³)                   |
| PM10             | Particulate Matter ≤10 µm diameter (µg/m³)                    |
| MQ               | Metal Oxide semiconductor gas sensor family                    |
| Gas Resistance   | BME680 heated metal oxide — higher Ohms = cleaner air          |
| Hysteresis       | Dead zone between trigger/clear thresholds to prevent flapping |
| Debounce         | Requiring sustained readings before state change               |
| Forced Mode      | BME680: manually trigger one measurement, then read results    |
| ESP-AT           | Espressif AT command firmware for WiFi control via UART        |
