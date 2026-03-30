/*
 * ember_alert_log.cpp  —  M4: Embedded Alert History + Event Replay
 *
 * Logs alarm ON events to RAM circular buffer (always) AND
 * to /sd/data/alert_log.csv on SD card (when available).
 * REPLAY and STATS work from RAM — no SD required.
 *
 * CSV format:
 *   timestamp,mode,alarm,pm25,pm10,temp,hum,aqi,q_off,q_on
 *
 * ─── GenAI Declaration (Claude AI - Anthropic & GitHub Copilot) ───
 * Lines 26-51: AlertEntry_t ring buffer and module state — AI-generated
 * Lines 64-76: get_timestamp() formatting — AI-generated
 * Lines 78-110: ember_alert_log_init() SD card setup — AI-assisted
 * Lines 112-158: ember_alert_log_check() OFF→ON transition detection
 *   and CSV logging — AI-assisted (~50%)
 * Lines 161-203: do_replay() serial output formatter — AI-generated
 * Lines 205-284: do_stats() statistics computation — ~60% AI-generated
 * Lines 286-317: ember_alert_check_serial_cmd() command parser incl.
 *   CALIB trigger — AI-assisted
 *
 * We designed the ring buffer size, CSV format, and REPLAY/STATS output
 * layout. AI assisted with the serial parser and statistics math.
 * Approximately 50% of this file is AI-assisted.
 * All code reviewed and validated on hardware.
 */

#include "ember_alert_log.h"
#include "mbed.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

/* ── RAM ring buffer ──────────────────────────────────────────────────────── */
#define MAX_ALERT_ENTRIES 20

typedef struct {
    char     timestamp[24];
    char     mode[8];
    uint16_t pm2_5;
    uint16_t pm10;
    float    temperature;
    float    humidity;
    float    aqi;
    float    q_off;
    float    q_on;
} AlertEntry_t;

static AlertEntry_t s_ring[MAX_ALERT_ENTRIES];
static int          s_ring_count = 0;   // total events ever logged
static int          s_ring_head  = 0;   // next write slot

/* ── Module state ─────────────────────────────────────────────────────────── */
static bool     s_sd_ok      = false;
static int      s_prev_alarm = 0;          // track OFF->ON transition
static void   (*s_print)(const char*) = nullptr;

// Serial command line buffer
static char     s_cmd_buf[32];
static int      s_cmd_len = 0;

// External serial port (defined in main.cpp)
extern BufferedSerial pc;

#define ALERT_LOG_PATH  "/sd/data/alert_log.csv"

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void pprint(const char* msg) {
    if (s_print) s_print(msg);
}

static void get_timestamp(char* buf, int buf_size) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    if (t && t->tm_year > 100) {
        snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
    } else {
        snprintf(buf, buf_size, "0000-00-00 00:00:00");
    }
}

/* ── Public: init ─────────────────────────────────────────────────────────── */

void ember_alert_log_init(bool sd_ok, void (*print_fn)(const char*)) {
    s_sd_ok   = sd_ok;
    s_print   = print_fn;
    s_prev_alarm = 0;
    s_cmd_len = 0;
    s_ring_count = 0;
    s_ring_head  = 0;
    memset(s_ring, 0, sizeof(s_ring));

    if (s_sd_ok) {
        // Create CSV with header if it doesn't exist yet
        FILE* fp = fopen(ALERT_LOG_PATH, "r");
        if (fp) {
            fclose(fp);
            pprint("[M4] Alert log found: " ALERT_LOG_PATH "\r\n");
        } else {
            fp = fopen(ALERT_LOG_PATH, "w");
            if (fp) {
                fprintf(fp, "timestamp,mode,alarm,pm25,pm10,temp,hum,aqi,q_off,q_on\n");
                fclose(fp);
                pprint("[M4] Alert log created: " ALERT_LOG_PATH "\r\n");
            } else {
                pprint("[M4] ERROR: Could not create alert log on SD\r\n");
            }
        }
    } else {
        pprint("[M4] SD not available — using RAM-only alert history\r\n");
    }

    pprint("[M4] Alert history ready (RAM buffer + serial REPLAY/STATS)\r\n");
}

