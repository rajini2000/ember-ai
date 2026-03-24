"""
Ember AI — Flask REST API Server
Milestone 1: RL Model REST API deployed on Render.com
Milestone 4: Embedded Web Control Panel — Arm, Disarm & Configure

Endpoints:
  POST /predict  — accepts sensor JSON, returns alarm decision + AQI
  GET  /history  — returns last 50 predictions with timestamps
  GET  /status   — returns server uptime and model version
  GET  /devices  — returns all connected device IDs
  GET  /         — real-time Chart.js dashboard
  GET  /control  — web control panel (arm/disarm, thresholds, test alarm)
  POST /command  — web panel sends config commands
  POST /config   — K64F polls for pending config (consumed on read)
"""

import sys
import os
import time
import threading
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
# M4: Device config & command queue  (in-memory, per-device)
# ---------------------------------------------------------------------------
_config_lock = threading.Lock()

# Current known state per device (updated on each /predict call)
device_state = {}

# Pending commands queued by the web control panel, consumed by K64F
# Key = device_id, Value = dict of config fields
pending_commands = {}

# Command log (last 20 commands for the control-panel UI)
command_log = []
MAX_LOG = 20


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

    # Update device state for control panel
    with _config_lock:
        device_state[device_id] = {
            'last_seen': datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
            'alarm':     result['alarm'],
            'aqi':       result['aqi_estimate'],
            'category':  result['category'],
            'sensor_data': {k: v for k, v in sensor_data.items()},  # copy
        }

    # Build response — include any pending config commands
    response = {
        'alarm':     result['alarm'],
        'aqi':       result['aqi_estimate'],
        'category':  result['category'],
        'device_id': device_id,
        'timestamp': datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
    }

    # Attach and consume pending commands (atomic)
    with _config_lock:
        cmds = pending_commands.pop(device_id, None)
        if cmds:
            response['config'] = cmds

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
# M4: POST /command — web control panel pushes config commands
# ---------------------------------------------------------------------------
@app.route('/command', methods=['POST'])
def set_command():
    """
    Accepts JSON from the web control panel and queues it for the K64F.

    Body example:
    {
        "device_id":   "K64F-ember",
        "arm":         true,            // arm or disarm the physical alarm
        "aqi_trigger": 151,             // AQI score that triggers alarm
        "aqi_clear":   100,             // AQI score that clears alarm
        "debounce":    3,               // consecutive cycles required
        "test_alarm":  true             // trigger a 3-second test buzz
    }
    """
    if not request.is_json:
        return jsonify({'error': 'JSON required'}), 400

    data      = request.get_json()
    device_id = data.pop('device_id', 'K64F-ember')

    with _config_lock:
        if device_id not in pending_commands:
            pending_commands[device_id] = {}
        pending_commands[device_id].update(data)

        # Append to command log
        entry = {
            'time':      datetime.now(timezone.utc).strftime('%H:%M:%S'),
            'device_id': device_id,
            'command':   data,
        }
        command_log.append(entry)
        if len(command_log) > MAX_LOG:
            command_log.pop(0)

    return jsonify({'status': 'queued', 'device_id': device_id}), 200


# ---------------------------------------------------------------------------
# M4: POST /config — K64F polls for pending config (consumed on read)
# ---------------------------------------------------------------------------
@app.route('/config', methods=['POST'])
def get_device_config():
    """
    K64F sends a small JSON with its device_id.
    Server returns any queued commands, then clears the queue.
    Returns {} if nothing pending.
    """
    device_id = 'K64F-ember'
    if request.is_json:
        device_id = request.get_json().get('device_id', device_id)

    with _config_lock:
        cmds = pending_commands.pop(device_id, {})

    return jsonify(cmds), 200


# ---------------------------------------------------------------------------
# M4: GET /device_state — current state for control panel display
# ---------------------------------------------------------------------------
@app.route('/device_state', methods=['GET'])
def get_device_state_endpoint():
    did = request.args.get('device_id', None)
    with _config_lock:
        if did:
            state = device_state.get(did, {})
        else:
            state = dict(device_state)
    return jsonify(state), 200


# ---------------------------------------------------------------------------
# M4: GET /command_log — last 20 commands for control panel
# ---------------------------------------------------------------------------
@app.route('/command_log', methods=['GET'])
def get_command_log():
    with _config_lock:
        return jsonify({'log': list(command_log)}), 200


# ---------------------------------------------------------------------------
# M4: GET /control — Embedded Web Control Panel
# ---------------------------------------------------------------------------
@app.route('/control', methods=['GET'])
def control_panel():
    return render_template('control.html')


# ---------------------------------------------------------------------------
# Run locally (not used on Render — Render uses gunicorn)
# ---------------------------------------------------------------------------
if __name__ == '__main__':
    port = int(os.environ.get('PORT', 5000))
    print(f"[Server] Running locally on http://localhost:{port}")
    app.run(host='0.0.0.0', port=port, debug=False)
