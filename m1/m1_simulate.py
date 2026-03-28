"""
M1 Simulation — RL Training Data Logger
Simulates the K64F firmware behavior on your laptop.

Controls:
  S + Enter  -> Label = SAFE   (0)  [simulates SW3]
  D + Enter  -> Label = DANGER (1)  [simulates SW2]
  DUMP + Enter -> Print training_data.csv contents
  Q + Enter  -> Quit

Output: training_data.csv in the same folder as this script
"""

import random
import math
import time
import csv
import os
import threading
from datetime import datetime

# ── Output file ──────────────────────────────────────────────
CSV_PATH = os.path.join(os.path.dirname(__file__), "training_data.csv")

# ── Training state ────────────────────────────────────────────
training_label = 0      # 0=SAFE, 1=DANGER
label_lock     = threading.Lock()
running        = True
prev_pm25      = 0.0
prev_pm10      = 0.0
first_cycle    = True

# ── Fake sensor value generators ─────────────────────────────
def fake_sensors(label):
    """Generate realistic sensor readings based on label."""
    if label == 0:  # SAFE — clean air
        pm1_0        = random.randint(2,  10)
        pm2_5        = random.randint(3,  12)
        pm10         = random.randint(5,  20)
        temperature  = round(random.uniform(20.0, 26.0), 2)
        humidity     = round(random.uniform(35.0, 55.0), 2)
        pressure     = round(random.uniform(1010.0, 1015.0), 2)
        gas_res      = round(random.uniform(40000, 80000), 0)
        mq_analog    = round(random.uniform(0.05, 0.20), 4)
        mq_digital   = 0
        tvoc         = random.randint(50,  200)
        eco2         = random.randint(400, 600)
    else:            # DANGER — polluted air
        pm1_0        = random.randint(40,  120)
        pm2_5        = random.randint(55,  180)
        pm10         = random.randint(80,  250)
        temperature  = round(random.uniform(24.0, 32.0), 2)
        humidity     = round(random.uniform(55.0, 80.0), 2)
        pressure     = round(random.uniform(1008.0, 1013.0), 2)
        gas_res      = round(random.uniform(5000, 20000), 0)
        mq_analog    = round(random.uniform(0.55, 0.90), 4)
        mq_digital   = 1
        tvoc         = random.randint(500, 2000)
        eco2         = random.randint(1000, 3000)
    return (pm1_0, pm2_5, pm10, temperature, humidity,
            pressure, gas_res, mq_analog, mq_digital, tvoc, eco2)

# ── 16-feature computation (mirrors ember_rl_training.cpp) ───
def compute_features(pm1_0, pm2_5, pm10, temperature, humidity,
                     pressure, gas_res, mq_analog, mq_digital,
                     tvoc, eco2):
    global prev_pm25, prev_pm10, first_cycle

    f = [0.0] * 16
    f[0]  = float(pm1_0)
    f[1]  = float(pm2_5)
    f[2]  = float(pm10)
    f[3]  = temperature
    f[4]  = humidity
    f[5]  = pressure
    f[6]  = gas_res
    f[7]  = mq_analog
    f[8]  = float(mq_digital)
    f[9]  = float(tvoc)
    f[10] = float(eco2)

    # AQI category from PM2.5 (EPA breakpoints)
    pm = f[1]
    if   pm <= 12.0:  cat = 0
    elif pm <= 35.4:  cat = 1
    elif pm <= 55.4:  cat = 2
    elif pm <= 150.4: cat = 3
    elif pm <= 250.4: cat = 4
    else:             cat = 5
    f[11] = float(cat)

    # Delta PM
    if first_cycle:
        f[12] = 0.0
        f[13] = 0.0
        first_cycle = False
    else:
        f[12] = f[1] - prev_pm25
        f[13] = f[2] - prev_pm10
    prev_pm25 = f[1]
    prev_pm10 = f[2]

    # Temperature-Humidity Index
    f[14] = f[3] + 0.33 * f[4] - 4.0

    # Gas ratio
    log_gas = math.log10(f[6]) if f[6] > 1.0 else 1.0
    f[15] = (f[7] / log_gas) if log_gas > 0.0 else 0.0

    return f

