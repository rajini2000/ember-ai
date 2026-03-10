/*
 * ember_training_logger.c
 * Ember AI — Milestone 1: RL Training Data Logger
 *
 * Computes 16 RL features from live sensor readings and writes
 * labeled CSV rows to training_data.csv on microSD every 2.5s.
 *
 * Serial output each cycle:
 *   [LOGGER] Label=SAFE(0)  LED=GREEN
 *     PM:  1.0=8.0  2.5=15.0  10=18.0
 *     ENV: T=27.3  H=18.2  P=990.9  Gas=14523678
 *     MQ:  A=0.031  D=0
 *     DRV: AQI=1  dPM25=2.1  dPM10=1.5  THI=30.3  GasR=0.0000022
 *   [SD]   Row written OK
 */

#include "ember_training_logger.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── CSV filename on SD card ──────────────────────────────── */
#define TRAINING_CSV  "training_data.csv"

/* ── CSV header ───────────────────────────────────────────── */
static const char CSV_HEADER[] =
    "timestamp,pm1,pm2_5,pm10,temp,hum,pres,gas,mq_a,mq_d,"
    "tvoc,eco2,aqi_cat,delta_pm25,delta_pm10,thi,gas_ratio,label\n";

/* ── AQI category thresholds (US EPA PM2.5) ──────────────── */
static int compute_aqi_category(float pm2_5)
{
    if      (pm2_5 <  12.1f) return 0;   /* GOOD           */
    else if (pm2_5 <  35.5f) return 1;   /* MODERATE       */
    else if (pm2_5 <  55.5f) return 2;   /* USG            */
    else if (pm2_5 < 150.5f) return 3;   /* UNHEALTHY      */
    else if (pm2_5 < 250.5f) return 4;   /* VERY UNHEALTHY */
    else                     return 5;   /* HAZARDOUS      */
}

/* ── Public: ember_logger_init ────────────────────────────── */
int ember_logger_init(void)
{
    int rc = sd_append_line(TRAINING_CSV, CSV_HEADER);
    if (rc != 0) {
        printf("[LOGGER] ERROR: Could not open %s on SD card (rc=%d)\r\n",
               TRAINING_CSV, rc);
        return LOGGER_ERR_OPEN;
    }
    printf("[LOGGER] Initialized. Writing to %s\r\n", TRAINING_CSV);
    printf("[LOGGER] SW2=DANGER(red)  SW3=SAFE(green)\r\n\r\n");
    return LOGGER_OK;
}

/* ── Public: ember_logger_compute_features ────────────────── */
void ember_logger_compute_features(const RawSensors_t *raw,
                                    float prev_pm2_5,
                                    float prev_pm10,
                                    RLFeatures_t *out)
{
    /* Features 0–10: raw passthrough */
    out->pm1_0        = raw->pm1_0;
    out->pm2_5        = raw->pm2_5;
    out->pm10         = raw->pm10;
    out->temperature  = raw->temperature;
    out->humidity     = raw->humidity;
    out->pressure     = raw->pressure;
    out->gas_resistance = raw->gas_resistance;
    out->mq_analog    = raw->mq_analog;
    out->mq_digital   = raw->mq_digital;
    out->tvoc         = raw->tvoc;
    out->eco2         = raw->eco2;

    /* Feature 11: AQI category from PM2.5 */
    out->aqi_category = compute_aqi_category(raw->pm2_5);

    /* Feature 12: PM2.5 rate of change */
    out->delta_pm25 = raw->pm2_5 - prev_pm2_5;

    /* Feature 13: PM10 rate of change */
    out->delta_pm10 = raw->pm10 - prev_pm10;

    /* Feature 14: Temperature-Humidity Index */
    out->thi = raw->temperature + (0.33f * raw->humidity) - 4.0f;

    /* Feature 15: Gas ratio — MQ voltage relative to BME680 gas resistance
     * gas_resistance can be 0 on first read — guard against log10(0) */
    if (raw->gas_resistance > 1.0f) {
        float log_gas = (float)log10((double)raw->gas_resistance);
        out->gas_ratio = (log_gas > 0.0f) ? (raw->mq_analog / log_gas) : 0.0f;
    } else {
        out->gas_ratio = 0.0f;
    }
}

