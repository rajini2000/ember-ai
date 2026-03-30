/*
 * ember_fire_detection.h — Fire & Smoke Detection with Multi-Sensor Fusion
 *
 * Computes a weighted fire-probability score each cycle from:
 *   - PMS5003 PM2.5/PM10 particle spikes (rate-of-change)
 *   - MQ gas sensor voltage surges (rate-of-change)
 *   - BME680 temperature anomalies (rate-of-change)
 *
 * When the score crosses the fire threshold, enters FIRE ALERT mode.
 * Includes rate-of-change filtering for false-positive suppression.
 *
 * ─── GenAI Declaration (Claude AI - Anthropic) ───
 * Lines 37-50: FireBaselineEntry_t struct — AI-generated
 * Lines 53-61: FireResult_t struct — AI-generated
 * Lines 65-112: Function prototypes and documentation — ~25% AI-assisted
 * All code reviewed and validated on hardware.
 *
 * Usage in main.cpp:
 *   #include "ember/ember_fire_detection.h"
 *
 *   // Once at startup:
 *   ember_fire_init(pc_print);
 *
 *   // Every sensor cycle (after reading sensors):
 *   FireResult_t fire = ember_fire_check(pm25, pm10, mq_analog, temperature);
 *
 *   // fire.alert      — true if FIRE ALERT triggered this cycle
 *   // fire.score      — fire probability score 0.0 - 1.0
 *   // fire.in_alert   — true if currently in FIRE ALERT mode
 */

#ifndef EMBER_FIRE_DETECTION_H
#define EMBER_FIRE_DETECTION_H

#include <cstdint>

/* ── Baseline snapshot (10 cycles pre-incident) ───────────────────────────── */
#define FIRE_BASELINE_SLOTS 10

typedef struct {
    uint16_t pm2_5;
    uint16_t pm10;
    float    mq_analog;
    float    temperature;
    float    humidity;
    float    pressure;
    uint16_t tvoc;
    uint16_t eco2;
    float    gas_res;
    int      mq_digital;
} FireBaselineEntry_t;

/* ── Result returned by ember_fire_check() ────────────────────────────────── */
typedef struct {
    bool  alert;        /* true on the cycle that FIRE ALERT first triggers    */
    bool  in_alert;     /* true while FIRE ALERT mode is active                */
    float score;        /* fire probability 0.0 – 1.0                          */
    float pm_delta;     /* PM2.5 rate-of-change (ug/m3 per cycle)              */
    float mq_delta;     /* MQ analog rate-of-change per cycle                  */
    float temp_delta;   /* Temperature rate-of-change (°C per cycle)           */
} FireResult_t;

/* ── Public API ────────────────────────────────────────────────────────────── */

/**
 * ember_fire_init()
 * Call once at startup. Resets baselines and state.
 *
 * @param print_fn  function pointer to pc_print for serial output
 */
void ember_fire_init(void (*print_fn)(const char*));

/**
 * ember_fire_check()
 * Call every sensor cycle. Computes fire probability score from deltas.
 * Returns FireResult_t with alert status and score breakdown.
 *
 * @param pm2_5        current PM2.5 value (ug/m3)
 * @param pm10         current PM10 value (ug/m3)
 * @param mq_analog    current MQ analog reading (0.0 – 1.0)
 * @param temperature  current temperature (°C)
 * @param humidity     current humidity (%)
 * @param pressure     current pressure (hPa)
 * @param tvoc         current TVOC (ppb)
 * @param eco2         current eCO2 (ppm)
 * @param gas_res      current BME680 gas resistance (ohms)
 * @param mq_digital   current MQ digital pin (0=alert, 1=normal)
 */
FireResult_t ember_fire_check(uint16_t pm2_5, uint16_t pm10,
                               float mq_analog, float temperature,
                               float humidity, float pressure,
                               uint16_t tvoc, uint16_t eco2,
                               float gas_res, int mq_digital);

/**
 * ember_fire_get_baseline()
 * Returns pointer to circular baseline buffer for SD logging.
 * Returns count of valid entries.
 */
const FireBaselineEntry_t* ember_fire_get_baseline(int* count);

/**
 * ember_fire_clear_alert()
 * Manually clear fire alert (e.g. after 3-press button dismiss).
 */
void ember_fire_clear_alert(void);

/**
 * ember_fire_is_active()
 * Returns true if fire alert is currently active.
 */
bool ember_fire_is_active(void);

#endif /* EMBER_FIRE_DETECTION_H */
