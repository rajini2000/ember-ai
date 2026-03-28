"""
M2 Simulation — On-Board RL Inference (laptop version)
Mirrors exactly what ember_rl_inference.cpp does on the K64F.

Run:
    python m2/m2_simulate.py

Controls:
    1 + Enter  -> Test: Clean air (expect ALARM OFF)
    2 + Enter  -> Test: Vape smoke peak (expect ALARM ON)
    3 + Enter  -> Test: Decay after smoke (expect ALARM OFF)
    L + Enter  -> Live mode: type your own sensor values
    Q + Enter  -> Quit
"""

import os, sys, math
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

import numpy as np

# ── Load trained model ────────────────────────────────────────────────────────
try:
    from stable_baselines3 import DQN
except ImportError:
    print("[ERROR] stable_baselines3 not installed. Run: pip install stable-baselines3")
    sys.exit(1)

MODEL_PATH = os.path.join(os.path.dirname(__file__), '..', 'models', 'best_model.zip')
if not os.path.exists(MODEL_PATH):
    print(f"[ERROR] Model not found: {MODEL_PATH}")
    sys.exit(1)

print("[M2] Loading model...")
model = DQN.load(MODEL_PATH)
q_net = model.policy.q_net.q_net
print("[M2] Model loaded OK\n")

# ── Normalisation constants (must match ember_rl_inference.cpp) ───────────────
NORM_PM1_MAX      = 500.0
NORM_PM25_MAX     = 500.0
NORM_PM10_MAX     = 600.0
NORM_TVOC_MAX     = 10000.0
NORM_ECO2_MIN     = 400.0
NORM_ECO2_RANGE   = 9600.0
NORM_TEMP_MIN     = -40.0
NORM_TEMP_RANGE   = 165.0
NORM_HUM_MAX      = 100.0
NORM_PRES_MIN     = 300.0
NORM_PRES_RANGE   = 800.0
NORM_GAS_LOG_MAX  = 7.301
NORM_AQI_MAX      = 500.0

# ── AQI engine (mirrors aqi_engine.py) ───────────────────────────────────────
def _interp(v, bps):
    for (cl, ch, il, ih) in bps:
        if cl <= v <= ch:
            return il + (v - cl) * (ih - il) / (ch - cl)
    return bps[-1][3] if v > bps[-1][1] else 0.0

def pm25_to_aqi(pm):
    return _interp(pm, [(0,12,0,50),(12.1,35.4,51,100),(35.5,55.4,101,150),
                        (55.5,150.4,151,200),(150.5,250.4,201,300),(250.5,500.4,301,500)])

def pm10_to_aqi(pm):
    return _interp(pm, [(0,54,0,50),(55,154,51,100),(155,254,101,150),
                        (255,354,151,200),(355,424,201,300),(425,604,301,500)])

def mq_to_aqi(v):
    if v < 0.3:  return 0
    if v < 0.6:  return 50
    if v < 1.0:  return 100
    if v < 1.5:  return 150
    if v < 2.0:  return 200
    if v < 2.5:  return 300
    return 500

def composite_aqi(pm25, pm10, mq_v, tvoc=0, eco2=0):
    scores = [pm25_to_aqi(pm25), pm10_to_aqi(pm10), mq_to_aqi(mq_v)]
    best = max(scores)
    above = sum(1 for s in scores if s > 100)
    if above >= 2: best *= 1.20
    return min(best, 500.0)

# ── Feature normalisation (mirrors build_obs in ember_rl_inference.cpp) ───────
prev_alarm = 0

def build_obs(pm1_0, pm2_5, pm10, temperature, humidity, pressure,
              gas_res, mq_analog, mq_digital, tvoc, eco2, aqi):
    obs = np.zeros(16, dtype=np.float32)
    obs[0]  = pm1_0  / NORM_PM1_MAX
    obs[1]  = pm2_5  / NORM_PM25_MAX
    obs[2]  = pm10   / NORM_PM10_MAX
    obs[3]  = tvoc   / NORM_TVOC_MAX
    obs[4]  = max(eco2 - NORM_ECO2_MIN, 0.0) / NORM_ECO2_RANGE
    obs[5]  = (temperature - NORM_TEMP_MIN) / NORM_TEMP_RANGE
    obs[6]  = humidity  / NORM_HUM_MAX
    obs[7]  = (pressure - NORM_PRES_MIN) / NORM_PRES_RANGE
    gas_clamped = max(gas_res, 10000.0)
    obs[8]  = math.log10(gas_clamped) / NORM_GAS_LOG_MAX
    obs[9]  = mq_analog
    obs[10] = float(mq_digital)
    obs[11] = 1.0   # pms_valid
    obs[12] = 0.0   # ens160_available
    obs[13] = 1.0   # bme680_available
    obs[14] = aqi   / NORM_AQI_MAX
    obs[15] = float(prev_alarm)
    return np.clip(obs, 0.0, 1.0)