/* ── Public: check and log ────────────────────────────────────────────────── */

void ember_alert_log_check(int alarm_on, float q_off, float q_on, float aqi,
                            const char* mode, const AlertSnapshot_t* snap) {
    // Only log on OFF -> ON transition (new alarm event)
    if (alarm_on == 1 && s_prev_alarm == 0) {
        char timestamp[24];
        get_timestamp(timestamp, sizeof(timestamp));

        // ---- Store in RAM ring buffer ----
        AlertEntry_t* entry = &s_ring[s_ring_head];
        strncpy(entry->timestamp, timestamp, sizeof(entry->timestamp) - 1);
        entry->timestamp[sizeof(entry->timestamp) - 1] = '\0';
        strncpy(entry->mode, mode ? mode : "?", sizeof(entry->mode) - 1);
        entry->mode[sizeof(entry->mode) - 1] = '\0';
        entry->pm2_5       = snap->pm2_5;
        entry->pm10        = snap->pm10;
        entry->temperature = snap->temperature;
        entry->humidity    = snap->humidity;
        entry->aqi         = aqi;
        entry->q_off       = q_off;
        entry->q_on        = q_on;
        s_ring_head = (s_ring_head + 1) % MAX_ALERT_ENTRIES;
        s_ring_count++;

        // Always print to serial
        char msg[128];
        snprintf(msg, sizeof(msg),
            "[M4] ALERT logged (#%d): %s  mode=%s  PM2.5=%u  AQI=%.1f\r\n",
            s_ring_count, timestamp, mode, snap->pm2_5, aqi);
        pprint(msg);

        // Write to SD if available
        if (s_sd_ok) {
            FILE* fp = fopen(ALERT_LOG_PATH, "a");
            if (fp) {
                fprintf(fp, "%s,%s,ON,%u,%u,%.1f,%.1f,%.1f,%.3f,%.3f\n",
                        timestamp, mode,
                        snap->pm2_5, snap->pm10,
                        snap->temperature, snap->humidity,
                        aqi, q_off, q_on);
                fclose(fp);
            }
        }
    }

    s_prev_alarm = alarm_on;
}

/* ── REPLAY: print last 10 alert events from RAM ────────────────────────── */

static void do_replay(void) {
    pprint("\r\n[M4] === ALERT REPLAY (last 10 events) ===\r\n");
    pprint("  Time                 Mode    PM2.5  PM10  Temp   Hum   AQI    Q_off  Q_on\r\n");
    pprint("  --------------------------------------------------------------------------\r\n");

    if (s_ring_count == 0) {
        pprint("  [No alarm events recorded yet]\r\n\r\n");
        return;
    }

    int total = (s_ring_count > 10) ? 10 : s_ring_count;
    int stored = (s_ring_count > MAX_ALERT_ENTRIES) ? MAX_ALERT_ENTRIES : s_ring_count;

    // If we want the last 10: start from (head - total) in circular buffer
    int start_idx;
    if (total > stored) total = stored;
    start_idx = (s_ring_head - total + MAX_ALERT_ENTRIES) % MAX_ALERT_ENTRIES;

    char out[160];
    for (int i = 0; i < total; i++) {
        int idx = (start_idx + i) % MAX_ALERT_ENTRIES;
        AlertEntry_t* e = &s_ring[idx];
        snprintf(out, sizeof(out),
            "  %-20s %-7s %-6u %-5u %-6.1f %-5.1f %-6.1f %-6.3f %.3f\r\n",
            e->timestamp, e->mode, e->pm2_5, e->pm10,
            e->temperature, e->humidity, e->aqi, e->q_off, e->q_on);
        pprint(out);
    }

    char summary[64];
    snprintf(summary, sizeof(summary),
        "[M4] Total alerts this session: %d\r\n", s_ring_count);
    pprint(summary);

    if (s_sd_ok) {
        pprint("[M4] (Also persisted to SD: " ALERT_LOG_PATH ")\r\n");
    } else {
        pprint("[M4] (RAM only — insert SD card for persistent CSV log)\r\n");
    }
    pprint("\r\n");
}

/* ── STATS: compute statistics from RAM buffer ───────────────────────────── */

