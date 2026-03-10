# Milestone 1 — RL Training Data Logger with Ground-Truth Labeling on K64F
**Demo:** March 12, 2–3 PM, Room A3058
**Worth:** 1%

---

## What This Milestone Is

The K64F computes the full 16-feature RL state vector from live sensor readings
and writes labeled rows to `training_data.csv` on the microSD card every 2.5s.

Two buttons attach human labels to the data:
- **SW2** brief press → label = DANGER (1), RGB LED turns red
- **SW3** brief press → label = SAFE (0), RGB LED turns green

**Why this matters:** This produces real, hardware-verified training data with
human-assigned ground truth labels. The labeled CSV can be used to retrain the
DQN model on actual sensor hardware instead of just synthetic data.

**This is distinct from Miraç's raw sensor logging** — he logs raw values.
Rajini logs the processed 16-feature RL state vector with human labels.

---

## The 16 Features Written to CSV

| # | Feature | How computed |
|---|---|---|
| 0 | pm1_0 | Raw from PMS5003 |
| 1 | pm2_5 | Raw from PMS5003 |
| 2 | pm10 | Raw from PMS5003 |
| 3 | temperature | Raw from BME680 |
| 4 | humidity | Raw from BME680 |
| 5 | pressure | Raw from BME680 |
| 6 | gas_resistance | Raw from BME680 (Ohms) |
| 7 | mq_analog | ADC reading 0.0–1.0 |
| 8 | mq_digital | 0 or 1 |
| 9 | tvoc | ENS160 (0 if offline) |
| 10 | eco2 | ENS160 (0 if offline) |
| 11 | aqi_category | 0–5 from PM2.5 EPA scale |
| 12 | delta_pm25 | PM2.5[now] − PM2.5[previous] |
| 13 | delta_pm10 | PM10[now] − PM10[previous] |
| 14 | thi | temp + 0.33×humidity − 4.0 |
| 15 | gas_ratio | mq_analog / log10(gas_resistance) |

---

## CSV Format

```
timestamp,pm1,pm2_5,pm10,temp,hum,pres,gas,mq_a,mq_d,tvoc,eco2,aqi_cat,delta_pm25,delta_pm10,thi,gas_ratio,label
2026-03-12 14:23:05,8,15,18,27.3,18.2,990.9,14523678,0.031,0,0,0,1,2.1,1.5,30.3,0.00000221,0
2026-03-12 14:23:07,450,709,812,27.5,18.1,990.5,250000,0.439,1,0,0,5,694.0,794.0,30.4,0.00006228,1
```

---

## Files

| File | What it does |
|---|---|
| `k64f/ember_training_logger.h` | Header: RawSensors_t, RLFeatures_t structs, function declarations |
| `k64f/ember_training_logger.c` | Implementation: feature computation, SD write, SW2/SW3, RGB LED |

---

## How to Integrate Into Mirac's K64F Project

1. Copy `ember_training_logger.h` and `ember_training_logger.c` into Mirac's MCUXpresso project
2. Add `ember_training_logger.c` to the project's source files
3. In your main loop call:

```c
#include "ember_training_logger.h"

// In main() startup:
ember_logger_init();

// In your 2.5s sensor loop (replace stubs with Mirac's driver calls):
RawSensors_t raw;
raw.pm1_0          = pms5003_get_pm1();
raw.pm2_5          = pms5003_get_pm25();
raw.pm10           = pms5003_get_pm10();
raw.temperature    = bme680_get_temperature();
raw.humidity       = bme680_get_humidity();
raw.pressure       = bme680_get_pressure();
raw.gas_resistance = bme680_get_gas();
raw.mq_analog      = mq_get_analog();
raw.mq_digital     = mq_get_digital();
raw.tvoc           = 0.0f;   // ENS160 offline
raw.eco2           = 0.0f;

label = ember_logger_check_buttons(label);
ember_logger_compute_features(&raw, prev_pm2_5, prev_pm10, &features);
ember_logger_print_features(&features, label);
get_timestamp(ts, sizeof(ts));
ember_logger_write_row(ts, &features, label);
prev_pm2_5 = raw.pm2_5;
prev_pm10  = raw.pm10;
```

4. Implement the 5 stub functions using Mirac's drivers:
   - `sw2_pressed()` — GPIO read for SW2
   - `sw3_pressed()` — GPIO read for SW3
   - `rgb_led_set(r, g, b)` — RGB LED GPIO
   - `get_timestamp(buf, size)` — RTC or counter
   - `sd_append_line(filename, line)` — SD card SPI write

---

## Serial Output Each Cycle

```
[LOGGER] Label=SAFE(0)    LED=GREEN
  PM:  1.0=8.0  2.5=15.0  10=18.0
  ENV: T=27.3  H=18.2  P=990.9  Gas=14523678
  MQ:  A=0.031  D=0
  DRV: AQI=1  dPM25=2.1  dPM10=1.5  THI=30.3  GasR=0.00000221
[SD]   Row written to training_data.csv

[LOGGER] *** SW2 PRESSED — switching to DANGER label ***
[LOGGER] Label=DANGER(1)  LED=RED
  PM:  1.0=450.0  2.5=709.0  10=812.0
  ...
[SD]   Row written to training_data.csv
```

---

## What to Show the Professor at Demo

1. Serial terminal showing features printing every 2.5 seconds
2. Press **SW3** — LED turns green, label=SAFE(0) visible in serial
3. Bring something near the sensor (vape, lighter, smoke) — PM2.5 rises, delta_pm25 goes positive
4. Press **SW2** — LED turns red, label=DANGER(1) visible in serial
5. Open SD card on a laptop — show `training_data.csv` with both labeled rows

**Say to professor:**
> "Milestone 1 is a training data collection tool running on the K64F.
> It computes the same 16-feature vector the DQN model was trained on —
> including derived features like PM2.5 delta, temperature-humidity index,
> and gas ratio. Every 2.5 seconds this is written to SD as a CSV row.
> SW2 labels readings as DANGER, SW3 as SAFE.
> This lets us collect real labeled hardware data to retrain the RL model
> on actual sensor readings instead of only synthetic data."

**If asked "Why 16 features?"**
> "These are the exact features the DQN neural network was trained on.
> Raw PM2.5 alone isn't enough — the AI also needs rate of change (delta)
> to detect rapid rises, gas ratio to cross-validate the MQ sensor against
> the BME680, and AQI category to encode the severity level."

---

## Completion Checklist

- [ ] K64F computes all 16 RL features from raw sensor readings each cycle
- [ ] Features + timestamp written to training_data.csv on microSD via SPI
- [ ] Press SW2 → label switches to DANGER (LED turns red)
- [ ] Press SW3 → label switches to SAFE (LED turns green)
- [ ] CSV contains: timestamp, 16 features, label column (0 or 1)
- [ ] Serial terminal shows computed features and current label each cycle
