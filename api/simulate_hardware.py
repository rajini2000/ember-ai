"""
Hardware Simulator — simulates K64F sending sensor readings to the API.

Use this to test the API locally WITHOUT real hardware.
It sends 7 readings (clean air → danger peak → recovery) to the server
every 2.5 seconds, exactly like the real K64F would.

Usage:
    # First, start the server in a separate terminal:
    #   python -m api.server
    #
    # Then run this:
    python -m api.simulate_hardware

    # Or point it at the live Render.com URL:
    python -m api.simulate_hardware --url https://ember-ai.onrender.com
"""

import requests
import time
import json
import argparse

DEFAULT_URL = 'http://localhost:5000'

# ---------------------------------------------------------------------------
# 7 test readings simulating a realistic vape smoke event
# ---------------------------------------------------------------------------
TEST_READINGS = [
    {
        'name': '1 — Clean indoor air (before vape)',
        'data': {
            'PM1.0': 8,    'PM2.5': 15,   'PM10': 18,
            'TVOC':  0,    'eCO2':  0,
            'temperature': 27.3, 'humidity': 18.2,
            'pressure': 990.9,   'gas': 14_523_678.0,
            'MQ_analog': 0.031,  'MQ_digital': 1,
        }
    },
    {
        'name': '2 — Clean indoor air (stable baseline)',
        'data': {
            'PM1.0': 9,    'PM2.5': 17,   'PM10': 20,
            'TVOC':  0,    'eCO2':  0,
            'temperature': 27.3, 'humidity': 18.3,
            'pressure': 991.0,   'gas': 14_200_000.0,
            'MQ_analog': 0.033,  'MQ_digital': 1,
        }
    },
    {
        'name': '3 — Smoke rising (vape starting)',
        'data': {
            'PM1.0': 120,  'PM2.5': 180,  'PM10': 210,
            'TVOC':  0,    'eCO2':  0,
            'temperature': 27.4, 'humidity': 18.2,
            'pressure': 990.8,   'gas': 3_500_000.0,
            'MQ_analog': 0.180,  'MQ_digital': 0,
        }
    },
    {
        'name': '4 — PEAK — vape smoke fully detected',
        'data': {
            'PM1.0': 500,  'PM2.5': 709,  'PM10': 812,
            'TVOC':  0,    'eCO2':  0,
            'temperature': 27.5, 'humidity': 18.1,
            'pressure': 990.5,   'gas': 250_000.0,
            'MQ_analog': 0.439,  'MQ_digital': 0,
        }
    },
    {
        'name': '5 — Still dangerous (PM still high)',
        'data': {
            'PM1.0': 300,  'PM2.5': 450,  'PM10': 520,
            'TVOC':  0,    'eCO2':  0,
            'temperature': 27.5, 'humidity': 18.1,
            'pressure': 990.6,   'gas': 800_000.0,
            'MQ_analog': 0.280,  'MQ_digital': 0,
        }
    },
    {
        'name': '6 — Decay (room ventilating)',
        'data': {
            'PM1.0': 40,   'PM2.5': 55,   'PM10': 68,
            'TVOC':  0,    'eCO2':  0,
            'temperature': 27.4, 'humidity': 18.3,
            'pressure': 990.7,   'gas': 1_200_000.0,
            'MQ_analog': 0.106,  'MQ_digital': 1,
        }
    },
    {
        'name': '7 — Back to normal (clean air)',
        'data': {
            'PM1.0': 10,   'PM2.5': 18,   'PM10': 22,
            'TVOC':  0,    'eCO2':  0,
            'temperature': 27.3, 'humidity': 18.2,
            'pressure': 990.9,   'gas': 12_000_000.0,
            'MQ_analog': 0.040,  'MQ_digital': 1,
        }
    },
]


def simulate(base_url: str):
    print("=" * 60)
    print("  EMBER Hardware Simulator")
    print(f"  Sending to: {base_url}")
    print("=" * 60)

    # First check that the server is running
    try:
        r = requests.get(f'{base_url}/status', timeout=5)
        status = r.json()
        print(f"\n[OK] Server is online — model v{status.get('model_version', '?')}")
        print(f"     Uptime: {status.get('uptime_seconds', 0)}s")
        print(f"     Total predictions: {status.get('total_predictions', 0)}\n")
    except Exception as e:
        print(f"\n[ERROR] Cannot reach server at {base_url}")
        print(f"        Make sure the server is running.")
        print(f"        Error: {e}\n")
        return

    # Send each reading
    for i, reading in enumerate(TEST_READINGS):
        print(f"--- Reading {i+1}/7: {reading['name']} ---")
        try:
            r = requests.post(
                f'{base_url}/predict',
                json=reading['data'],
                timeout=10,
            )
            result = r.json()
            alarm    = result.get('alarm', '?')
            aqi      = result.get('aqi', '?')
            category = result.get('category', '?')
            ts       = result.get('timestamp', '?')

            # Colour output: ON = warning, OFF = safe
            alarm_display = f"*** ALARM: {alarm} ***" if alarm == 'ON' else f"Alarm: {alarm}"
            print(f"  {alarm_display}   AQI={aqi}  ({category})")
            print(f"  Timestamp: {ts}")
            print(f"  PM2.5={reading['data']['PM2.5']}  MQ={reading['data']['MQ_analog']:.3f}V")

        except Exception as e:
            print(f"  [ERROR] {e}")

        print()

        # Wait 2.5 seconds between readings (same as real K64F)
        if i < len(TEST_READINGS) - 1:
            time.sleep(2.5)

    # Show history after all readings
    print("=" * 60)
    print("  Checking /history endpoint...")
    print("=" * 60)
    try:
        r    = requests.get(f'{base_url}/history', timeout=5)
        data = r.json()
        print(f"  Total stored: {data.get('count', 0)} predictions\n")
        for row in data.get('predictions', [])[:7]:
            print(f"  [{row['timestamp']}]  "
                  f"PM2.5={row['pm25']:.0f}  "
                  f"Alarm={row['alarm']}  "
                  f"AQI={row['aqi']:.0f}  "
                  f"({row['category']})")
    except Exception as e:
        print(f"  [ERROR] {e}")

    print("\n[Simulator] Done. All 7 readings sent successfully.")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Ember Hardware Simulator')
    parser.add_argument(
        '--url',
        default=DEFAULT_URL,
        help=f'Base URL of the API server (default: {DEFAULT_URL})'
    )
    args = parser.parse_args()
    simulate(args.url)
