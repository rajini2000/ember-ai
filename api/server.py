"""
Ember AI — Flask REST API Server
Milestone 1: RL Model REST API deployed on Render.com

Endpoints:
  POST /predict  — accepts sensor JSON, returns alarm decision + AQI
  GET  /history  — returns last 50 predictions with timestamps
  GET  /status   — returns server uptime and model version
  GET  /devices  — returns all connected device IDs
  GET  /         — real-time Chart.js dashboard
"""

import sys
import os
import time
from datetime import datetime, timezone

from flask import Flask, request, jsonify, render_template
from flask_cors import CORS

# ---------------------------------------------------------------------------
# Path setup — works both locally and on Render.com
# ---------------------------------------------------------------------------
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR  = os.path.join(BASE_DIR, 'src')
sys.path.insert(0, SRC_DIR)

from predict        import EmberPredictor
from api.database   import init_db, log_prediction, get_history, get_prediction_count, get_registered_devices

# ---------------------------------------------------------------------------
# App setup
# ---------------------------------------------------------------------------
app        = Flask(__name__, template_folder='templates')
CORS(app)                       # allow requests from any browser / domain

START_TIME = time.time()        # track when the server started
MODEL_VERSION = "1.0.0"        # update this when you retrain the model

# Load the AI model ONCE at startup (not on every request — it's slow)
print("[Server] Loading Ember AI model...")
predictor = EmberPredictor()
print("[Server] Model ready. Starting Flask server.")

# Initialise the SQLite database (creates table if it doesn't exist)
init_db()


# ---------------------------------------------------------------------------
# POST /predict — main endpoint
# ---------------------------------------------------------------------------
@app.route('/predict', methods=['POST'])
def predict():
    """
    Accepts sensor readings as JSON, runs the AI, returns alarm decision.

    Expected JSON body (from K64F hardware via ESP32 WiFi):
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

    Returns:
    {
        "alarm":    "ON" or "OFF",
        "aqi":      287.5,
        "category": "VERY_UNHEALTHY",
        "timestamp": "2026-03-12 14:23:05"
    }
    """
    if not request.is_json:
        return jsonify({'error': 'Request must be JSON'}), 400

    sensor_data = request.get_json()

    # Validate that at least PM2.5 is present
    if 'PM2.5' not in sensor_data and 'pm25' not in sensor_data:
        return jsonify({'error': 'Missing required field: PM2.5'}), 400

    # Read device_id (MAC address or device code) — optional field
    device_id = sensor_data.pop('device_id', 'unknown')

    # Run the AI model
    result = predictor.predict(sensor_data)

    # Log to database with device ID
    log_prediction(sensor_data, result, device_id)

    # Build response
    response = {
        'alarm':     result['alarm'],
        'aqi':       result['aqi_estimate'],
        'category':  result['category'],
        'device_id': device_id,
        'timestamp': datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
    }

    return jsonify(response), 200


# ---------------------------------------------------------------------------
# GET /history — returns last 50 predictions
# ---------------------------------------------------------------------------
@app.route('/history', methods=['GET'])
def history():
    """
    Returns the last 50 predictions stored in the database.

    Response:
    {
        "count": 50,
        "predictions": [
            {
                "id": 50,
                "timestamp": "2026-03-12 14:23:05",
                "pm25": 709.0,
                "pm10": 812.0,
                "temperature": 27.5,
                "humidity": 18.1,
                "mq_analog": 0.439,
                "alarm": "ON",
                "aqi": 500.0,
                "category": "HAZARDOUS"
            },
            ...
        ]
    }
    """
    device_id = request.args.get('device_id', None)  # optional filter
    rows = get_history(limit=50, device_id=device_id)
    return jsonify({
        'count':       len(rows),
        'device_id':   device_id or 'all',
        'predictions': rows,
    }), 200


# ---------------------------------------------------------------------------
# GET /status — server uptime and model info
# ---------------------------------------------------------------------------
@app.route('/status', methods=['GET'])
def status():
    """
    Returns server health info.

    Response:
    {
        "status":          "online",
        "model_version":   "1.0.0",
        "uptime_seconds":  3742,
        "total_predictions": 128,
        "server_time":     "2026-03-12 14:23:05"
    }
    """
    uptime = int(time.time() - START_TIME)
    return jsonify({
        'status':             'online',
        'model_version':      MODEL_VERSION,
        'uptime_seconds':     uptime,
        'total_predictions':  get_prediction_count(),
        'server_time':        datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
    }), 200


# ---------------------------------------------------------------------------
# GET /devices — list all devices that have sent data
# ---------------------------------------------------------------------------
@app.route('/devices', methods=['GET'])
def devices():
    """
    Returns all unique device IDs that have sent data to this API.

    Response:
    {
        "devices": [
            {"device_id": "AA:BB:CC:DD:EE:FF", "total_readings": 142},
            {"device_id": "ember-simulator",    "total_readings": 7}
        ]
    }
    """
    return jsonify({'devices': get_registered_devices()}), 200


# ---------------------------------------------------------------------------
# GET /history?device_id=XX — filter history by device
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# GET / — real-time dashboard (HTML page with Chart.js)
# ---------------------------------------------------------------------------
@app.route('/', methods=['GET'])
def dashboard():
    return render_template('dashboard.html')


# ---------------------------------------------------------------------------
# Run locally (not used on Render — Render uses gunicorn)
# ---------------------------------------------------------------------------
if __name__ == '__main__':
    port = int(os.environ.get('PORT', 5000))
    print(f"[Server] Running locally on http://localhost:{port}")
    app.run(host='0.0.0.0', port=port, debug=False)
