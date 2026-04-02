"""
Database module — SQLite logging of all predictions.

Every call to POST /predict is stored here with a timestamp.
GET /history reads from here to return the last 50 predictions.
"""

import sqlite3
import os
import json
from datetime import datetime

# Store database at the project root (one level above api/)
ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB_PATH = os.path.join(ROOT, 'predictions.db')


def init_db():
    """Create the predictions table if it doesn't exist yet."""
    conn = sqlite3.connect(DB_PATH)
    c    = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS predictions (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   TEXT    NOT NULL,
            device_id   TEXT    DEFAULT 'unknown',
            pm25        REAL,
            pm10        REAL,
            temperature REAL,
            humidity    REAL,
            mq_analog   REAL,
            alarm       TEXT    NOT NULL,
            aqi         REAL,
            category    TEXT,
            raw_input   TEXT,
            pm1_0       REAL,
            tvoc        REAL,
            eco2        REAL,
            pressure    REAL,
            gas_resistance REAL
        )
    ''')
    # Migration: add columns that may not exist in older databases
    for col, ctype in [('pm1_0', 'REAL'), ('tvoc', 'REAL'), ('eco2', 'REAL'),
                        ('pressure', 'REAL'), ('gas_resistance', 'REAL')]:
        try:
            c.execute(f'ALTER TABLE predictions ADD COLUMN {col} {ctype}')
        except sqlite3.OperationalError:
            pass  # column already exists
    conn.commit()
    conn.close()


def log_prediction(sensor_data: dict, result: dict, device_id: str = 'unknown'):
    """
    Save one prediction to the database.

    Args:
        sensor_data : the raw sensor reading dict from the hardware
        result      : the dict returned by EmberPredictor.predict()
        device_id   : MAC address or device code from the hardware
    """
    conn = sqlite3.connect(DB_PATH)
    c    = conn.cursor()
    c.execute('''
        INSERT INTO predictions
            (timestamp, device_id, pm25, pm10, temperature, humidity, mq_analog,
             alarm, aqi, category, raw_input,
             pm1_0, tvoc, eco2, pressure, gas_resistance)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', (
        datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S'),
        device_id,
        float(sensor_data.get('PM2.5',       0)),
        float(sensor_data.get('PM10',        0)),
        float(sensor_data.get('temperature', 0)),
        float(sensor_data.get('humidity',    0)),
        float(sensor_data.get('MQ_analog',   0)),
        result['alarm'],
        result['aqi_estimate'],
        result['category'],
        json.dumps(sensor_data),
        float(sensor_data.get('PM1.0',       0)),
        float(sensor_data.get('TVOC',        0)),
        float(sensor_data.get('eCO2',        0)),
        float(sensor_data.get('pressure',    0)),
        float(sensor_data.get('gas',         0)),
    ))
    conn.commit()
    conn.close()


def get_history(limit: int = 50, device_id: str = None) -> list:
    """
    Return the last `limit` predictions, newest first.
    Optionally filter by device_id.

    Returns a list of dicts — each dict is one row.
    """
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    c    = conn.cursor()

    if device_id:
        c.execute('''
            SELECT id, timestamp, device_id, pm25, pm10, temperature, humidity,
                   mq_analog, alarm, aqi, category,
                   pm1_0, tvoc, eco2, pressure, gas_resistance
            FROM   predictions
            WHERE  device_id = ?
            ORDER  BY id DESC
            LIMIT  ?
        ''', (device_id, limit))
    else:
        c.execute('''
            SELECT id, timestamp, device_id, pm25, pm10, temperature, humidity,
                   mq_analog, alarm, aqi, category,
                   pm1_0, tvoc, eco2, pressure, gas_resistance
            FROM   predictions
            ORDER  BY id DESC
            LIMIT  ?
        ''', (limit,))

    rows = [dict(row) for row in c.fetchall()]
    conn.close()
    return rows


def get_registered_devices() -> list:
    """Return list of all unique device IDs that have sent data."""
    conn = sqlite3.connect(DB_PATH)
    c    = conn.cursor()
    c.execute('SELECT DISTINCT device_id, COUNT(*) as count FROM predictions GROUP BY device_id')
    devices = [{'device_id': row[0], 'total_readings': row[1]} for row in c.fetchall()]
    conn.close()
    return devices


def get_prediction_count() -> int:
    """Return total number of predictions stored."""
    conn = sqlite3.connect(DB_PATH)
    c    = conn.cursor()
    c.execute('SELECT COUNT(*) FROM predictions')
    count = c.fetchone()[0]
    conn.close()
    return count
