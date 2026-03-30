/*
 * ember_calibration.cpp — M5(Rajini): Embedded Sensor Calibration Routine
 *
 * Hold SW2 (PTC6) for 3 seconds to enter calibration mode.
 * Collects 24 samples over ~60 seconds (2.5s per cycle).
 * Computes min, max, mean, std for each of 11 sensor features.
 * Saves to /sd/calibration.csv on microSD via SPI.
 * On boot, loads calibration.csv and provides to RL inference.
 *
 * ─── GenAI Declaration (Claude AI - Anthropic & GitHub Copilot) ───
 * Lines 47-120: ember_calibration_load() SD card CSV parser — AI-generated
 * Lines 130-155: save_calibration_to_sd() CSV writer — AI-generated
 * Lines 159-210: run_calibration() statistics computation (min/max/mean/
 *   std) — AI-assisted (~70%)
 * Lines 223-355: ember_calibration_check() sample collection state
 *   machine — AI-assisted (~50%)
 * Lines 356-359: ember_calibration_trigger() — AI-generated
 *
 * We designed the calibration workflow, sample count, and feature list.
 * AI assisted with the statistics math, CSV file I/O, and state machine
 * scaffolding. Approximately 55% of this file is AI-assisted.
 * All code reviewed and validated on hardware.
 */

#include "ember_calibration.h"
#include <cstring>
#include <cstdio>
#include <cmath>

/* ── Configuration ────────────────────────────────────────────────────────── */
#define CALIB_HOLD_MS        2000     /* SW2 hold time to trigger (2 seconds)  */
#define CALIB_SAMPLE_COUNT   24       /* Number of baseline samples             */
#define CALIB_CYCLE_MS       2500     /* Time between samples (2.5 seconds)     */
#define CALIB_FILE_PATH      "/sd/calibration.csv"

/* ── Internal state ───────────────────────────────────────────────────────── */
static CalibParams_t calib_params = {};  /* .valid = false initially */
static void (*_print)(const char*) = nullptr;

/* Feature names for printing */
static const char* feature_names[CALIB_NUM_FEATURES] = {
    "PM1.0", "PM2.5", "PM10", "Temp", "Humidity",
    "Pressure", "GasRes", "MQ_Analog", "MQ_Digital", "TVOC", "eCO2"
};

/* ── Helper: print to serial ──────────────────────────────────────────────── */
static void cprint(const char* msg) {
    if (_print) _print(msg);
}

/* ============================================================================
 * Load calibration from SD card
 * ========================================================================== */
void ember_calibration_load(bool sd_ready, void (*print_fn)(const char*))
{
    _print = print_fn;
    calib_params.valid = false;

    if (!sd_ready) {
        cprint("[CALIB] SD not ready — skipping calibration load\r\n");
        return;
    }

    FILE* fp = fopen(CALIB_FILE_PATH, "r");
    if (!fp) {
        cprint("[CALIB] No calibration.csv found — using defaults\r\n");
        return;
    }

    cprint("[CALIB] Loading calibration.csv from SD...\r\n");

    /* CSV format: feature_index,min,max,mean,std */
    char line[128];
    int loaded = 0;

    /* Skip header line */
    if (fgets(line, sizeof(line), fp) == nullptr) {
        fclose(fp);
        return;
    }

    while (fgets(line, sizeof(line), fp) && loaded < CALIB_NUM_FEATURES) {
        int idx;
        float fmin, fmax, fmean, fstd;
        if (sscanf(line, "%d,%f,%f,%f,%f", &idx, &fmin, &fmax, &fmean, &fstd) == 5) {
            if (idx >= 0 && idx < CALIB_NUM_FEATURES) {
                calib_params.features[idx].min_val  = fmin;
                calib_params.features[idx].max_val  = fmax;
                calib_params.features[idx].mean_val = fmean;
                calib_params.features[idx].std_val  = fstd;
                loaded++;
            }
        }
    }
    fclose(fp);

    if (loaded == CALIB_NUM_FEATURES) {
        calib_params.valid = true;
        cprint("[CALIB] Calibration loaded OK — ");

        char msg[96];
        snprintf(msg, sizeof(msg), "%d features loaded\r\n", loaded);
        cprint(msg);

        /* Print loaded parameters */
        for (int i = 0; i < CALIB_NUM_FEATURES; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "[CALIB]   %s: min=%.2f max=%.2f mean=%.2f std=%.2f\r\n",
                feature_names[i],
                calib_params.features[i].min_val,
                calib_params.features[i].max_val,
                calib_params.features[i].mean_val,
                calib_params.features[i].std_val);
            cprint(buf);
        }
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg),
            "[CALIB] Incomplete calibration — only %d/%d features\r\n",
            loaded, CALIB_NUM_FEATURES);
        cprint(msg);
    }
}

/* ============================================================================
 * Get calibration parameters
 * ========================================================================== */
