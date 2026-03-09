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
ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB_PATH = os.path.join(ROOT, 'predictions.db')


def init_db():
    """Create the predictions table if it doesn't exist yet."""
    conn = sqlite3.connect(DB_PATH)
    c    = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS predictions (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   TEXT    NOT NULL,
            pm25        REAL,
            pm10        REAL,
            temperature REAL,
            humidity    REAL,
            mq_analog   REAL,
            alarm       TEXT    NOT NULL,
            aqi         REAL,
            category    TEXT,
            raw_input   TEXT
        )
    ''')
    conn.commit()
    conn.close()


def log_prediction(sensor_data: dict, result: dict):
    """
    Save one prediction to the database.

    Args:
        sensor_data : the raw sensor reading dict from the hardware
        result      : the dict returned by EmberPredictor.predict()
    """
    conn = sqlite3.connect(DB_PATH)
    c    = conn.cursor()
    c.execute('''
        INSERT INTO predictions
            (timestamp, pm25, pm10, temperature, humidity, mq_analog,
             alarm, aqi, category, raw_input)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', (
        datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S'),
        float(sensor_data.get('PM2.5',      0)),
        float(sensor_data.get('PM10',       0)),
        float(sensor_data.get('temperature', 0)),
        float(sensor_data.get('humidity',    0)),
        float(sensor_data.get('MQ_analog',   0)),
        result['alarm'],
        result['aqi_estimate'],
        result['category'],
        json.dumps(sensor_data),
    ))
    conn.commit()
    conn.close()


def get_history(limit: int = 50) -> list:
    """
    Return the last `limit` predictions, newest first.

    Returns a list of dicts — each dict is one row.
    """
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row          # makes rows behave like dicts
    c    = conn.cursor()
    c.execute('''
        SELECT id, timestamp, pm25, pm10, temperature, humidity,
               mq_analog, alarm, aqi, category
        FROM   predictions
        ORDER  BY id DESC
        LIMIT  ?
    ''', (limit,))
    rows = [dict(row) for row in c.fetchall()]
    conn.close()
    return rows


def get_prediction_count() -> int:
    """Return total number of predictions stored."""
    conn = sqlite3.connect(DB_PATH)
    c    = conn.cursor()
    c.execute('SELECT COUNT(*) FROM predictions')
    count = c.fetchone()[0]
    conn.close()
    return count
