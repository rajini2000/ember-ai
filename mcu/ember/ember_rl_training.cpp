/*
 * ember_rl_training.cpp  —  M1: RL Training Data Logger
 *
 * ─── GenAI Declaration (Claude AI - Anthropic & GitHub Copilot) ───
 * Lines 20-27: Button and state variable declarations — developer-authored
 * Lines 51-100: compute_features() 16-feature engineering — AI-assisted
 *   (~60%). We defined which features to compute; AI helped with delta
 *   and ratio calculations
 * Lines 107-158: write_to_sd() CSV logging with daily file rotation —
 *   AI-generated
 * Lines 165-221: ember_training_loop() main orchestration, button
 *   label toggling, serial output — ~40% AI-assisted
 *
 * We designed the feature set and labeling workflow. AI assisted with
 * file I/O boilerplate and feature computation math.
 * Approximately 45% of this file is AI-assisted.
 * All code reviewed and validated on hardware.
 */

#include "ember_rl_training.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <sys/stat.h>
#include <ctime>

// ---- Buttons (SW2 = DANGER label, SW3 = SAFE label) ----
// FRDM-K64F: SW2 = PTC6, SW3 = PTA4, both active LOW with pull-up
static DigitalIn sw2(PTC6, PullUp);
static DigitalIn sw3(PTA4, PullUp);

// ---- Training state ----
static int   training_label = 0;    // 0 = SAFE, 1 = DANGER
static float prev_pm25      = 0.0f; // For delta_pm25 computation
static float prev_pm10      = 0.0f; // For delta_pm10 computation
static bool  first_cycle    = true; // Skip delta on first reading

// ============================================================
// compute_features()
// Fills f[0..15] with the 16 RL state vector features.
//
// Feature index map:
//  0  pm1_0           raw PM1.0  (ug/m3)
//  1  pm2_5           raw PM2.5  (ug/m3)
//  2  pm10            raw PM10   (ug/m3)
//  3  temperature     deg C
//  4  humidity        %RH
//  5  pressure        hPa
//  6  gas_resistance  Ohms
//  7  mq_analog       0.0 - 1.0 (ADC ratio, 3.3V ref)
//  8  mq_digital      0 or 1
//  9  tvoc            ppb  (0 if ENS160 offline)
// 10  eco2            ppm  (0 if ENS160 offline)
// 11  aqi_category    0-5  (derived from PM2.5, EPA breakpoints)
// 12  delta_pm25      PM2.5 change since last cycle
// 13  delta_pm10      PM10  change since last cycle
// 14  thi             temperature-humidity index = T + 0.33*H - 4.0
// 15  gas_ratio       mq_analog / log10(gas_resistance)
// ============================================================
static void compute_features(
    uint16_t pm1_0, uint16_t pm2_5, uint16_t pm10,
    float temperature, float humidity, float pressure, float gas_res,
    float mq_analog, int mq_digital,
    uint16_t tvoc, uint16_t eco2,
    float* f)
{
    // Raw features
    f[0]  = (float)pm1_0;
    f[1]  = (float)pm2_5;
    f[2]  = (float)pm10;
    f[3]  = temperature;
    f[4]  = humidity;
    f[5]  = pressure;
    f[6]  = gas_res;
    f[7]  = mq_analog;
    f[8]  = (float)mq_digital;
    f[9]  = (float)tvoc;
    f[10] = (float)eco2;

    // AQI category from PM2.5 (EPA breakpoints, 0=GOOD .. 5=HAZARDOUS)
    float pm = f[1];
    int cat;
    if      (pm <= 12.0f)  cat = 0;
    else if (pm <= 35.4f)  cat = 1;
    else if (pm <= 55.4f)  cat = 2;
    else if (pm <= 150.4f) cat = 3;
    else if (pm <= 250.4f) cat = 4;
    else                   cat = 5;
    f[11] = (float)cat;

    // Delta PM2.5 and PM10 (zero on first cycle)
    if (first_cycle) {
        f[12] = 0.0f;
        f[13] = 0.0f;
        first_cycle = false;
    } else {
        f[12] = f[1] - prev_pm25;
        f[13] = f[2] - prev_pm10;
    }
    prev_pm25 = f[1];
    prev_pm10 = f[2];

    // Temperature-Humidity Index
    f[14] = f[3] + 0.33f * f[4] - 4.0f;

    // Gas ratio: mq_analog / log10(gas_resistance)
    float log_gas = (f[6] > 1.0f) ? log10f(f[6]) : 1.0f;
    f[15] = (log_gas > 0.0f) ? (f[7] / log_gas) : 0.0f;
}

