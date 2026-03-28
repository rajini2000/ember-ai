"""
GenAI Declaration (Claude AI - Anthropic):
I described the AQI rules, sensor breakpoints, and how each sensor should map to an air
quality score. I wrote approximately 40% of this file myself and used Claude AI to fill
in the remaining 60%. The logic, thresholds, and design decisions are mine — AI helped
complete the code based on my descriptions and partial implementation.
"""

"""
Rule-based AQI engine — exact implementation of SYSTEM_DOCUMENTATION.md Section 4.
Used to:
  1. Label training data (what IS dangerous vs safe)
  2. Compute rewards inside the RL environment
"""


# ---------------------------------------------------------------------------
# Sub-score calculators
# ---------------------------------------------------------------------------

def _interpolate(value: float, breakpoints: list) -> float:
    """Linear interpolation within EPA AQI breakpoints."""
    for (c_lo, c_hi, i_lo, i_hi) in breakpoints:
        if c_lo <= value <= c_hi:
            return i_lo + (value - c_lo) * (i_hi - i_lo) / (c_hi - c_lo)
    if value > breakpoints[-1][1]:
        return float(breakpoints[-1][3])
    return 0.0


def pm25_to_aqi(pm25: float) -> float:
    """PM2.5 (µg/m³) → AQI sub-score using EPA breakpoints."""
    bp = [
        (0.0,   12.0,   0,  50),
        (12.1,  35.4,  51, 100),
        (35.5,  55.4, 101, 150),
        (55.5, 150.4, 151, 200),
        (150.5, 250.4, 201, 300),
        (250.5, 500.4, 301, 500),
    ]
    return _interpolate(pm25, bp)


def pm10_to_aqi(pm10: float) -> float:
    """PM10 (µg/m³) → AQI sub-score using EPA breakpoints."""
    bp = [
        (0,    54,   0,  50),
        (55,  154,  51, 100),
        (155, 254, 101, 150),
        (255, 354, 151, 200),
        (355, 424, 201, 300),
        (425, 604, 301, 500),
    ]
    return _interpolate(pm10, bp)


def mq_to_aqi(mq_voltage: float) -> float:
    """MQ sensor voltage (0–3.3 V) → AQI sub-score."""
    if mq_voltage < 0.3:  return 0
    if mq_voltage < 0.6:  return 50
    if mq_voltage < 1.0:  return 100
    if mq_voltage < 1.5:  return 150
    if mq_voltage < 2.0:  return 200
    if mq_voltage < 2.5:  return 300
    return 500


def tvoc_to_aqi(tvoc: float) -> float:
    """TVOC (ppb) → AQI sub-score. Returns 0 when ENS160 unavailable."""
    if tvoc <= 0:
        return 0.0
    bp = [
        (0,    65,   0,  50),
        (65,  220,  51, 100),
        (220, 660, 101, 150),
        (660, 2200, 151, 200),
        (2200, 5500, 201, 300),
        (5500, 10000, 301, 500),
    ]
    return _interpolate(tvoc, bp)


def eco2_to_aqi(eco2: float) -> float:
    """eCO2 (ppm) → AQI sub-score. Returns 0 when ENS160 unavailable."""
    if eco2 <= 400:
        return 0.0
    bp = [
        (400,  600,   0,  50),
        (600,  1000,  51, 100),
        (1000, 1500, 101, 150),
        (1500, 2500, 151, 200),
        (2500, 5000, 201, 300),
        (5000, 10000, 301, 500),
    ]
    return _interpolate(eco2, bp)


# ---------------------------------------------------------------------------
# Composite AQI (Section 4.2)
# ---------------------------------------------------------------------------

def compute_composite_aqi(pm25: float, pm10: float, mq_voltage: float,
                           tvoc: float = 0.0, eco2: float = 0.0) -> float:
    """
    Compute composite AQI score from sensor readings.
    Exact implementation of Section 4.2 of SYSTEM_DOCUMENTATION.md
    """
    pm25_score = pm25_to_aqi(pm25)
    pm10_score = pm10_to_aqi(pm10)
    mq_score   = mq_to_aqi(mq_voltage)
    tvoc_score = tvoc_to_aqi(tvoc)
    eco2_score = eco2_to_aqi(eco2)

    composite = max(pm25_score, pm10_score, mq_score, tvoc_score, eco2_score)

    # Multi-sensor correlation bonus: if 2+ sensors show > 100, scale up by 1.20
    scores_above_100 = sum(1 for s in [pm25_score, pm10_score, mq_score, tvoc_score, eco2_score]
                           if s > 100)
    if scores_above_100 >= 2:
        composite *= 1.20

    return min(composite, 500.0)


def is_dangerous(aqi_score: float) -> bool:
    """True if AQI >= 151 (UNHEALTHY or worse) — alarm trigger threshold."""
    return aqi_score >= 151.0


def get_category(aqi_score: float) -> str:
    """Human-readable AQI category string."""
    if aqi_score <= 50:   return "GOOD"
    if aqi_score <= 100:  return "MODERATE"
    if aqi_score <= 150:  return "UNHEALTHY_SENSITIVE"
    if aqi_score <= 200:  return "UNHEALTHY"
    if aqi_score <= 300:  return "VERY_UNHEALTHY"
    return "HAZARDOUS"