# ── Run inference (same as ember_inference_run on K64F) ───────────────────────
def run_inference(pm1_0, pm2_5, pm10, temperature, humidity, pressure,
                  gas_res, mq_analog, mq_digital, tvoc=0, eco2=0):
    global prev_alarm

    mq_v = mq_analog * 3.3
    aqi  = composite_aqi(pm2_5, pm10, mq_v, tvoc, eco2)
    obs  = build_obs(pm1_0, pm2_5, pm10, temperature, humidity, pressure,
                     gas_res, mq_analog, mq_digital, tvoc, eco2, aqi)

    import torch
    with torch.no_grad():
        obs_t  = torch.FloatTensor(obs).unsqueeze(0)
        q_vals = q_net(obs_t).squeeze().numpy()

    action    = int(np.argmax(q_vals))
    prev_alarm = action
    return action, q_vals[0], q_vals[1], aqi

def print_result(label, action, q_off, q_on, aqi):
    alarm_str = "ON " if action == 1 else "OFF"
    led_str   = "RED  " if action == 1 else "GREEN"
    print(f"  [M2] AQI={aqi:.1f}  Q(OFF)={q_off:+.3f}  Q(ON)={q_on:+.3f}  -> ALARM {alarm_str}  | LED={led_str}")
    if action == 1:
        print("  [GPIO] PTA2 = HIGH  (alarm buzzer ON)")
    else:
        print("  [GPIO] PTA2 = LOW   (alarm buzzer OFF)")

# ── Preset test scenarios ─────────────────────────────────────────────────────
SCENARIOS = {
    "1": {
        "name": "Clean indoor air",
        "data": dict(pm1_0=8,   pm2_5=15,  pm10=18,
                     temperature=23.0, humidity=45.0, pressure=1012.0,
                     gas_res=60000.0, mq_analog=0.05, mq_digital=0,
                     tvoc=0, eco2=0)
    },
    "2": {
        "name": "Vape smoke peak",
        "data": dict(pm1_0=500, pm2_5=709, pm10=812,
                     temperature=27.5, humidity=18.1, pressure=990.5,
                     gas_res=250000.0, mq_analog=0.439, mq_digital=1,
                     tvoc=0, eco2=0)
    },
    "3": {
        "name": "Decay 30s after smoke",
        "data": dict(pm1_0=40,  pm2_5=55,  pm10=68,
                     temperature=27.4, humidity=18.3, pressure=990.7,
                     gas_res=1200000.0, mq_analog=0.106, mq_digital=0,
                     tvoc=0, eco2=0)
    },
}

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    global prev_alarm
    print("=" * 60)
    print("  M2 Simulation — On-Board RL Inference")
    print("=" * 60)
    print("  1  ->  Test: Clean air")
    print("  2  ->  Test: Vape smoke peak")
    print("  3  ->  Test: Decay after smoke")
    print("  L  ->  Live: enter your own values")
    print("  Q  ->  Quit")
    print("=" * 60)

    while True:
        try:
            cmd = input("\n> ").strip().upper()
        except (EOFError, KeyboardInterrupt):
            break

        if cmd == "Q":
            print("Bye.")
            break

        elif cmd in SCENARIOS:
            s = SCENARIOS[cmd]
            print(f"\n  Scenario: {s['name']}")
            d = s['data']
            action, q_off, q_on, aqi = run_inference(**d)
            print_result(s['name'], action, q_off, q_on, aqi)

        elif cmd == "L":
            print("  Enter sensor values (press Enter to use defaults):")
            try:
                pm2_5 = float(input("    PM2.5 (ug/m3)  [default 15]: ") or 15)
                pm10  = float(input("    PM10  (ug/m3)  [default 18]: ") or 18)
                pm1_0 = float(input("    PM1.0 (ug/m3)  [default 8 ]: ") or 8)
                mq    = float(input("    MQ analog 0-1  [default 0.05]: ") or 0.05)
                temp  = float(input("    Temperature C  [default 23]: ") or 23)
                hum   = float(input("    Humidity %%     [default 45]: ") or 45)
                pres  = float(input("    Pressure hPa   [default 1012]: ") or 1012)
                gas   = float(input("    Gas resistance [default 60000]: ") or 60000)
                action, q_off, q_on, aqi = run_inference(
                    pm1_0=pm1_0, pm2_5=pm2_5, pm10=pm10,
                    temperature=temp, humidity=hum, pressure=pres,
                    gas_res=gas, mq_analog=mq, mq_digital=int(mq > 0.15))
                print_result("Custom", action, q_off, q_on, aqi)
            except ValueError:
                print("  [ERROR] Invalid input")

        else:
            print("  Unknown command. Use 1, 2, 3, L, or Q.")

if __name__ == "__main__":
    main()
