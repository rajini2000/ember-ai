/*
 * ember_training_logger.h
 * Ember AI — Milestone 1: RL Training Data Logger
 *
 * Computes the 16-feature RL state vector from live sensor readings
 * and writes labeled rows to training_data.csv on microSD every 2.5s.
 *
 * SW2 brief press → label = DANGER (1), LED red
 * SW3 brief press → label = SAFE   (0), LED green
 *
 * CSV format:
 * timestamp,pm1,pm2_5,pm10,temp,hum,pres,gas,mq_a,mq_d,
 * tvoc,eco2,aqi_cat,delta_pm25,delta_pm10,thi,gas_ratio,label
 */

#ifndef EMBER_TRAINING_LOGGER_H
#define EMBER_TRAINING_LOGGER_H

#include <stdint.h>

/* ── Raw sensor input ─────────────────────────────────────── */
typedef struct {
    float pm1_0;            /* PMS5003  µg/m³        */
    float pm2_5;            /* PMS5003  µg/m³        */
    float pm10;             /* PMS5003  µg/m³        */
    float temperature;      /* BME680   °C           */
    float humidity;         /* BME680   %RH          */
    float pressure;         /* BME680   hPa          */
    float gas_resistance;   /* BME680   Ohms         */
    float mq_analog;        /* MQ ADC   0.0–1.0      */
    int   mq_digital;       /* MQ DOUT  0 or 1       */
    float tvoc;             /* ENS160   ppb (0=off)  */
    float eco2;             /* ENS160   ppm (0=off)  */
} RawSensors_t;

/* ── Computed 16-feature RL state vector ──────────────────── */
typedef struct {
    /* Raw passthrough (features 0–10) */
    float pm1_0;
    float pm2_5;
    float pm10;
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
    float mq_analog;
    int   mq_digital;
    float tvoc;
    float eco2;
    /* Derived features (features 11–15) */
    int   aqi_category;     /* 0=GOOD 1=MOD 2=USG 3=UNHEALTHY 4=VERY 5=HAZ */
    float delta_pm25;       /* PM2.5[now] - PM2.5[prev]                      */
    float delta_pm10;       /* PM10[now]  - PM10[prev]                       */
    float thi;              /* temp-humidity index: T + 0.33×H - 4.0         */
    float gas_ratio;        /* mq_analog / log10(gas_resistance)             */
} RLFeatures_t;

/* ── Label state ──────────────────────────────────────────── */
#define LABEL_SAFE    0
#define LABEL_DANGER  1

/* ── Return codes ─────────────────────────────────────────── */
#define LOGGER_OK         0
#define LOGGER_ERR_SD    -1
#define LOGGER_ERR_OPEN  -2

/* ── Public API ───────────────────────────────────────────── */

/**
 * ember_logger_init
 * Opens training_data.csv on SD card and writes the header row.
 * Call once at startup.
 * Returns LOGGER_OK or error code.
 */
int ember_logger_init(void);

/**
 * ember_logger_compute_features
 * Computes the 16 RL features from raw sensor readings.
 * prev_pm2_5 and prev_pm10 are the values from the last cycle
 * (used for delta computation). Pass 0.0 on first call.
 */
void ember_logger_compute_features(const RawSensors_t *raw,
                                    float prev_pm2_5,
                                    float prev_pm10,
                                    RLFeatures_t *out);

/**
 * ember_logger_write_row
 * Appends one row to training_data.csv on SD.
 * timestamp: string like "2026-03-12 14:23:05"
 * label: LABEL_SAFE or LABEL_DANGER
 * Returns LOGGER_OK or error code.
 */
int ember_logger_write_row(const char *timestamp,
                            const RLFeatures_t *features,
                            int label);

/**
 * ember_logger_check_buttons
 * Call each cycle to check SW2/SW3 state.
 * Returns current label (LABEL_SAFE or LABEL_DANGER).
 * Also updates the RGB LED.
 *
 * Implement sw2_pressed() and sw3_pressed() using your GPIO driver.
 */
int ember_logger_check_buttons(int current_label);

/**
 * ember_logger_print_features
 * Prints the 16 features + label to serial terminal.
 */
void ember_logger_print_features(const RLFeatures_t *f, int label);

/* ── Implement these with your K64F drivers ───────────────── */

/** Returns 1 if SW2 was briefly pressed this cycle, 0 otherwise */
int sw2_pressed(void);

/** Returns 1 if SW3 was briefly pressed this cycle, 0 otherwise */
int sw3_pressed(void);

/** Set RGB LED: call with (1,0,0) for red, (0,1,0) for green, (0,0,0) off */
void rgb_led_set(int r, int g, int b);

/** Get current timestamp as string "YYYY-MM-DD HH:MM:SS" */
void get_timestamp(char *buf, int buf_size);

/** SD card: open file, append string, close */
int sd_append_line(const char *filename, const char *line);

#endif /* EMBER_TRAINING_LOGGER_H */