const CalibParams_t* ember_calibration_get(void)
{
    return &calib_params;
}

/* ============================================================================
 * Save calibration to SD card
 * ========================================================================== */
static bool save_calibration_to_sd(void)
{
    FILE* fp = fopen(CALIB_FILE_PATH, "w");
    if (!fp) {
        cprint("[CALIB] ERROR: Cannot open calibration.csv for writing\r\n");
        return false;
    }

    /* Write header */
    fprintf(fp, "feature_idx,min,max,mean,std\n");

    /* Write each feature */
    for (int i = 0; i < CALIB_NUM_FEATURES; i++) {
        fprintf(fp, "%d,%.4f,%.4f,%.4f,%.4f\n",
            i,
            calib_params.features[i].min_val,
            calib_params.features[i].max_val,
            calib_params.features[i].mean_val,
            calib_params.features[i].std_val);
    }

    fclose(fp);
    return true;
}

/* ============================================================================
 * Run the calibration routine (blocking ~60 seconds)
 * Called when SW2 3-second hold detected.
 * ========================================================================== */
static void run_calibration(
    uint16_t pm1_0, uint16_t pm2_5, uint16_t pm10,
    float temperature, float humidity, float pressure, float gas_res,
    float mq_analog, int mq_digital,
    uint16_t tvoc, uint16_t eco2,
    bool sd_ready,
    DigitalOut &led_red, DigitalOut &led_green, DigitalOut &led_blue)
{
    cprint("\r\n[CALIB] ============================================\r\n");
    cprint("[CALIB]   SENSOR CALIBRATION MODE ACTIVATED\r\n");
    cprint("[CALIB]   Collecting baseline for 60 seconds...\r\n");
    cprint("[CALIB] ============================================\r\n");

    /* Storage for samples */
    float samples[CALIB_NUM_FEATURES][CALIB_SAMPLE_COUNT];
    memset(samples, 0, sizeof(samples));

    /* First sample is the current reading */
    samples[CALIB_PM1_0][0]     = (float)pm1_0;
    samples[CALIB_PM2_5][0]     = (float)pm2_5;
    samples[CALIB_PM10][0]      = (float)pm10;
    samples[CALIB_TEMP][0]      = temperature;
    samples[CALIB_HUM][0]       = humidity;
    samples[CALIB_PRESS][0]     = pressure;
    samples[CALIB_GAS_RES][0]   = gas_res;
    samples[CALIB_MQ_ANALOG][0] = mq_analog;
    samples[CALIB_MQ_DIGITAL][0]= (float)mq_digital;
    samples[CALIB_TVOC][0]      = (float)tvoc;
    samples[CALIB_ECO2][0]      = (float)eco2;

    /* LED: all OFF, then blink blue during calibration */
    led_red   = 1;  /* OFF (active low) */
    led_green = 1;
    led_blue  = 1;

    /* Note: We can only read the sensors that the main loop reads.
     * For the remaining 23 samples, we'll wait CALIB_CYCLE_MS and
     * the caller will keep feeding us readings via ember_calibration_check().
     * BUT since calibration is blocking, we need to read sensors here
     * ourselves. We'll mark that we're in calibration mode and let the
     * main loop logic handle the reads when we return.
     *
     * ALTERNATIVE (simpler): Just use the first sample and wait for
     * the main loop to call us 23 more times with new data.
     * This approach is cleaner — we set a flag and return.
     */

    /* Actually, since the partner's code passes sensor data each cycle,
     * we'll handle this as a state machine: collect samples across
     * multiple calls to ember_calibration_check(). */
    cprint("[CALIB] Sample 1/24 captured. Collecting remaining samples...\r\n");
}

/* ── Calibration state machine ────────────────────────────────────────────── */
static bool calib_active = false;
static int  calib_sample_idx = 0;
static float calib_samples[CALIB_NUM_FEATURES][CALIB_SAMPLE_COUNT];

/* Trigger flag — set by serial command "CALIB" */
static bool calib_trigger_pending = false;

/* ============================================================================
 * ember_calibration_check() — called every sensor cycle
 * ========================================================================== */