static void do_stats(void) {
    pprint("\r\n[M4] === ALERT STATISTICS ===\r\n");

    if (s_ring_count == 0) {
        pprint("  [No alarm events recorded yet]\r\n\r\n");
        return;
    }

    int stored = (s_ring_count > MAX_ALERT_ENTRIES) ? MAX_ALERT_ENTRIES : s_ring_count;

    int   hour_bins[24] = {0};
    float sum_pm25 = 0, sum_pm10 = 0, sum_temp = 0, sum_aqi = 0;
    int   cloud_count = 0, local_count = 0;

    for (int i = 0; i < stored; i++) {
        AlertEntry_t* e = &s_ring[i];
        sum_pm25 += e->pm2_5;
        sum_pm10 += e->pm10;
        sum_temp += e->temperature;
        sum_aqi  += e->aqi;

        // Parse hour from timestamp "YYYY-MM-DD HH:MM:SS"
        int hour = 0;
        if (strlen(e->timestamp) >= 13) {
            sscanf(e->timestamp + 11, "%d", &hour);
        }
        if (hour >= 0 && hour < 24) hour_bins[hour]++;

        if (strstr(e->mode, "CLOUD")) cloud_count++;
        else                           local_count++;
    }

    // Find most frequent hour
    int peak_hour = 0;
    for (int h = 1; h < 24; h++) {
        if (hour_bins[h] > hour_bins[peak_hour]) peak_hour = h;
    }

    char out[128];

    snprintf(out, sizeof(out), "  Total alerts (session): %d\r\n", s_ring_count);
    pprint(out);

    snprintf(out, sizeof(out), "  Stored in buffer      : %d / %d\r\n", stored, MAX_ALERT_ENTRIES);
    pprint(out);

    snprintf(out, sizeof(out), "  Cloud decisions       : %d\r\n", cloud_count);
    pprint(out);

    snprintf(out, sizeof(out), "  Local (RL) decisions  : %d\r\n", local_count);
    pprint(out);

    snprintf(out, sizeof(out), "  Avg PM2.5 at alarm    : %.1f ug/m3\r\n",
             sum_pm25 / stored);
    pprint(out);

    snprintf(out, sizeof(out), "  Avg PM10  at alarm    : %.1f ug/m3\r\n",
             sum_pm10 / stored);
    pprint(out);

    snprintf(out, sizeof(out), "  Avg AQI   at alarm    : %.1f\r\n",
             sum_aqi / stored);
    pprint(out);

    snprintf(out, sizeof(out), "  Avg Temp  at alarm    : %.1f C\r\n",
             sum_temp / stored);
    pprint(out);

    snprintf(out, sizeof(out), "  Most alerts at hour   : %02d:00\r\n", peak_hour);
    pprint(out);

    if (s_sd_ok) {
        pprint("  CSV log on SD         : " ALERT_LOG_PATH "\r\n");
    } else {
        pprint("  SD card               : Not available (RAM only)\r\n");
    }
    pprint("\r\n");
}

/* ── Public: check serial input for REPLAY / STATS ───────────────────────── */

void ember_alert_check_serial_cmd(void) {
    // Non-blocking read from serial
    while (pc.readable()) {
        char c;
        if (pc.read(&c, 1) != 1) break;

        if (c == '\r' || c == '\n') {
            if (s_cmd_len > 0) {
                s_cmd_buf[s_cmd_len] = '\0';

                // Convert to uppercase for comparison
                for (int i = 0; i < s_cmd_len; i++) {
                    if (s_cmd_buf[i] >= 'a' && s_cmd_buf[i] <= 'z')
                        s_cmd_buf[i] -= 32;
                }

                if (strcmp(s_cmd_buf, "REPLAY") == 0) {
                    do_replay();
                } else if (strcmp(s_cmd_buf, "STATS") == 0) {
                    do_stats();
                } else if (strcmp(s_cmd_buf, "CALIB") == 0) {
                    extern void ember_calibration_trigger(void);
                    ember_calibration_trigger();
                }

                s_cmd_len = 0;
            }
        } else if (s_cmd_len < (int)sizeof(s_cmd_buf) - 1) {
            s_cmd_buf[s_cmd_len++] = c;
        }
    }
}
