"""
Generate realistic M1 training dataset for Ember AI project.
Produces training_data.csv with 500 rows, ~62% SAFE / ~38% DANGER.
Timestamps simulate a real 2-hour data collection session.
"""

import random
import math
import csv
import os
from datetime import datetime, timedelta

random.seed(42)

OUTPUT = os.path.join(os.path.dirname(__file__), "training_data.csv")

# 500 rows total — 313 SAFE, 187 DANGER (uneven, realistic)
TOTAL = 500
SAFE_COUNT   = 313
DANGER_COUNT = 187

# Mix labels in a realistic pattern (not perfectly alternating)
labels = [0] * SAFE_COUNT + [1] * DANGER_COUNT
random.shuffle(labels)

# Start time: simulate session from 9:00 AM
start_time = datetime(2026, 3, 12, 9, 0, 0)

prev_pm25 = 0.0
prev_pm10 = 0.0
first_cycle = True

def safe_sensors():
    return dict(
        pm1_0       = random.randint(1, 10),
        pm2_5       = random.randint(2, 11),
        pm10        = random.randint(4, 18),
        temperature = round(random.uniform(19.5, 25.5), 2),
        humidity    = round(random.uniform(32.0, 54.0), 2),
        pressure    = round(random.uniform(1009.0, 1015.0), 2),
        gas_res     = round(random.uniform(38000, 82000), 0),
        mq_analog   = round(random.uniform(0.04, 0.22), 4),
        mq_digital  = 0,
        tvoc        = random.randint(40, 210),
        eco2        = random.randint(390, 620),
    )

def danger_sensors():
    return dict(
        pm1_0       = random.randint(35, 130),
        pm2_5       = random.randint(56, 200),
        pm10        = random.randint(75, 260),
        temperature = round(random.uniform(23.5, 33.0), 2),
        humidity    = round(random.uniform(54.0, 82.0), 2),
        pressure    = round(random.uniform(1007.0, 1013.0), 2),
        gas_res     = round(random.uniform(4000, 22000), 0),
        mq_analog   = round(random.uniform(0.52, 0.93), 4),
        mq_digital  = 1,
        tvoc        = random.randint(480, 2100),
        eco2        = random.randint(950, 3200),
    )

def compute_features(s, prev_pm25, prev_pm10, first_cycle):
    f = [0.0] * 16
    f[0]  = float(s['pm1_0'])
    f[1]  = float(s['pm2_5'])
    f[2]  = float(s['pm10'])
    f[3]  = s['temperature']
    f[4]  = s['humidity']
    f[5]  = s['pressure']
    f[6]  = s['gas_res']
    f[7]  = s['mq_analog']
    f[8]  = float(s['mq_digital'])
    f[9]  = float(s['tvoc'])
    f[10] = float(s['eco2'])

    pm = f[1]
    if   pm <= 12.0:  cat = 0
    elif pm <= 35.4:  cat = 1
    elif pm <= 55.4:  cat = 2
    elif pm <= 150.4: cat = 3
    elif pm <= 250.4: cat = 4
    else:             cat = 5
    f[11] = float(cat)

    if first_cycle:
        f[12] = 0.0
        f[13] = 0.0
    else:
        f[12] = f[1] - prev_pm25
        f[13] = f[2] - prev_pm10

    f[14] = f[3] + 0.33 * f[4] - 4.0

    log_gas = math.log10(f[6]) if f[6] > 1.0 else 1.0
    f[15]   = (f[7] / log_gas) if log_gas > 0.0 else 0.0

    return f

rows = []
current_time = start_time

for i, label in enumerate(labels):
    s = safe_sensors() if label == 0 else danger_sensors()
    f = compute_features(s, prev_pm25, prev_pm10, first_cycle)

    prev_pm25   = f[1]
    prev_pm10   = f[2]
    first_cycle = False

    ts = current_time.strftime("%Y-%m-%d %H:%M:%S")
    # Add small jitter to timestamp (2.4s – 2.7s per cycle)
    current_time += timedelta(seconds=random.uniform(2.4, 2.7))

    rows.append([
        ts,
        f"{f[0]:.0f}", f"{f[1]:.0f}", f"{f[2]:.0f}",
        f"{f[3]:.2f}", f"{f[4]:.2f}", f"{f[5]:.2f}", f"{f[6]:.0f}",
        f"{f[7]:.4f}", int(f[8]),
        f"{f[9]:.0f}", f"{f[10]:.0f}",
        f"{f[11]:.0f}", f"{f[12]:.2f}", f"{f[13]:.2f}",
        f"{f[14]:.2f}", f"{f[15]:.6f}",
        label
    ])

header = [
    "timestamp",
    "pm1_0","pm2_5","pm10",
    "temperature","humidity","pressure","gas_resistance",
    "mq_analog","mq_digital",
    "tvoc","eco2",
    "aqi_category","delta_pm25","delta_pm10","thi","gas_ratio",
    "label"
]

with open(OUTPUT, "w", newline="") as fp:
    writer = csv.writer(fp)
    writer.writerow(header)
    writer.writerows(rows)

print(f"Generated {TOTAL} rows ({SAFE_COUNT} SAFE, {DANGER_COUNT} DANGER)")
print(f"Saved to: {OUTPUT}")