void ember_calibration_check(
    uint16_t pm1_0, uint16_t pm2_5, uint16_t pm10,
    float temperature, float humidity, float pressure, float gas_res,
    float mq_analog, int mq_digital,
    uint16_t tvoc, uint16_t eco2,
    bool sd_ready,
    DigitalOut &led_red, DigitalOut &led_green, DigitalOut &led_blue,
    void (*print_fn)(const char*))
{
    _print = print_fn;

    /* ── If calibration is in progress, collect samples ──────────────────── */
    if (calib_active) {
        if (calib_sample_idx < CALIB_SAMPLE_COUNT) {
            /* Store this cycle's readings */
            calib_samples[CALIB_PM1_0][calib_sample_idx]     = (float)pm1_0;
            calib_samples[CALIB_PM2_5][calib_sample_idx]     = (float)pm2_5;
            calib_samples[CALIB_PM10][calib_sample_idx]      = (float)pm10;
            calib_samples[CALIB_TEMP][calib_sample_idx]      = temperature;
            calib_samples[CALIB_HUM][calib_sample_idx]       = humidity;
            calib_samples[CALIB_PRESS][calib_sample_idx]     = pressure;
            calib_samples[CALIB_GAS_RES][calib_sample_idx]   = gas_res;
            calib_samples[CALIB_MQ_ANALOG][calib_sample_idx] = mq_analog;
            calib_samples[CALIB_MQ_DIGITAL][calib_sample_idx]= (float)mq_digital;
            calib_samples[CALIB_TVOC][calib_sample_idx]      = (float)tvoc;
            calib_samples[CALIB_ECO2][calib_sample_idx]      = (float)eco2;

            calib_sample_idx++;

            /* Blink blue LED */
            led_blue = !led_blue;

            char msg[64];
            snprintf(msg, sizeof(msg),
                "[CALIB] Sample %d/%d collected\r\n",
                calib_sample_idx, CALIB_SAMPLE_COUNT);
            cprint(msg);

            /* Check if we have all samples */
            if (calib_sample_idx >= CALIB_SAMPLE_COUNT) {
                /* ── Compute statistics ──────────────────────────────────── */
                cprint("\r\n[CALIB] Computing normalization parameters...\r\n");

                for (int f = 0; f < CALIB_NUM_FEATURES; f++) {
                    float fmin = calib_samples[f][0];
                    float fmax = calib_samples[f][0];
                    float sum  = 0.0f;

                    /* Min, Max, Sum */
                    for (int s = 0; s < CALIB_SAMPLE_COUNT; s++) {
                        float v = calib_samples[f][s];
                        if (v < fmin) fmin = v;
                        if (v > fmax) fmax = v;
                        sum += v;
                    }

                    float mean = sum / (float)CALIB_SAMPLE_COUNT;

                    /* Standard deviation */
                    float var_sum = 0.0f;
                    for (int s = 0; s < CALIB_SAMPLE_COUNT; s++) {
                        float diff = calib_samples[f][s] - mean;
                        var_sum += diff * diff;
                    }
                    float std_dev = sqrtf(var_sum / (float)CALIB_SAMPLE_COUNT);

                    /* Store */
                    calib_params.features[f].min_val  = fmin;
                    calib_params.features[f].max_val  = fmax;
                    calib_params.features[f].mean_val = mean;
                    calib_params.features[f].std_val  = std_dev;

                    /* Print */
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "[CALIB]   %-10s  min=%8.2f  max=%8.2f  mean=%8.2f  std=%6.2f\r\n",
                        feature_names[f], fmin, fmax, mean, std_dev);
                    cprint(buf);
                }

                calib_params.valid = true;

                /* ── Save to SD card ─────────────────────────────────────── */
                if (sd_ready) {
                    if (save_calibration_to_sd()) {
                        cprint("[CALIB] calibration.csv written to SD card OK\r\n");
                    }
                } else {
                    cprint("[CALIB] SD not available — calibration in RAM only\r\n");
                }

                /* ── Completion: solid green LED ─────────────────────────── */
                led_red   = 1;  /* OFF */
                led_green = 0;  /* ON (active low) */
                led_blue  = 1;  /* OFF */

                cprint("[CALIB] ============================================\r\n");
                cprint("[CALIB]   CALIBRATION COMPLETE\r\n");
                cprint("[CALIB]   Green LED = calibrated\r\n");
                cprint("[CALIB] ============================================\r\n\r\n");

                /* Reset state */
                calib_active = false;
                calib_sample_idx = 0;
            }
        }
        return;  /* Don't start new calibration while one is running */
    }

    /* ── Check if serial "CALIB" command was issued ────────────────────── */
    if (calib_trigger_pending) {
        calib_trigger_pending = false;

        cprint("\r\n[CALIB] ============================================\r\n");
        cprint("[CALIB]   SENSOR CALIBRATION MODE ACTIVATED\r\n");
        cprint("[CALIB]   Collecting baseline for ~60 seconds (24 samples)...\r\n");
        cprint("[CALIB] ============================================\r\n");

        /* Initialize sample collection */
        calib_active = true;
        calib_sample_idx = 0;
        memset(calib_samples, 0, sizeof(calib_samples));

        /* Visual feedback: blue LED ON */
        led_red   = 1;  /* OFF */
        led_green = 1;  /* OFF */
        led_blue  = 0;  /* ON (active low) */
    }
}

/* ============================================================================
 * ember_calibration_trigger() — called when "CALIB" typed in serial
 * ========================================================================== */
void ember_calibration_trigger(void)
{
    calib_trigger_pending = true;
}