/* ── Public: ember_logger_write_row ───────────────────────── */
int ember_logger_write_row(const char *timestamp,
                            const RLFeatures_t *f,
                            int label)
{
    char line[256];
    int n = snprintf(line, sizeof(line),
        "%s,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%.0f,%.4f,%d,"
        "%.1f,%.1f,%d,%.2f,%.2f,%.2f,%.8f,%d\n",
        timestamp,
        f->pm1_0, f->pm2_5, f->pm10,
        f->temperature, f->humidity, f->pressure,
        f->gas_resistance,
        f->mq_analog, f->mq_digital,
        f->tvoc, f->eco2,
        f->aqi_category,
        f->delta_pm25, f->delta_pm10,
        f->thi, f->gas_ratio,
        label
    );

    if (n <= 0 || n >= (int)sizeof(line)) {
        printf("[SD]   ERROR: Line buffer overflow\r\n");
        return LOGGER_ERR_SD;
    }

    int rc = sd_append_line(TRAINING_CSV, line);
    if (rc != 0) {
        printf("[SD]   ERROR: Write failed (rc=%d)\r\n", rc);
        return LOGGER_ERR_SD;
    }

    printf("[SD]   Row written to %s\r\n", TRAINING_CSV);
    return LOGGER_OK;
}

/* ── Public: ember_logger_check_buttons ───────────────────── */
int ember_logger_check_buttons(int current_label)
{
    if (sw2_pressed()) {
        current_label = LABEL_DANGER;
        rgb_led_set(1, 0, 0);   /* red */
        printf("[LOGGER] *** SW2 PRESSED — switching to DANGER label ***\r\n");
    } else if (sw3_pressed()) {
        current_label = LABEL_SAFE;
        rgb_led_set(0, 1, 0);   /* green */
        printf("[LOGGER] *** SW3 PRESSED — switching to SAFE label ***\r\n");
    }
    return current_label;
}

/* ── Public: ember_logger_print_features ─────────────────── */
void ember_logger_print_features(const RLFeatures_t *f, int label)
{
    const char *label_str = (label == LABEL_DANGER) ? "DANGER(1)" : "SAFE(0)";
    const char *led_str   = (label == LABEL_DANGER) ? "RED"       : "GREEN";

    printf("[LOGGER] Label=%-9s  LED=%s\r\n", label_str, led_str);
    printf("  PM:  1.0=%.1f  2.5=%.1f  10=%.1f\r\n",
           f->pm1_0, f->pm2_5, f->pm10);
    printf("  ENV: T=%.1f  H=%.1f  P=%.1f  Gas=%.0f\r\n",
           f->temperature, f->humidity, f->pressure, f->gas_resistance);
    printf("  MQ:  A=%.4f  D=%d\r\n",
           f->mq_analog, f->mq_digital);
    printf("  DRV: AQI=%d  dPM25=%.2f  dPM10=%.2f  THI=%.2f  GasR=%.8f\r\n",
           f->aqi_category, f->delta_pm25, f->delta_pm10,
           f->thi, f->gas_ratio);
}

/* ── Main loop integration example ───────────────────────── */
/*
 * Call ember_training_loop() from your K64F main loop or RTOS task.
 * Replace the stub sensor reads with Mirac's actual driver calls.
 *
 * void ember_training_loop(void)
 * {
 *     static int   label      = LABEL_SAFE;
 *     static float prev_pm2_5 = 0.0f;
 *     static float prev_pm10  = 0.0f;
 *
 *     RawSensors_t  raw;
 *     RLFeatures_t  features;
 *     char          timestamp[24];
 *
 *     // ── Read sensors (replace with Mirac's driver calls) ──
 *     raw.pm1_0          = pms5003_get_pm1();
 *     raw.pm2_5          = pms5003_get_pm25();
 *     raw.pm10           = pms5003_get_pm10();
 *     raw.temperature    = bme680_get_temperature();
 *     raw.humidity       = bme680_get_humidity();
 *     raw.pressure       = bme680_get_pressure();
 *     raw.gas_resistance = bme680_get_gas();
 *     raw.mq_analog      = mq_get_analog();      // 0.0–1.0
 *     raw.mq_digital     = mq_get_digital();     // 0 or 1
 *     raw.tvoc           = 0.0f;                 // ENS160 offline
 *     raw.eco2           = 0.0f;
 *
 *     // ── Check buttons → update label + LED ──
 *     label = ember_logger_check_buttons(label);
 *
 *     // ── Compute 16 features ──
 *     ember_logger_compute_features(&raw, prev_pm2_5, prev_pm10, &features);
 *
 *     // ── Print to serial ──
 *     ember_logger_print_features(&features, label);
 *
 *     // ── Write to SD card ──
 *     get_timestamp(timestamp, sizeof(timestamp));
 *     ember_logger_write_row(timestamp, &features, label);
 *
 *     // ── Save for next cycle's delta computation ──
 *     prev_pm2_5 = raw.pm2_5;
 *     prev_pm10  = raw.pm10;
 *
 *     delay_ms(2500);
 * }
 */
