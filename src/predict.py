"""
Inference module — used by the website / Twilio integration.

Your partner's server sends a JSON object with the latest sensor readings.
This module loads the trained model and returns an alarm decision.

Usage (standalone test):
    python predict.py

Usage (from your partner's Python server):
    from predict import EmberPredictor
    predictor = EmberPredictor()
    result = predictor.predict({
        "PM1.0": 8, "PM2.5": 15, "PM10": 18,
        "TVOC": 0,  "eCO2": 0,
        "temperature": 27.3, "humidity": 18.2,
        "pressure": 990.9,   "gas": 14523678.0,
        "MQ_analog": 0.031,  "MQ_digital": 1
    })
    print(result)
    # {'alarm': 'OFF', 'action': 0, 'aqi_estimate': 57.4, 'category': 'GOOD'}
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

import json
import numpy as np
from pathlib import Path

from stable_baselines3 import DQN
from data_loader import DataLoader, NORM
from aqi_engine import compute_composite_aqi, get_category

ROOT       = os.path.join(os.path.dirname(__file__), '..')
MODELS_DIR = os.path.join(ROOT, 'models')
MODEL_PATH = os.path.join(MODELS_DIR, 'best_model.zip')


class EmberPredictor:
    """
    Wraps the trained DQN model for single-sample inference.

    Designed to be called by your partner's web server on every
    incoming sensor reading from the hardware.
    """

    def __init__(self, model_path: str = None):
        path = model_path or MODEL_PATH
        if not os.path.exists(path):
            raise FileNotFoundError(
                f"Model not found at {path}. Run train.py first."
            )
        self.model       = DQN.load(path)
        self.loader      = DataLoader()
        self._prev_action = 0     # track alarm state across calls
        print(f"[Predictor] Model loaded from {path}")

    def predict(self, sensor_reading: dict) -> dict:
        """
        Make an alarm decision from a single set of sensor readings.

        Args:
            sensor_reading: dict with keys matching the hardware CSV columns:
                PM1.0, PM2.5, PM10, TVOC, eCO2,
                temperature, humidity, pressure, gas,
                MQ_analog, MQ_digital

        Returns:
            dict:
                alarm      : 'ON' or 'OFF'
                action     : 0 (off) or 1 (on)
                aqi_estimate: float — estimated AQI score (0–500)
                category   : str  — 'GOOD', 'MODERATE', 'UNHEALTHY', etc.
                confidence : float — probability of ALARM_ON [0.0–1.0]
        """
        obs       = self._build_obs(sensor_reading)
        action, _ = self.model.predict(obs, deterministic=True)
        action    = int(action)

        mq_v  = float(sensor_reading.get('MQ_analog', 0)) * 3.3
        aqi   = compute_composite_aqi(
            float(sensor_reading.get('PM2.5', 0)),
            float(sensor_reading.get('PM10',  0)),
            mq_v,
            float(sensor_reading.get('TVOC',  0)),
            float(sensor_reading.get('eCO2',  0)),
        )

        self._prev_action = action

        return {
            'alarm':       'ON' if action == 1 else 'OFF',
            'action':      action,
            'aqi_estimate': round(aqi, 1),
            'category':    get_category(aqi),
        }

    def predict_batch(self, readings: list) -> list:
        """
        Process a list of sensor readings in order (maintains alarm state).
        Useful for replaying historical CSV data.
        """
        self._prev_action = 0
        return [self.predict(r) for r in readings]

    def reset(self):
        """Reset internal alarm state (call at start of new monitoring session)."""
        self._prev_action = 0

    # -----------------------------------------------------------------------
    # Internal helpers
    # -----------------------------------------------------------------------

    def _build_obs(self, r: dict) -> np.ndarray:
        """Convert sensor reading dict → normalised 16-feature obs vector."""
        obs = np.zeros(16, dtype=np.float32)

        obs[0]  = min(float(r.get('PM1.0', 0)) / NORM['PM1.0'][1], 1.0)
        obs[1]  = min(float(r.get('PM2.5', 0)) / NORM['PM2.5'][1], 1.0)
        obs[2]  = min(float(r.get('PM10',  0)) / NORM['PM10'][1],  1.0)
        obs[3]  = min(float(r.get('TVOC',  0)) / NORM['TVOC'][1],  1.0)

        eco2    = max(float(r.get('eCO2', 0)) - NORM['eCO2_min'], 0.0)
        obs[4]  = min(eco2 / NORM['eCO2_range'], 1.0)

        obs[5]  = (float(r.get('temperature', 25)) - (-40)) / 165.0
        obs[6]  = float(r.get('humidity', 50)) / 100.0
        obs[7]  = (float(r.get('pressure', 1000)) - 300) / 800.0

        gas_raw = max(float(r.get('gas', 1_000_000)), 10_000)
        obs[8]  = np.log10(gas_raw) / NORM['gas_log_max']

        obs[9]  = (float(r.get('MQ_analog', 0)) * 3.3) / 3.3
        obs[10] = float(r.get('MQ_digital', 1))

        obs[11] = 1.0   # pms_valid (assumed valid if we received the reading)
        obs[12] = 0.0   # ens160_available (ENS160 disconnected)
        obs[13] = 1.0   # bme680_available

        # AQI estimate from rule-based engine (feature 14)
        mq_v   = float(r.get('MQ_analog', 0)) * 3.3
        aqi    = compute_composite_aqi(
            float(r.get('PM2.5', 0)), float(r.get('PM10', 0)),
            mq_v, float(r.get('TVOC', 0)), float(r.get('eCO2', 0))
        )
        obs[14] = min(aqi / 500.0, 1.0)
        obs[15] = float(self._prev_action)   # current alarm state

        return np.clip(obs, 0.0, 1.0)


# ---------------------------------------------------------------------------
# Standalone test — simulates website calling the predictor
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    print("=" * 55)
    print("  EMBER AI — Predictor Standalone Test")
    print("=" * 55)

    predictor = EmberPredictor()

    # Test with normal clean air readings (from Section 9 of docs)
    test_cases = [
        {
            'name': 'Clean indoor air',
            'data': {
                'PM1.0': 8,   'PM2.5': 15,  'PM10': 18,
                'TVOC': 0,    'eCO2': 0,
                'temperature': 27.3, 'humidity': 18.2,
                'pressure': 990.9,   'gas': 14_523_678.0,
                'MQ_analog': 0.031,  'MQ_digital': 1
            }
        },
        {
            'name': 'Vape smoke peak',
            'data': {
                'PM1.0': 500, 'PM2.5': 709, 'PM10': 812,
                'TVOC': 0,    'eCO2': 0,
                'temperature': 27.5, 'humidity': 18.1,
                'pressure': 990.5,   'gas': 250_000.0,
                'MQ_analog': 0.439,  'MQ_digital': 0   # 0.439 * 3.3 = 1.45V
            }
        },
        {
            'name': 'Decay 30 seconds after vape',
            'data': {
                'PM1.0': 40, 'PM2.5': 55, 'PM10': 68,
                'TVOC': 0,   'eCO2': 0,
                'temperature': 27.4, 'humidity': 18.3,
                'pressure': 990.7,   'gas': 1_200_000.0,
                'MQ_analog': 0.106,  'MQ_digital': 1   # 0.106 * 3.3 = 0.35V
            }
        },
    ]

    for case in test_cases:
        result = predictor.predict(case['data'])
        print(f"\nScenario : {case['name']}")
        print(f"  Alarm  : {result['alarm']}")
        print(f"  AQI    : {result['aqi_estimate']} ({result['category']})")

    print("\n[Predictor] Test complete.")
    print("\nTo use from your partner's server:")
    print("  from predict import EmberPredictor")
    print("  p = EmberPredictor()")
    print("  result = p.predict(sensor_dict)")
    print("  # result['alarm'] == 'ON' → trigger Twilio SMS")
