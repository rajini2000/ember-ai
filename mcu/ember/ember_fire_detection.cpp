/*
 * ember_fire_detection.cpp — Fire & Smoke Detection with Multi-Sensor Fusion
 *
 * Fuses PMS5003 particle spikes, MQ gas voltage surges, and BME680
 * temperature anomalies into a weighted fire-probability score.
 *
 * Score components (each 0.0 – 1.0, then weighted):
 *   PM spike:   weight 0.40 — PM2.5 rate-of-change vs baseline
 *   MQ surge:   weight 0.35 — MQ analog rate-of-change vs baseline
 *   Temp rise:  weight 0.25 — temperature rate-of-change vs baseline
 *
 * False-positive suppression:
 *   - Rate-of-change filtering: requires RAPID change, not just high values
 *   - Debounce: 3 consecutive cycles above threshold to trigger
 *   - Cooldown: 60s after alert before re-triggering
 *   - Normal cooking/dust: gradual changes score low; only spikes trigger
 *
 * ─── GenAI Declaration (Claude AI - Anthropic & GitHub Copilot) ───
 * Lines 35-62: Tunable parameters (#define block) — values tuned by
 *   developer, constant structure AI-assisted
 * Lines 66-95: Module state variables and baseline arrays — AI-generated
 * Lines 100-140: compute_baseline() moving-average with spike exclusion
 *   — AI-generated
 * Lines 145-250: ember_fire_check() scoring algorithm — AI-assisted (~60%)
 *   Rate-of-change computation, component scoring, debounce state machine
 * Lines 255-329: ember_fire_init(), ember_fire_clear(), debug output —
 *   AI-assisted scaffolding
 *
 * We designed the sensor fusion weights, thresholds, and false-positive
 * suppression strategy. AI assisted with the moving-average baseline and
 * debounce/cooldown state machine. Approximately 50% of this file is
 * AI-assisted. All code validated with real smoke/incense tests.
 */

#include "ember_fire_detection.h"
#include "mbed.h"
#include <cstdio>
#include <cstring>
#include <cmath>

/* ── Tunable parameters ───────────────────────────────────────────────────── */

// Weights for each component (must sum to 1.0)
#define W_PM     0.55f
#define W_MQ     0.25f
#define W_TEMP   0.20f

// Fire threshold: score >= this triggers FIRE ALERT
#define FIRE_THRESHOLD      0.50f

// Debounce: consecutive cycles above threshold to confirm fire
#define FIRE_DEBOUNCE       3

// Cooldown after alert ends before re-triggering (seconds)
#define FIRE_COOLDOWN_SEC   60

// Rate-of-change thresholds (per cycle, ~2s interval)
// Values above these get a component score of 1.0; linear scaling below
#define PM25_SPIKE_MAX      60.0f    // ug/m3 delta for full PM score
#define PM25_SPIKE_MIN       5.0f    // minimum delta to register (noise floor)

// Absolute PM2.5 thresholds (emergency override regardless of delta)
#define PM25_ABS_ALERT     150      // ug/m3 absolute value for alert boost
#define PM25_ABS_BOOST       0.20f  // bonus added when PM2.5 exceeds absolute threshold
#define MQ_SURGE_MAX         0.15f   // analog delta for full MQ score
#define MQ_SURGE_MIN         0.02f   // minimum delta to register
#define TEMP_RISE_MAX        3.0f    // °C delta for full temp score
#define TEMP_RISE_MIN        0.3f    // minimum delta to register

// Baseline moving-average window (number of cycles)
#define BASELINE_WINDOW      15

/* ── Module state ─────────────────────────────────────────────────────────── */

static void (*s_print)(const char*) = nullptr;

// Moving average baselines
static float s_pm25_history[BASELINE_WINDOW];
static float s_mq_history[BASELINE_WINDOW];
static float s_temp_history[BASELINE_WINDOW];
static int   s_hist_idx   = 0;
static int   s_hist_count = 0;     // fills up to BASELINE_WINDOW

// Pre-incident baseline ring buffer
static FireBaselineEntry_t s_baseline[FIRE_BASELINE_SLOTS];
static int s_base_idx   = 0;
static int s_base_count = 0;

// Alert state
static bool  s_in_alert      = false;
static int   s_debounce_cnt  = 0;
static Timer s_cooldown_timer;
static bool  s_cooldown_active = false;
static bool  s_first_cycle    = true;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void pprint(const char* msg) {
    if (s_print) s_print(msg);
}

// Compute moving average of a history buffer
static float moving_avg(const float* buf, int count) {
    if (count == 0) return 0.0f;
    int n = (count < BASELINE_WINDOW) ? count : BASELINE_WINDOW;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += buf[i];
    return sum / n;
}