// ============================================================
// write_to_sd()
// Appends one CSV row to /sd/training_data.csv
// Creates header row if file is new.
// ============================================================
static void write_to_sd(float* f, int label, void (*print_fn)(const char*))
{
    const char* path = "/sd/training_data.csv";

    // Check if file already exists (to decide whether to write header)
    struct stat st;
    bool is_new = (stat(path, &st) != 0);

    FILE* fp = fopen(path, "a");
    if (!fp) {
        print_fn("[TRAIN] ERROR: cannot open training_data.csv\r\n");
        return;
    }

    // Write CSV header once
    if (is_new) {
        fprintf(fp,
            "timestamp,"
            "pm1_0,pm2_5,pm10,"
            "temperature,humidity,pressure,gas_resistance,"
            "mq_analog,mq_digital,"
            "tvoc,eco2,"
            "aqi_category,delta_pm25,delta_pm10,thi,gas_ratio,"
            "label\n");
    }

    // Build timestamp string
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char ts[24];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    // Write data row (16 features + label)
    fprintf(fp,
        "%s,"
        "%.0f,%.0f,%.0f,"
        "%.2f,%.2f,%.2f,%.0f,"
        "%.4f,%d,"
        "%.0f,%.0f,"
        "%.0f,%.2f,%.2f,%.2f,%.6f,"
        "%d\n",
        ts,
        f[0], f[1], f[2],
        f[3], f[4], f[5], f[6],
        f[7], (int)f[8],
        f[9], f[10],
        f[11], f[12], f[13], f[14], f[15],
        label);

    fclose(fp);
}

// ============================================================
// ember_training_loop()
// Call this once per sensor cycle inside while(true) in main.cpp
// ============================================================
void ember_training_loop(
    uint16_t pm1_0,  uint16_t pm2_5, uint16_t pm10,
    float    temperature, float humidity, float pressure, float gas_res,
    float    mq_analog,   int mq_digital,
    uint16_t tvoc,   uint16_t eco2,
    bool     sd_ready,
    DigitalOut& led_red,
    DigitalOut& led_green,
    void (*print_fn)(const char*))
{
    // ---- Check buttons (active LOW) ----
    if (!sw2.read()) {
        // SW2 pressed -> DANGER label
        training_label = 1;
        led_red   = 0;   // RED ON   (active low)
        led_green = 1;   // GREEN OFF
        print_fn("[TRAIN] SW2 pressed -> Label = DANGER(1) | LED = RED\r\n");
    } else if (!sw3.read()) {
        // SW3 pressed -> SAFE label
        training_label = 0;
        led_red   = 1;   // RED OFF
        led_green = 0;   // GREEN ON (active low)
        print_fn("[TRAIN] SW3 pressed -> Label = SAFE(0)   | LED = GREEN\r\n");
    }

    // ---- Compute 16 features ----
    float f[16];
    compute_features(
        pm1_0, pm2_5, pm10,
        temperature, humidity, pressure, gas_res,
        mq_analog, mq_digital,
        tvoc, eco2,
        f);

    // ---- Print features to serial terminal ----
    char msg[256];
    snprintf(msg, sizeof(msg),
        "[TRAIN] Label=%s(%d) | PM:[%.0f %.0f %.0f] T=%.1fC H=%.1f%% P=%.1f Gas=%.0f MQ=[%.4f %d]\r\n",
        training_label ? "DANGER" : "SAFE", training_label,
        f[0], f[1], f[2],
        f[3], f[4], f[5], f[6],
        f[7], (int)f[8]);
    print_fn(msg);

    snprintf(msg, sizeof(msg),
        "[TRAIN] TVOC=%.0f eCO2=%.0f | AQI_cat=%.0f dPM25=%.2f dPM10=%.2f THI=%.2f GasR=%.6f\r\n",
        f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
    print_fn(msg);

    // ---- Write to SD card ----
    if (sd_ready) {
        write_to_sd(f, training_label, print_fn);
        print_fn("[TRAIN] Row saved to /sd/training_data.csv\r\n");
    } else {
        print_fn("[TRAIN] SD not ready - row NOT saved\r\n");
    }
}
