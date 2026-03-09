"""
Synthetic air quality data generator.

Produces realistic time-series CSV data based on SYSTEM_DOCUMENTATION.md Section 9
(Typical Sensor Values) and Section 3.1 (CSV format).

The generator creates two types of episodes:
  - Clean air  : all readings within normal indoor ranges
  - Pollution  : clean start → rapid rise → peak (vape/smoke) → decay → clean recovery

Run directly to generate data:
    python data_generator.py
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

import numpy as np
import pandas as pd
from datetime import datetime, timedelta
from pathlib import Path

from aqi_engine import compute_composite_aqi, is_dangerous

# ---------------------------------------------------------------------------
# Documented sensor ranges (Section 9)
# ---------------------------------------------------------------------------
CLEAN = {
    # (mean, std, min, max)
    'pm1_0': (8,   2.0,  2,   18),
    'pm2_5': (15,  3.0,  5,   30),
    'pm10':  (20,  4.0,  8,   40),
    'temp':  (24,  2.0,  18,  32),
    'hum':   (28,  8.0,  10,  55),
    'pres':  (1000, 5.0, 980, 1020),
    'gas':   (12_000_000, 3_000_000, 5_000_000, 20_000_000),
    'mq_v':  (0.10, 0.03, 0.05, 0.25),   # actual voltage (V), < 0.3 V = clean
}

PEAK = {
    # (min, max) — vape smoke / danger scenario
    'pm2_5': (400, 1000),
    'pm10':  (500, 1000),
    'mq_v':  (1.0, 3.0),
    'gas':   (50_000, 500_000),
}

SAMPLE_INTERVAL_SEC = 2.5   # ~2s loop + sensor overhead


class SyntheticDataGenerator:
    """
    Generates synthetic sensor readings modelled on the Ember project hardware stack:
    FRDM-K64F + PMS5003 + BME680 + MQ gas sensor.
    """

    def __init__(self, seed: int = 42):
        self.rng = np.random.default_rng(seed)

    # -----------------------------------------------------------------------
    # Public
    # -----------------------------------------------------------------------

    def generate_dataset(self,
                         n_clean: int = 300,
                         n_pollution: int = 200,
                         save_dir: str = None) -> pd.DataFrame:
        """
        Generate a full labelled dataset.

        Args:
            n_clean:     Number of clean-air episodes.
            n_pollution: Number of pollution episodes.
            save_dir:    Directory to save CSV. Skips saving if None.

        Returns:
            pd.DataFrame with all episodes concatenated.
        """
        episodes = []

        for _ in range(n_clean):
            length = int(self.rng.integers(60, 120))
            episodes.append(self._clean_episode(length))

        for _ in range(n_pollution):
            length = int(self.rng.integers(80, 160))
            episodes.append(self._pollution_episode(length))

        df = pd.concat(episodes, ignore_index=True)

        if save_dir:
            Path(save_dir).mkdir(parents=True, exist_ok=True)
            out = os.path.join(save_dir, 'synthetic_training_data.csv')
            df.to_csv(out, index=False)
            print(f"[DataGen] Saved {len(df):,} rows ({n_clean} clean + {n_pollution} pollution episodes) → {out}")

        danger_pct = df['is_dangerous'].mean() * 100
        print(f"[DataGen] Danger rows: {danger_pct:.1f}%  |  Safe rows: {100 - danger_pct:.1f}%")
        return df

    # -----------------------------------------------------------------------
    # Episode builders
    # -----------------------------------------------------------------------

    def _clean_episode(self, length: int) -> pd.DataFrame:
        rows = [self._clean_reading() for _ in range(length)]
        return self._to_dataframe(rows)

    def _pollution_episode(self, length: int) -> pd.DataFrame:
        """
        Realistic pollution event profile:
          [clean_pre] → [rise] → [peak] → [decay] → [clean_post]
        """
        clean_pre  = int(self.rng.integers(15, 35))
        rise_time  = int(self.rng.integers(5,  15))
        peak_hold  = int(self.rng.integers(5,  25))
        decay_time = int(self.rng.integers(15, 40))
        clean_post = max(length - clean_pre - rise_time - peak_hold - decay_time, 10)

        # Choose peak values
        peak_pm25 = float(self.rng.uniform(*PEAK['pm2_5']))
        peak_pm10 = float(self.rng.uniform(*PEAK['pm10']))
        peak_pm1  = peak_pm25 * float(self.rng.uniform(0.60, 0.80))
        peak_mq_v = float(self.rng.uniform(*PEAK['mq_v']))
        peak_gas  = float(self.rng.uniform(*PEAK['gas']))

        rows = []

        # 1. Clean start
        for _ in range(clean_pre):
            rows.append(self._clean_reading())

        # 2. Rise — linear interpolation from clean → peak
        for step in range(rise_time):
            t = (step + 1) / rise_time
            rows.append(self._interpolated(t, peak_pm1, peak_pm25, peak_pm10, peak_mq_v, peak_gas))

        # 3. Peak — noisy readings near peak
        for _ in range(peak_hold):
            rows.append(self._peak_reading(peak_pm1, peak_pm25, peak_pm10, peak_mq_v, peak_gas))

        # 4. Decay — PM decays slower than gas (documented ~10s lag for PM)
        for step in range(decay_time):
            frac = 1.0 - (step + 1) / decay_time
            pm_t  = frac ** 0.7   # slower PM decay
            mq_t  = frac ** 1.4   # faster MQ decay
            gas_t = 1.0 - pm_t    # gas resistance recovers as PM drops
            rows.append(self._interpolated(
                pm_t,
                peak_pm1 * pm_t,
                peak_pm25 * pm_t,
                peak_pm10 * pm_t,
                peak_mq_v * mq_t + CLEAN['mq_v'][0] * (1 - mq_t),
                peak_gas  * (1 - gas_t) + CLEAN['gas'][0] * gas_t
            ))

        # 5. Clean recovery
        for _ in range(clean_post):
            rows.append(self._clean_reading())

        return self._to_dataframe(rows)

    # -----------------------------------------------------------------------
    # Single-reading generators
    # -----------------------------------------------------------------------

    def _clean_reading(self) -> dict:
        mq_v = self._norm(*CLEAN['mq_v'])
        gas  = self._norm(*CLEAN['gas'])
        pm25 = self._norm(*CLEAN['pm2_5'])
        pm10 = self._norm(*CLEAN['pm10'])
        pm1  = self._norm(*CLEAN['pm1_0'])
        return {
            'PM1.0':       pm1,
            'PM2.5':       pm25,
            'PM10':        pm10,
            'TVOC':        0,
            'eCO2':        0,
            'temperature': self._norm(*CLEAN['temp']),
            'humidity':    self._norm(*CLEAN['hum']),
            'pressure':    self._norm(*CLEAN['pres']),
            'gas':         gas,
            'MQ_analog':   mq_v / 3.3,    # ADC value 0.0–1.0
            'MQ_digital':  1,              # below threshold = 1 (clean)
        }

    def _peak_reading(self, pm1, pm25, pm10, mq_v, gas) -> dict:
        base = self._clean_reading()
        noise_pm = float(self.rng.normal(0, 0.05))
        noise_mq = float(self.rng.normal(0, 0.02))
        return {
            **base,
            'PM1.0':      max(pm1  * (1 + noise_pm), 0),
            'PM2.5':      max(pm25 * (1 + noise_pm), 0),
            'PM10':       max(pm10 * (1 + noise_pm), 0),
            'gas':        max(gas  * (1 + float(self.rng.normal(0, 0.05))), 10_000),
            'MQ_analog':  float(np.clip((mq_v + noise_mq) / 3.3, 0.0, 1.0)),
            'MQ_digital': 0 if mq_v > 0.5 else 1,
        }

    def _interpolated(self, t, pm1, pm25, pm10, mq_v, gas) -> dict:
        """Interpolate at fraction t (0=clean baseline, 1=peak values)."""
        base = self._clean_reading()
        return {
            **base,
            'PM1.0':      max(pm1,  0),
            'PM2.5':      max(pm25, 0),
            'PM10':       max(pm10, 0),
            'gas':        max(gas,  10_000),
            'MQ_analog':  float(np.clip(mq_v / 3.3, 0.0, 1.0)),
            'MQ_digital': 0 if mq_v > 0.5 else 1,
        }

    # -----------------------------------------------------------------------
    # Helpers
    # -----------------------------------------------------------------------

    def _norm(self, mean, std, lo, hi) -> float:
        return float(np.clip(self.rng.normal(mean, std), lo, hi))

    def _to_dataframe(self, rows: list) -> pd.DataFrame:
        """Convert list of dicts → DataFrame with timestamps and AQI labels."""
        df = pd.DataFrame(rows)

        # Add timestamps
        start = datetime(2026, 1, 1) + timedelta(
            seconds=float(self.rng.uniform(0, 86400))
        )
        df.insert(0, 'timestamp', [
            start + timedelta(seconds=i * SAMPLE_INTERVAL_SEC)
            for i in range(len(df))
        ])

        # Compute AQI score and danger label
        df['aqi_score'] = df.apply(
            lambda r: compute_composite_aqi(
                r['PM2.5'], r['PM10'], r['MQ_analog'] * 3.3, r['TVOC'], r['eCO2']
            ), axis=1
        )
        df['is_dangerous'] = df['aqi_score'].apply(is_dangerous).astype(int)

        return df


# ---------------------------------------------------------------------------
# Standalone run
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    BASE = os.path.join(os.path.dirname(__file__), '..', 'data', 'synthetic')
    gen = SyntheticDataGenerator(seed=42)
    df  = gen.generate_dataset(n_clean=300, n_pollution=200, save_dir=BASE)
    print(df.head())
    print(f"\nTotal rows: {len(df):,}")
    print(f"Columns   : {list(df.columns)}")