// Clamp value between 0.0 and 1.0
static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// Map a delta to a 0.0–1.0 component score with noise floor
static float delta_to_score(float delta, float min_thresh, float max_thresh) {
    if (delta < min_thresh) return 0.0f;
    return clamp01((delta - min_thresh) / (max_thresh - min_thresh));
}

/* ── Public: init ─────────────────────────────────────────────────────────── */

void ember_fire_init(void (*print_fn)(const char*)) {
    s_print = print_fn;
    s_hist_idx = 0;
    s_hist_count = 0;
    s_base_idx = 0;
    s_base_count = 0;
    s_in_alert = false;
    s_debounce_cnt = 0;
    s_cooldown_active = false;
    s_first_cycle = true;
    memset(s_pm25_history, 0, sizeof(s_pm25_history));
    memset(s_mq_history, 0, sizeof(s_mq_history));
    memset(s_temp_history, 0, sizeof(s_temp_history));
    memset(s_baseline, 0, sizeof(s_baseline));

    pprint("[FIRE] Fire/smoke detection initialized\r\n");
    char cfg[128];
    snprintf(cfg, sizeof(cfg),
        "[FIRE] Threshold=%.2f  Debounce=%d  Cooldown=%ds  Weights=PM%.0f%%/MQ%.0f%%/T%.0f%%\r\n",
        FIRE_THRESHOLD, FIRE_DEBOUNCE, FIRE_COOLDOWN_SEC,
        W_PM * 100, W_MQ * 100, W_TEMP * 100);
    pprint(cfg);
}

/* ── Public: check every cycle ────────────────────────────────────────────── */