# ── Write CSV row ─────────────────────────────────────────────
def write_csv(f, label):
    is_new = not os.path.exists(CSV_PATH)
    with open(CSV_PATH, "a", newline="") as fp:
        writer = csv.writer(fp)
        if is_new:
            writer.writerow([
                "timestamp",
                "pm1_0","pm2_5","pm10",
                "temperature","humidity","pressure","gas_resistance",
                "mq_analog","mq_digital",
                "tvoc","eco2",
                "aqi_category","delta_pm25","delta_pm10","thi","gas_ratio",
                "label"
            ])
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        writer.writerow([
            ts,
            f"{f[0]:.0f}", f"{f[1]:.0f}", f"{f[2]:.0f}",
            f"{f[3]:.2f}", f"{f[4]:.2f}", f"{f[5]:.2f}", f"{f[6]:.0f}",
            f"{f[7]:.4f}", int(f[8]),
            f"{f[9]:.0f}", f"{f[10]:.0f}",
            f"{f[11]:.0f}", f"{f[12]:.2f}", f"{f[13]:.2f}",
            f"{f[14]:.2f}", f"{f[15]:.6f}",
            label
        ])

# ── Dump CSV to terminal ──────────────────────────────────────
def dump_csv():
    if not os.path.exists(CSV_PATH):
        print("[DUMP] training_data.csv not found yet")
        return
    print("[DUMP] ===== training_data.csv =====")
    with open(CSV_PATH, "r") as fp:
        print(fp.read())
    print("[DUMP] ===== END =====")

# ── Sensor loop (runs in background thread) ───────────────────
def sensor_loop():
    global running
    cycle = 0
    while running:
        with label_lock:
            label = training_label

        sensors = fake_sensors(label)
        f = compute_features(*sensors)

        label_str = "DANGER" if label == 1 else "SAFE"
        led_str   = "RED" if label == 1 else "GREEN"

        print(f"\n[TRAIN] Label={label_str}({label}) | "
              f"PM:[{f[0]:.0f} {f[1]:.0f} {f[2]:.0f}] "
              f"T={f[3]:.1f}C H={f[4]:.1f}% P={f[5]:.1f} "
              f"Gas={f[6]:.0f} MQ=[{f[7]:.4f} {int(f[8])}] | LED={led_str}")
        print(f"[TRAIN] TVOC={f[9]:.0f} eCO2={f[10]:.0f} | "
              f"AQI_cat={f[11]:.0f} dPM25={f[12]:.2f} dPM10={f[13]:.2f} "
              f"THI={f[14]:.2f} GasR={f[15]:.6f}")

        write_csv(f, label)
        print(f"[TRAIN] Row saved to training_data.csv  (cycle {cycle+1})")

        cycle += 1
        time.sleep(2.5)

# ── Main ──────────────────────────────────────────────────────
def main():
    global training_label, running

    print("=" * 60)
    print("  M1 Simulation — K64F RL Training Data Logger")
    print("=" * 60)
    print("  S + Enter  ->  Label = SAFE   (SW3)")
    print("  D + Enter  ->  Label = DANGER (SW2)")
    print("  DUMP       ->  Print CSV contents")
    print("  Q          ->  Quit")
    print("=" * 60)
    print(f"  Writing to: {CSV_PATH}")
    print("=" * 60)
    print()

    # Start sensor loop in background
    t = threading.Thread(target=sensor_loop, daemon=True)
    t.start()

    # Main thread handles keyboard input
    while running:
        try:
            cmd = input().strip().upper()
        except (EOFError, KeyboardInterrupt):
            break

        if cmd == "S":
            with label_lock:
                training_label = 0
            print("[TRAIN] SW3 pressed -> Label = SAFE(0)   | LED = GREEN")
        elif cmd == "D":
            with label_lock:
                training_label = 1
            print("[TRAIN] SW2 pressed -> Label = DANGER(1) | LED = RED")
        elif cmd == "DUMP":
            dump_csv()
        elif cmd == "Q":
            print("Stopping simulation...")
            running = False

if __name__ == "__main__":
    main()
