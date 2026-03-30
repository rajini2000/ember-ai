/*
 * ember_alert_log.h  —  M4: Embedded Alert History + Event Replay
 *
 * Logs alarm ON events to alert_log.csv on SD card.
 * Supports REPLAY (last 10 events) and STATS commands via serial terminal.
 *
 * ─── GenAI Declaration (Claude AI - Anthropic) ───
 * Lines 27-42: AlertSnapshot_t struct — AI-assisted
 * Lines 46-77: Function prototypes and documentation — ~25% AI-assisted
 * All code reviewed and validated on hardware.
 *
 * Usage in main.cpp:
 *   #include "ember/ember_alert_log.h"
 *
 *   // Once at startup:
 *   ember_alert_log_init(sd_ready);
 *
 *   // After every alarm decision (M3 block):
 *   ember_alert_log_check(alarm, q_off, q_on, aqi, mode, &last_snapshot);
 *
 *   // In main loop — check for REPLAY / STATS typed in serial terminal:
 *   ember_alert_check_serial_cmd();
 */

#ifndef EMBER_ALERT_LOG_H
#define EMBER_ALERT_LOG_H

#include <cstdint>

/* ── Snapshot struct (matches last_snapshot in main.cpp) ──────────────────── */
typedef struct {
    uint16_t pm1_0;
    uint16_t pm2_5;
    uint16_t pm10;
    float    temperature;
    float    humidity;
    float    pressure;
    float    gas_res;
    float    mq_analog;
    int      mq_digital;
    uint16_t tvoc;
    uint16_t eco2;
    bool     pms_valid;
} AlertSnapshot_t;

/* ── Public API ────────────────────────────────────────────────────────────── */

/**
 * ember_alert_log_init()
 * Call once at startup. Creates alert_log.csv on SD if it doesn't exist.
 *
 * @param sd_ok   true if SD card is mounted and ready
 * @param print_fn  function pointer to pc_print for serial output
 */
void ember_alert_log_init(bool sd_ok, void (*print_fn)(const char*));

/**
 * ember_alert_log_check()
 * Call after every alarm decision. Detects OFF->ON transition and logs entry.
 *
 * @param alarm_on   current alarm state (0 or 1)
 * @param q_off      Q-value for ALARM OFF (0.0 if CLOUD mode)
 * @param q_on       Q-value for ALARM ON  (0.0 if CLOUD mode)
 * @param aqi        composite AQI estimate
 * @param mode       "CLOUD" or "LOCAL"
 * @param snap       pointer to current sensor snapshot
 */
void ember_alert_log_check(int alarm_on, float q_off, float q_on, float aqi,
                            const char* mode, const AlertSnapshot_t* snap);

/**
 * ember_alert_check_serial_cmd()
 * Call every cycle. Non-blocking — reads one char at a time from serial.
 * When a full line is received, checks for "REPLAY" or "STATS" commands.
 */
void ember_alert_check_serial_cmd(void);

#endif /* EMBER_ALERT_LOG_H */