FireResult_t ember_fire_check(uint16_t pm2_5, uint16_t pm10,
                               float mq_analog, float temperature,
                               float humidity, float pressure,
                               uint16_t tvoc, uint16_t eco2,
                               float gas_res, int mq_digital) {
    FireResult_t result = {false, false, 0.0f, 0.0f, 0.0f, 0.0f};

    // Store in pre-incident baseline ring buffer (always, for SD logging)
    FireBaselineEntry_t* be = &s_baseline[s_base_idx];
    be->pm2_5       = pm2_5;
    be->pm10        = pm10;
    be->mq_analog   = mq_analog;
    be->temperature = temperature;
    be->humidity    = humidity;
    be->pressure    = pressure;
    be->tvoc        = tvoc;
    be->eco2        = eco2;
    be->gas_res     = gas_res;
    be->mq_digital  = mq_digital;
    s_base_idx = (s_base_idx + 1) % FIRE_BASELINE_SLOTS;
    if (s_base_count < FIRE_BASELINE_SLOTS) s_base_count++;

    // Check cooldown
    if (s_cooldown_active) {
        if (s_cooldown_timer.elapsed_time() >= std::chrono::seconds(FIRE_COOLDOWN_SEC)) {
            s_cooldown_active = false;
            s_cooldown_timer.stop();
            pprint("[FIRE] Cooldown expired, detection re-enabled\r\n");
        }
    }

    // Need at least a few cycles to compute meaningful deltas
    if (s_first_cycle || s_hist_count < 2) {
        s_pm25_history[s_hist_idx] = (float)pm2_5;
        s_mq_history[s_hist_idx]   = mq_analog;
        s_temp_history[s_hist_idx] = temperature;
        s_hist_idx = (s_hist_idx + 1) % BASELINE_WINDOW;
        if (s_hist_count < BASELINE_WINDOW) s_hist_count++;
        s_first_cycle = false;
        result.in_alert = s_in_alert;
        return result;
    }

    // Compute moving averages (baseline)
    float avg_pm25 = moving_avg(s_pm25_history, s_hist_count);
    float avg_mq   = moving_avg(s_mq_history, s_hist_count);
    float avg_temp = moving_avg(s_temp_history, s_hist_count);

    // Rate-of-change: current value minus baseline average
    float delta_pm   = (float)pm2_5 - avg_pm25;
    float delta_mq   = mq_analog - avg_mq;
    float delta_temp  = temperature - avg_temp;

    // Only positive deltas matter (we're looking for spikes/surges/rises)
    if (delta_pm < 0.0f) delta_pm = 0.0f;
    if (delta_mq < 0.0f) delta_mq = 0.0f;
    if (delta_temp < 0.0f) delta_temp = 0.0f;

    result.pm_delta   = delta_pm;
    result.mq_delta   = delta_mq;
    result.temp_delta = delta_temp;

    // Component scores (0.0 – 1.0 each)
    float pm_score   = delta_to_score(delta_pm, PM25_SPIKE_MIN, PM25_SPIKE_MAX);
    float mq_score   = delta_to_score(delta_mq, MQ_SURGE_MIN, MQ_SURGE_MAX);
    float temp_score = delta_to_score(delta_temp, TEMP_RISE_MIN, TEMP_RISE_MAX);

    // Weighted fire probability score
    float fire_score = (W_PM * pm_score) + (W_MQ * mq_score) + (W_TEMP * temp_score);

    // Bonus: if MQ digital pin flips to ALERT (0), add a boost
    if (mq_digital == 0 && mq_score > 0.1f) {
        fire_score += 0.10f;
    }

    // Bonus: if TVOC spikes (smoke produces VOCs), add boost
    if (tvoc > 500 && pm_score > 0.1f) {
        fire_score += 0.05f;
    }

    // Emergency override: absolute PM2.5 level indicates dangerous air
    // This catches cases where gradual ramps have shifted the baseline
    if (pm2_5 >= PM25_ABS_ALERT) {
        fire_score += PM25_ABS_BOOST;
        char abs_msg[80];
        snprintf(abs_msg, sizeof(abs_msg),
            "[FIRE] Absolute PM2.5=%u >= %d, boost +%.2f\r\n",
            pm2_5, PM25_ABS_ALERT, PM25_ABS_BOOST);
        pprint(abs_msg);
    }

    fire_score = clamp01(fire_score);
    result.score = fire_score;

    // Debug: print fire scoring every cycle (only when score > 0)
    if (fire_score > 0.01f) {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
            "[FIRE] Score=%.2f  PM:d=%.0f/s=%.2f  MQ:d=%.4f/s=%.2f  T:d=%.1f/s=%.2f  base=%.0f\r\n",
            fire_score, delta_pm, pm_score, delta_mq, mq_score, delta_temp, temp_score, avg_pm25);
        pprint(dbg);
    }

    // Update moving-average history (push current values)
    // Only update baseline with "normal" readings; spiked values pollute baseline
    if (pm2_5 < PM25_ABS_ALERT && delta_pm < PM25_SPIKE_MAX) {
        s_pm25_history[s_hist_idx] = (float)pm2_5;
    }
    // MQ and temp always update (less susceptible to sudden extremes)
    s_mq_history[s_hist_idx]   = mq_analog;
    s_temp_history[s_hist_idx] = temperature;
    s_hist_idx = (s_hist_idx + 1) % BASELINE_WINDOW;
    if (s_hist_count < BASELINE_WINDOW) s_hist_count++;

    // Debounce + alert logic
    if (!s_in_alert && !s_cooldown_active) {
        if (fire_score >= FIRE_THRESHOLD) {
            s_debounce_cnt++;
            if (s_debounce_cnt >= FIRE_DEBOUNCE) {
                // === FIRE ALERT TRIGGERED ===
                s_in_alert = true;
                result.alert = true;

                char msg[160];
                snprintf(msg, sizeof(msg),
                    "\r\n[FIRE] !!! FIRE ALERT !!! Score=%.2f (PM:%.2f MQ:%.2f T:%.2f)\r\n",
                    fire_score, pm_score, mq_score, temp_score);
                pprint(msg);
                snprintf(msg, sizeof(msg),
                    "[FIRE] Deltas: PM2.5=+%.1f  MQ=+%.4f  Temp=+%.2fC\r\n",
                    delta_pm, delta_mq, delta_temp);
                pprint(msg);
            } else {
                char msg[80];
                snprintf(msg, sizeof(msg),
                    "[FIRE] Score=%.2f above threshold (%d/%d debounce)\r\n",
                    fire_score, s_debounce_cnt, FIRE_DEBOUNCE);
                pprint(msg);
            }
        } else {
            // Reset debounce counter if score drops
            if (s_debounce_cnt > 0) {
                s_debounce_cnt = 0;
            }
        }
    } else if (s_in_alert) {
        // While in alert: check if fire score drops below threshold
        if (fire_score < FIRE_THRESHOLD * 0.5f) {
            // Fire seems to have subsided
            s_in_alert = false;
            s_debounce_cnt = 0;
            s_cooldown_active = true;
            s_cooldown_timer.reset();
            s_cooldown_timer.start();
            pprint("[FIRE] Alert cleared (score dropped). Cooldown started.\r\n");
        }
    }

    result.in_alert = s_in_alert;
    return result;
}

/* ── Public: get baseline for SD logging ──────────────────────────────────── */

const FireBaselineEntry_t* ember_fire_get_baseline(int* count) {
    if (count) *count = s_base_count;
    return s_baseline;
}

/* ── Public: manually clear fire alert ────────────────────────────────────── */

void ember_fire_clear_alert(void) {
    if (s_in_alert) {
        s_in_alert = false;
        s_debounce_cnt = 0;
        s_cooldown_active = true;
        s_cooldown_timer.reset();
        s_cooldown_timer.start();
        pprint("[FIRE] Alert manually cleared. Cooldown started.\r\n");
    }
}

/* ── Public: query active status ──────────────────────────────────────────── */

bool ember_fire_is_active(void) {
    return s_in_alert;
}
