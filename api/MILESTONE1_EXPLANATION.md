# Milestone 1 — RL Model REST API on Render.com
**Demo:** March 12, 2–3 PM, Room A3058
**Worth:** 1%

---

## What I Built

A Flask REST API server that runs the trained AI model online.
Anyone can send sensor readings to it via HTTP and get back an alarm decision.

The server is deployed on **Render.com** (not Vercel — Vercel's 50MB limit
is too small for PyTorch + stable-baselines3 which are ~500MB).

---

## The 3 Endpoints

### 1. POST /predict — Ask the AI

**What it does:** Receives sensor readings, runs the DQN model, returns alarm decision.

```
POST https://your-app.onrender.com/predict
Content-Type: application/json

{
    "PM1.0":       8,
    "PM2.5":       15,
    "PM10":        18,
    "TVOC":        0,
    "eCO2":        0,
    "temperature": 27.3,
    "humidity":    18.2,
    "pressure":    990.9,
    "gas":         14523678.0,
    "MQ_analog":   0.031,
    "MQ_digital":  1
}
```

Response:
```json
{
    "alarm":     "ON",
    "aqi":       287.5,
    "category":  "VERY_UNHEALTHY",
    "timestamp": "2026-03-12 14:23:05"
}
```

### 2. GET /history — See Last 50 Predictions

**What it does:** Returns the last 50 predictions stored in the SQLite database.

```
GET https://your-app.onrender.com/history
```

Response:
```json
{
    "count": 50,
    "predictions": [
        {
            "id": 50,
            "timestamp": "2026-03-12 14:23:05",
            "pm25": 709.0,
            "alarm": "ON",
            "aqi": 500.0,
            "category": "HAZARDOUS"
        },
        ...
    ]
}
```

### 3. GET /status — Server Health Check

**What it does:** Shows server is online, how long it's been running, model version.

```
GET https://your-app.onrender.com/status
```

Response:
```json
{
    "status":             "online",
    "model_version":      "1.0.0",
    "uptime_seconds":     3742,
    "total_predictions":  128,
    "server_time":        "2026-03-12 14:23:05"
}
```

---

## Files Created for This Milestone

| File | What it does |
|---|---|
| `api/server.py` | Flask web server — the main API |
| `api/database.py` | SQLite logging — stores every prediction |
| `api/__init__.py` | Makes `api/` a Python package |
| `api/simulate_hardware.py` | Test script — simulates K64F hardware |
| `Procfile` | Tells Render.com how to start the server |
| `requirements.txt` | Updated to include Flask, gunicorn, flask-cors |

---

## How to Test Locally (Before Render.com)

### Step 1 — Install new packages
```bash
pip install flask flask-cors gunicorn requests
```

### Step 2 — Start the server
```bash
cd "AI RL"
python -m api.server
```

You should see:
```
[Server] Loading Ember AI model...
[Predictor] Model loaded from .../models/best_model.zip
[Server] Model ready. Starting Flask server.
[Server] Running locally on http://localhost:5000
```

### Step 3 — Run the hardware simulator (new terminal)
```bash
cd "AI RL"
python -m api.simulate_hardware
```

You should see:
```
[OK] Server is online — model v1.0.0

--- Reading 1/7: 1 — Clean indoor air (before vape) ---
  Alarm: OFF   AQI=57.4  (MODERATE)

--- Reading 4/7: 4 — PEAK — vape smoke fully detected ---
  *** ALARM: ON ***   AQI=500.0  (HAZARDOUS)

--- Reading 7/7: 7 — Back to normal (clean air) ---
  Alarm: OFF   AQI=61.0  (MODERATE)
```

### Step 4 — Test the endpoints manually in a browser
- Open: `http://localhost:5000/status`
- Open: `http://localhost:5000/history`

Or use curl:
```bash
curl -X POST http://localhost:5000/predict \
     -H "Content-Type: application/json" \
     -d '{"PM2.5": 709, "PM10": 812, "PM1.0": 500, "MQ_analog": 0.439, "MQ_digital": 0, "temperature": 27.5, "humidity": 18.1, "pressure": 990.5, "gas": 250000, "TVOC": 0, "eCO2": 0}'
```

---

## How to Deploy to Render.com

### Step 1 — Push your code to GitHub
```bash
git add api/ Procfile requirements.txt
git commit -m "Milestone 1: Flask REST API for Ember AI"
git push
```

### Step 2 — Create account on Render.com
Go to https://render.com → Sign up with GitHub (free).

### Step 3 — Create a new Web Service
1. Click **"New"** → **"Web Service"**
2. Connect your GitHub repo
3. Set these settings:
   - **Name:** `ember-ai` (or anything you like)
   - **Runtime:** Python 3
   - **Build Command:** `pip install -r requirements.txt`
   - **Start Command:** `gunicorn api.server:app`
   - **Plan:** Free

4. Click **"Create Web Service"**

### Step 4 — Wait for deployment (~3–5 minutes)
Render.com will:
1. Clone your GitHub repo
2. Install all packages from requirements.txt
3. Start the server with gunicorn
4. Give you a public URL like: `https://ember-ai.onrender.com`

### Step 5 — Test the live URL
```bash
python -m api.simulate_hardware --url https://ember-ai.onrender.com
```

---

## What to Show the Professor at Demo

1. **Open browser → `https://your-app.onrender.com/status`**
   - Shows server is online with uptime

2. **Show `POST /predict` with a danger reading:**
   Use simulate_hardware.py or curl. Show the alarm turning ON with AQI=500.

3. **Open browser → `https://your-app.onrender.com/history`**
   - Shows the last 50 predictions with timestamps in the database

4. **Say this:**
   > "I deployed the trained DQN reinforcement learning model as a live REST API
   > on Render.com. The API accepts sensor readings via HTTP POST, runs the AI
   > inference in milliseconds, and returns the alarm decision. Every prediction
   > is stored in a SQLite database. The GET /history endpoint retrieves the last
   > 50 predictions with timestamps. The GET /status endpoint confirms the server
   > uptime and model version. The server is accessible from any browser or device
   > via a public URL — this is how Mirac's hardware will connect to my AI."

---

## Completion Checklist

- [ ] `POST /predict` receives sensor JSON and returns `{ "alarm": "ON/OFF", "aqi": X }`
- [ ] API accessible via public Render.com URL from any browser
- [ ] All predictions stored in SQLite with timestamp
- [ ] `GET /history` returns last 50 logged predictions
- [ ] `GET /status` returns server uptime and model version
