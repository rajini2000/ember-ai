/*
 * ember_calibration.h — M5(Rajini): Embedded Sensor Calibration Routine
 *
 * Triggered by holding onboard SW2 for 3 seconds.
 * Collects baseline readings from all sensors over 60 seconds (24 cycles).
 * Computes per-feature normalization: min, max, mean, standard deviation.
 * Writes calibration.csv to SD card via SPI.
 * On boot, loads calibration.csv and applies to RL inference normalization.
 * RGB LED blinks during calibration, solid green on completion.
 *
 * ─── GenAI Declaration (Claude AI - Anthropic) ───
 * Lines 38-51: Feature index #defines — developer-authored
 * Lines 54-59: CalibFeature_t struct — AI-assisted
 * Lines 62-65: CalibParams_t struct — AI-assisted
 * Lines 70-104: Function prototypes and documentation — ~30% AI-assisted
 * All code reviewed and validated on hardware.
 *
 * Usage in main.cpp:
 *   #include "ember/ember_calibration.h"
 *
 *   // At boot (after SD init):
 *   ember_calibration_load(sd_ready, pc_print);
 *   ember_inference_set_calib(ember_calibration_get());
 *
 *   // Every sensor cycle:
 *   ember_calibration_check(pm1_0, pm2_5, pm10, temp, hum, press,
 *       gas_res, mq_analog, mq_digital, tvoc, eco2,
 *       sd_ready, led_red, led_green, led_blue, pc_print);
 *   ember_inference_set_calib(ember_calibration_get());
 */

#ifndef EMBER_CALIBRATION_H
#define EMBER_CALIBRATION_H

#include "mbed.h"
#include <cstdint>

/* ── Number of sensor features calibrated ─────────────────────────────────── */
#define CALIB_NUM_FEATURES 11

/* Feature indices */
#define CALIB_PM1_0      0
#define CALIB_PM2_5      1
#define CALIB_PM10       2
#define CALIB_TEMP       3
#define CALIB_HUM        4
#define CALIB_PRESS      5
#define CALIB_GAS_RES    6
#define CALIB_MQ_ANALOG  7
#define CALIB_MQ_DIGITAL 8
#define CALIB_TVOC       9
#define CALIB_ECO2      10

/* ── Per-feature calibration parameters ───────────────────────────────────── */
typedef struct {
    float min_val;
    float max_val;
    float mean_val;
    float std_val;
} CalibFeature_t;

/* ── Full calibration parameter set ───────────────────────────────────────── */
typedef struct CalibParams_tag {
    bool          valid;                          /* true if loaded/computed   */
    CalibFeature_t features[CALIB_NUM_FEATURES];  /* per-feature stats         */
} CalibParams_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Load calibration.csv from SD card (if present).
 * Call once at boot, after SD card is initialized.
 */
void ember_calibration_load(bool sd_ready, void (*print_fn)(const char*));

/**
 * Get pointer to current calibration parameters.
 * Returns pointer to static internal struct (valid == false if not loaded).
 */
const CalibParams_t* ember_calibration_get(void);

/**
 * Called every sensor cycle in the main loop.
 * Detects SW2 held for 3 seconds, then runs 60-second calibration routine.
 * Writes results to SD, blinks LEDs during calibration.
 *
 * @param pm1_0 ... eco2  — current sensor readings
 * @param sd_ready         — true if SD card is available
 * @param led_red/green/blue — LED outputs for visual feedback
 * @param print_fn         — serial print function
 */
void ember_calibration_check(
    uint16_t pm1_0, uint16_t pm2_5, uint16_t pm10,
    float temperature, float humidity, float pressure, float gas_res,
    float mq_analog, int mq_digital,
    uint16_t tvoc, uint16_t eco2,
    bool sd_ready,
    DigitalOut &led_red, DigitalOut &led_green, DigitalOut &led_blue,
    void (*print_fn)(const char*));

/**
 * Trigger calibration from serial command.
 * Call this when user types "CALIB" in serial terminal.
 */
void ember_calibration_trigger(void);

#endif /* EMBER_CALIBRATION_H */
