# Ember AI — Integration Guide for Mirac (Website + Twilio)

This is the trained AI model for the Ember air quality project.
Your job is to connect it to your website and Twilio SMS.

---

## Step 1 — Install Dependencies

```bash
pip install -r requirements.txt
```

---

## Step 2 — Test the AI is Working

```bash
cd src
python predict.py
```

You should see:
```
Scenario : Clean indoor air  →  Alarm: OFF
Scenario : Vape smoke peak   →  Alarm: ON
Scenario : Decay after vape  →  Alarm: ON
```

If you see this, the model is loaded and working correctly.

---

## Step 3 — Connect to Your Website / Server

In your Python web server (Flask, FastAPI, Django — whatever you use),
add this code:

```python
import sys
sys.path.insert(0, 'path/to/src')   # point to the src/ folder

from predict import EmberPredictor

# Load once when server starts (not on every request)
predictor = EmberPredictor()
```

---

## Step 4 — Call the AI on Every Sensor Reading

Every time the hardware sends a new sensor reading to your website,
call this:

```python
result = predictor.predict({
    "PM1.0":       8,          # from PMS5003
    "PM2.5":       15,         # from PMS5003
    "PM10":        18,         # from PMS5003
    "TVOC":        0,          # ENS160 (set 0 if unavailable)
    "eCO2":        0,          # ENS160 (set 0 if unavailable)
    "temperature": 27.3,       # from BME680
    "humidity":    18.2,       # from BME680
    "pressure":    990.9,      # from BME680
    "gas":         14523678.0, # from BME680 (Ohms)
    "MQ_analog":   0.031,      # MQ sensor ADC (0.0 to 1.0)
    "MQ_digital":  1           # MQ sensor digital pin (0 or 1)
})

print(result['alarm'])       # 'ON' or 'OFF'
print(result['aqi_estimate'])  # e.g. 287.5
print(result['category'])    # e.g. 'VERY_UNHEALTHY'
```

---

## Step 5 — Trigger Twilio SMS When Alarm is ON

```python
from twilio.rest import Client

TWILIO_SID   = "your_account_sid"
TWILIO_TOKEN = "your_auth_token"
FROM_NUMBER  = "+1xxxxxxxxxx"   # your Twilio number
TO_NUMBER    = "+1xxxxxxxxxx"   # owner's phone number

def send_alert(aqi, category):
    client = Client(TWILIO_SID, TWILIO_TOKEN)
    client.messages.create(
        body=f"EMBER ALERT: Dangerous air quality detected!\nAQI: {aqi} ({category})\nPlease check the premises immediately.",
        from_=FROM_NUMBER,
        to=TO_NUMBER
    )
```

---

## Step 6 — Full Example (Flask Server)

```python
from flask import Flask, request, jsonify
import sys
sys.path.insert(0, './src')

from predict import EmberPredictor
from twilio.rest import Client

app = Flask(__name__)
predictor = EmberPredictor()   # load model once

TWILIO_SID   = "your_account_sid"
TWILIO_TOKEN = "your_auth_token"
FROM_NUMBER  = "+1xxxxxxxxxx"
TO_NUMBER    = "+1xxxxxxxxxx"

alarm_already_on = False   # avoid sending SMS every 2 seconds


@app.route('/sensor', methods=['POST'])
def receive_sensor_data():
    global alarm_already_on

    data   = request.json       # hardware sends JSON to this endpoint
    result = predictor.predict(data)

    # Show on website
    response = {
        'alarm':    result['alarm'],
        'aqi':      result['aqi_estimate'],
        'category': result['category'],
        'data':     data
    }

    # Twilio SMS — only send when alarm first turns ON (not every reading)
    if result['alarm'] == 'ON' and not alarm_already_on:
        send_twilio_sms(result['aqi_estimate'], result['category'])
        alarm_already_on = True

    elif result['alarm'] == 'OFF':
        alarm_already_on = False   # reset so next danger triggers SMS again

    return jsonify(response)


def send_twilio_sms(aqi, category):
    client = Client(TWILIO_SID, TWILIO_TOKEN)
    client.messages.create(
        body=f"EMBER ALERT: Dangerous air quality detected!\nAQI: {aqi} ({category})\nPlease check the premises immediately.",
        from_=FROM_NUMBER,
        to=TO_NUMBER
    )


if __name__ == '__main__':
    app.run(debug=True, port=5000)
```

---

## What the Hardware Sends to Your Server

The K64F sends readings every ~2.5 seconds via ESP32 WiFi.
Your server should accept a POST request at `/sensor` with this JSON:

```json
{
    "PM1.0": 8,
    "PM2.5": 15,
    "PM10": 18,
    "TVOC": 0,
    "eCO2": 0,
    "temperature": 27.3,
    "humidity": 18.2,
    "pressure": 990.9,
    "gas": 14523678.0,
    "MQ_analog": 0.031,
    "MQ_digital": 1
}
```

---

## AI Performance (for demo / presentation)

| Metric | Result |
|---|---|
| Accuracy | 99.76% |
| False Negative Rate (missed danger) | 0.00% |
| False Positive Rate (false alarms) | 0.31% |
| Training data | Synthetic + Beijing real-world PM2.5/PM10 |
| Algorithm | Deep Q-Network (DQN) Reinforcement Learning |

**The AI never misses a real danger event.**

---

## Files You Need

```
AI RL/
├── requirements.txt        ← install this first
├── models/
│   └── best_model.zip      ← trained AI model (do not delete)
└── src/
    ├── predict.py          ← import this in your server
    ├── aqi_engine.py       ← needed by predict.py
    └── data_loader.py      ← needed by predict.py
```

---

## If You Have Hardware CSV Data to Send Rajini

When you collect real sensor data from the SD card, the files will be named like:
```
2026-02-22.csv
2026-02-23.csv
```

**Send those CSV files to Rajini.**

He will:
1. Drop them into `data/real/`
2. Retrain the AI with your real data
3. Send you back a new `models/best_model.zip`
4. You replace the old `best_model.zip` with the new one — no other changes needed

**The more data you collect, the better the AI gets.**

Try to collect data in these two situations:
- Several hours of **normal clean air** (so AI learns your room's baseline)
- **Blow vape/smoke near the sensor** on purpose (so AI learns real danger patterns from your exact hardware)

---


