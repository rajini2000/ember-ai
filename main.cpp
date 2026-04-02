/**
 * Ember Project - FRDM-K64F + ESP32 WiFi Module + PMS5003 Sensor
 * Communicates with ESP32 running AT firmware over UART.
 * ESP32 UART1: TX=GPIO17, RX=GPIO16 (AT command port)
 * K64F UART3:  TX=PTC17, RX=PTC16
 * Wiring: ESP32 TX(GPIO17) -> K64F RX(PTC16)
 *         ESP32 RX(GPIO16) -> K64F TX(PTC17)
 *         GND <-> GND
 *
 * PMS5003 PM2.5 Sensor on UART1:
 * K64F UART1: TX=PTC4, RX=PTC3 at 9600 baud
 * Wiring: PMS5003 TX -> K64F RX(PTC3)
 *         PMS5003 5V -> 5V, GND -> GND
 *
 * Flying Fish MQ Gas Sensor:
 * AO -> PTB2 (A0) - analog gas concentration
 * DO -> PTB3 (A1) - digital threshold alert
 * VCC -> 5V, GND -> GND
 *
 * ENS160 Air Quality Sensor (I2C):
 * SDA -> PTE25 (D14)
 * SCL -> PTE24 (D15)
 * I2C address: 0x52
 * VCC -> 3.3V, GND -> GND
 *
 * BME680 Environmental Sensor (I2C - shared bus with ENS160):
 * SDA -> PTE25 (D14)
 * SCL -> PTE24 (D15)
 * I2C address: 0x76
 * VCC -> 3.3V, GND -> GND
 *
 * MicroSD Card (SPI):
 * MISO -> PTD3, MOSI -> PTD2, SCK -> PTD1, CS -> PTD0
 * Max data: 30GB, daily CSV files
 *
 * ─── GenAI Declaration (Claude AI - Anthropic & GitHub Copilot) ───
 * Lines 89-101: poll_button() debounce logic — AI-generated
 * Lines 150-200: BME680 register map and calibration struct — AI-assisted
 * Lines 232-267: esp_send_cmd() AT-command driver — AI-generated
 * Lines 289-408: sendToAIServer() HTTPS POST and JSON parsing — AI-assisted
 * Lines 416-560: pollRemoteConfig() remote arm/disarm — AI-generated
 * Lines 597-745: WiFi credential load/save/scan/connect — ~60% AI-generated
 * Lines 763-1250: SoftAP HTML portal (url_decode, buildAndSendPinPage,
 *   buildAndSendConfigPage, sendSuccessPage) — AI-assisted scaffolding,
 *   HTML/CSS content authored by developer
 * Lines 1281-1808: startSoftAP(), stopSoftAP(), handleSoftAPClient() —
 *   ~40% AI-generated (AT-command sequences), rest hand-written
 * Lines 1893-2010: I2C helper functions — AI-generated
 * Lines 2013-2165: SH1106 OLED driver (oled_init through oled_invert_rect)
 *   — AI-assisted from datasheet specifications
 * Lines 2169-2292: sendFireAlertToAPI(), logFireEventToSD() — AI-generated
 * Lines 2294-2350: oled_update() display pages — ~30% AI-assisted
 * Lines 2372-2650: readENS160(), readBME680() register-level I2C reads —
 *   AI-generated from sensor datasheet register maps
 * Lines 2715-2862: SD card init, file rotation, CSV logging — AI-assisted
 * Lines 2864-3460: main() loop orchestration — ~35% AI-assisted
 *
 * We wrote the pin assignments, sensor wiring, alarm thresholds, AQI
 * breakpoint tables, and overall system architecture. AI assisted with
 * boilerplate I2C/SPI/UART driver code and AT-command sequences.
 * Approximately 45% of this file is AI-assisted. All AI-generated code
 * was reviewed, tested, and validated on FRDM-K64F hardware.
 */

#include "mbed.h"
#include <cstring>
#include <cstdio>
#include "SDBlockDevice.h"
#include "FATFileSystem.h"
#include <time.h>
#include "ember/ember_rl_inference.h"  // M2: On-Board RL Inference
#include "ember/ember_alert_log.h"     // M4: Embedded Alert History + Event Replay
#include "ember/ember_fire_detection.h" // M5: Fire & Smoke Detection
#include "ember/ember_calibration.h"     // M5(Rajini): Sensor Calibration

// Serial to PC for debug output
BufferedSerial pc(USBTX, USBRX, 115200);

// Serial to ESP32 (UART3: TX=PTC17, RX=PTC16)
static BufferedSerial esp(PTC17, PTC16, 115200);

// Serial to PMS5003 (UART1: TX=PTC4, RX=PTC3 at 9600 baud)
static BufferedSerial pms_serial(PTC4, PTC3, 9600);

// MQ Gas Sensor (Flying Fish module)
AnalogIn mq_analog(PTB2);    // AO pin - analog gas concentration
DigitalIn mq_digital(PTB3);  // DO pin - digital threshold alert

// Alarm output (HIGH = on, LOW = off)
DigitalOut alarm(PTA2, 0);   // Start with alarm off

// ========== ARM/DISARM BUTTON ==========
// Physical push-button on PTA1 (D3) — toggles alarm arm/disarm
// Press once: disarm → armed=false, alarm off
// Press again: arm   → armed=true
DigitalIn arm_disarm_btn(PTA1, PullUp);
static bool arm_btn_prev = true;           // PullUp: true = released
static uint32_t arm_btn_last_ms = 0;       // debounce tracker
static volatile bool arm_btn_pressed = false;

// OLED page button — now triggers TEST ALARM (3 sec buzz)
// Screen auto-cycles every 3 seconds (no manual page switching)
DigitalIn oled_btn_pin(PTC2, PullUp);
static volatile int oled_page = 0;
static const int OLED_PAGES = 4;
static volatile bool btn_pressed = false;
static bool btn_prev_state = true;       // true = released (PullUp)
static uint32_t btn_last_edge_ms = 0;    // debounce tracker
static Timer oled_auto_timer;            // auto-cycle OLED pages every 3s
static bool oled_auto_started = false;

// OLED arm/disarm overlay: show status for 2 seconds after button press
static bool oled_arm_overlay = false;
static Timer oled_arm_overlay_timer;
static bool oled_arm_overlay_requested = false;  // set by pollRemoteConfig, handled in main loop

// Poll button for falling-edge detection (call frequently)
static void poll_button() {
    bool cur = oled_btn_pin.read();
    // Detect falling edge (released → pressed) with 120ms debounce
    if (btn_prev_state && !cur) {
        uint32_t now = (uint32_t)(Kernel::Clock::now().time_since_epoch().count());
        if (now - btn_last_edge_ms >= 120) {
            btn_pressed = true;
            btn_last_edge_ms = now;
        }
    }
    btn_prev_state = cur;
}

// Poll arm/disarm button on PTA1 for toggle detection
static void poll_arm_button() {
    bool cur = arm_disarm_btn.read();
    if (arm_btn_prev && !cur) {
        uint32_t now = (uint32_t)(Kernel::Clock::now().time_since_epoch().count());
        if (now - arm_btn_last_ms >= 200) {  // 200ms debounce
            arm_btn_pressed = true;
            arm_btn_last_ms = now;
        }
    }
    arm_btn_prev = cur;
}

// ========== COMPOSITE AIR QUALITY INDEX (AQI) ALGORITHM ==========
// Each sensor maps to a 0-500 sub-score based on EPA breakpoints.
// Composite score = weighted max of sub-scores.
// Alarm uses hysteresis + debounce to prevent false triggers.
//
// Score ranges: 0-50 GOOD | 51-100 MODERATE | 101-150 UNHEALTHY_SENSITIVE
//               151-200 UNHEALTHY | 201-300 VERY_UNHEALTHY | 301-500 HAZARDOUS
//
// Alarm triggers at composite >= 151 (UNHEALTHY) for 3 consecutive cycles
// Alarm clears  at composite <  100 (GOOD/MODERATE) for 3 consecutive cycles

// Alarm state
static bool alarm_active = false;
static Timer alarm_cooldown_timer;
static bool alarm_cooldown_active = false;
const int ALARM_COOLDOWN_SEC = 30;  // 30-second cooldown after manual dismiss
static int last_aqi_score = 0;      // AQI from AI server or on-board RL

// ========== M4 + DIGITAL TWIN: Remote Config Variables ==========
static bool alarm_armed = true;              // DIGITAL TWIN: synced between physical button & web panel
static bool test_alarm_requested = false;    // DIGITAL TWIN: web panel test button → physical buzzer
static Timer test_alarm_timer;
static bool test_alarm_active = false;       // Currently running 3s test
static const int TEST_ALARM_DURATION_MS = 3000;

// ========== M5: Fire Alert State ==========
static bool fire_alert_active = false;       // FIRE ALERT mode active
static bool fire_alert_sent = false;         // HTTP POST sent for current event

// ========== DIGITAL TWIN: State sync variables ==========
static bool dt_status_pending = false;       // flag to send status on next cycle
static const char* dt_pending_cause = "";   // cause string for pending status

// MicroSD Card (SPI) - lower frequency for compatibility
SDBlockDevice sd(PTD2, PTD3, PTD1, PTD0, 400000);  // MOSI, MISO, SCK, CS, 400kHz
FATFileSystem fs("sd");
static bool sd_ready = false;
static const uint64_t MAX_DATA_SIZE = 30ULL * 1024ULL * 1024ULL * 1024ULL; // 30GB

// Shared I2C bus for ENS160 + BME680
I2C sensor_i2c(PTE25, PTE24);   // SDA=PTE25(D14), SCL=PTE24(D15)

// ENS160 Air Quality Sensor
const int ENS160_ADDR = 0x53 << 1;  // 7-bit addr shifted for Mbed I2C (ADD=VCC → 0x53)

// BME680 Environmental Sensor
const int BME680_ADDR = 0x76 << 1;  // 7-bit addr shifted for Mbed I2C (SDO=GND → 0x76)

// SH1106 OLED Display (1.3" 128x64)
const int SH1106_ADDR = 0x3C << 1;  // 7-bit addr shifted for Mbed I2C

// BME680 Register addresses
const char BME680_REG_CHIP_ID     = 0xD0;
const char BME680_REG_RESET       = 0xE0;
const char BME680_REG_CTRL_HUM    = 0x72;
const char BME680_REG_CTRL_MEAS   = 0x74;
const char BME680_REG_CTRL_GAS_1  = 0x71;
const char BME680_REG_GAS_WAIT_0  = 0x64;
const char BME680_REG_RES_HEAT_0  = 0x5A;
const char BME680_REG_STATUS      = 0x1D;
const char BME680_REG_MEAS_STATUS = 0x1D;
const char BME680_REG_TEMP_MSB    = 0x22;
const char BME680_REG_TEMP_LSB    = 0x23;
const char BME680_REG_TEMP_XLSB   = 0x24;
const char BME680_REG_PRESS_MSB   = 0x1F;
const char BME680_REG_PRESS_LSB   = 0x20;
const char BME680_REG_PRESS_XLSB  = 0x21;
const char BME680_REG_HUM_MSB     = 0x25;
const char BME680_REG_HUM_LSB     = 0x26;
const char BME680_REG_GAS_MSB     = 0x2A;
const char BME680_REG_GAS_LSB     = 0x2B;

// BME680 calibration data storage
struct BME680Calib {
    uint16_t par_t1;
    int16_t  par_t2;
    int8_t   par_t3;
    uint16_t par_p1;
    int16_t  par_p2;
    int8_t   par_p3;
    int16_t  par_p4;
    int16_t  par_p5;
    int8_t   par_p6;
    int8_t   par_p7;
    int16_t  par_p8;
    int16_t  par_p9;
    uint8_t  par_p10;
    uint16_t par_h1;
    uint16_t par_h2;
    int8_t   par_h3;
    int8_t   par_h4;
    int8_t   par_h5;
    uint8_t  par_h6;
    int8_t   par_h7;
    int8_t   par_g1;
    int16_t  par_g2;
    uint8_t  par_g3;
    uint8_t  res_heat_range;
    int8_t   res_heat_val;
    int8_t   range_sw_err;
};

static BME680Calib bme_cal;

// ENS160 Register addresses
const char ENS160_REG_PART_ID    = 0x00;
const char ENS160_REG_OPMODE     = 0x10;
const char ENS160_REG_CONFIG     = 0x11;
const char ENS160_REG_COMMAND    = 0x12;
const char ENS160_REG_TEMP_IN    = 0x13; // 2 bytes
const char ENS160_REG_RH_IN      = 0x15; // 2 bytes
const char ENS160_REG_DATA_STATUS = 0x20;
const char ENS160_REG_DATA_AQI   = 0x21;
const char ENS160_REG_DATA_TVOC  = 0x22; // 2 bytes, little-endian
const char ENS160_REG_DATA_ECO2  = 0x24; // 2 bytes, little-endian

// ENS160 operating modes
const char ENS160_OPMODE_DEEP_SLEEP = 0x00;
const char ENS160_OPMODE_IDLE       = 0x01;
const char ENS160_OPMODE_STD        = 0x02; // Standard operation

// LEDs for status indication
DigitalOut led_red(LED1);
DigitalOut led_green(LED2);
DigitalOut led_blue(LED3);

// Helper: print string to PC
void pc_print(const char* msg) {
    pc.write(msg, strlen(msg));
}

// Send AT command to ESP32 and read response
// Returns number of bytes read into response buffer
int esp_send_cmd(const char* cmd, char* resp, int resp_size, int timeout_ms = 3000) {
    // Clear any pending data
    char junk[64];
    while (esp.readable()) {
        esp.read(junk, sizeof(junk));
    }

    // Send command
    esp.write(cmd, strlen(cmd));
    esp.write("\r\n", 2);

    // Read response with timeout
    Timer t;
    t.start();
    int total = 0;
    memset(resp, 0, resp_size);

    while (t.elapsed_time() < std::chrono::milliseconds(timeout_ms) && total < resp_size - 1) {
        if (esp.readable()) {
            int n = esp.read(resp + total, resp_size - 1 - total);
            if (n > 0) total += n;
            // Check if we got OK or ERROR
            if (strstr(resp, "OK\r\n") || strstr(resp, "ERROR\r\n")) {
                break;
            }
        } else {
            ThisThread::sleep_for(10ms);
        }
    }
    t.stop();
    return total;
}

// Check if response contains "OK"
bool resp_ok(const char* resp) {
    return strstr(resp, "OK") != nullptr;
}

// ========== AI SERVER INTEGRATION ==========
// Sends sensor data to Rajini's AI server on Render for RL-based alarm decision
static const char* AI_SERVER_HOST = "ember-ai-ews2.onrender.com";
static const int   AI_SERVER_PORT = 443;
static bool ai_server_available = false;
static int  ai_fail_count = 0;
const int   AI_MAX_FAILS = 3;       // After 3 consecutive failures, skip for a while
const int   AI_RETRY_CYCLES = 15;   // Retry after 15 cycles (~30s) of failures
static int  ai_skip_counter = 0;

// Result from AI server
struct AIServerResult {
    bool  valid;          // true if server responded successfully
    bool  alarm_on;       // true if server says "ON"
    float aqi;            // AQI from server
    char  category[24];   // AQI category string
};

// ========== DIGITAL TWIN: Send sensor data to server (Physical → Virtual) ==========
// POSTs sensor readings to /predict — web dashboard mirrors live sensor values,
// AQI, alarm state, creating a virtual duplicate of the physical device.
AIServerResult sendToAIServer(float pm1_0, float pm2_5, float pm10,
                              float tvoc, float eco2,
                              float temperature, float humidity,
                              float pressure, float gas_res,
                              float mq_analog, int mq_digital)
{
    AIServerResult result = {false, false, 0.0f, ""};
    char resp[1024];
    
    // Skip if too many consecutive fails (retry after AI_RETRY_CYCLES)
    if (ai_fail_count >= AI_MAX_FAILS) {
        ai_skip_counter++;
        if (ai_skip_counter < AI_RETRY_CYCLES) return result;
        ai_skip_counter = 0;
        ai_fail_count = 0;
    }
    
    // Build JSON body
    char body[256];
    int body_len = snprintf(body, sizeof(body),
        "{\"PM1.0\":%.0f,\"PM2.5\":%.0f,\"PM10\":%.0f,"
        "\"TVOC\":%.0f,\"eCO2\":%.0f,"
        "\"temperature\":%.1f,\"humidity\":%.1f,"
        "\"pressure\":%.1f,\"gas\":%.0f,"
        "\"MQ_analog\":%.4f,\"MQ_digital\":%d,"
        "\"device_id\":\"K64F-ember\"}",
        pm1_0, pm2_5, pm10,
        tvoc, eco2,
        temperature, humidity,
        pressure, gas_res,
        mq_analog, mq_digital);
    
    // Use AT+HTTPCPOST — handles HTTPS/TLS internally (more reliable than raw SSL socket)
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "AT+HTTPCPOST=\"https://%s/predict\",%d,1,\"Content-Type: application/json\"",
        AI_SERVER_HOST, body_len);
    
    // Drain stale serial data
    { char junk[256]; while (esp.readable()) { esp.read(junk, sizeof(junk)); ThisThread::sleep_for(5ms); } }
    
    esp.write(cmd, strlen(cmd));
    esp.write("\r\n", 2);
    
    // Wait for '>' prompt (up to 5s)
    Timer t;
    t.start();
    bool found_prompt = false;
    while (t.elapsed_time() < 5s) {
        if (esp.readable()) {
            char c;
            if (esp.read(&c, 1) == 1 && c == '>') { found_prompt = true; break; }
        } else {
            ThisThread::sleep_for(5ms);
        }
    }
    
    if (!found_prompt) {
        ai_fail_count++;
        return result;
    }
    
    // Send JSON body after '>' prompt
    esp.write(body, body_len);
    
    // Wait for +HTTPCPOST response (8s timeout — Render responds in 2-3s when warm)
    memset(resp, 0, sizeof(resp));
    int total = 0;
    t.reset();
    while (t.elapsed_time() < 8s && total < (int)sizeof(resp) - 1) {
        if (esp.readable()) {
            int n = esp.read(resp + total, sizeof(resp) - 1 - total);
            if (n > 0) total += n;
            if (strstr(resp, "\r\nOK") || strstr(resp, "ERROR")) break;
        } else {
            ThisThread::sleep_for(10ms);
        }
    }
    
    // Parse JSON from +HTTPCPOST response
    char* json_start = strstr(resp, "{\"alarm\"");
    if (!json_start) json_start = strstr(resp, "{\"aqi\"");
    if (!json_start) json_start = strchr(resp, '{');
    
    if (json_start) {
        result.valid = true;
        ai_fail_count = 0;
        ai_server_available = true;
        
        char* alarm_ptr = strstr(json_start, "\"alarm\"");
        if (alarm_ptr) result.alarm_on = (strstr(alarm_ptr, "\"ON\"") != nullptr);
        
        char* aqi_ptr = strstr(json_start, "\"aqi\"");
        if (aqi_ptr) {
            aqi_ptr = strchr(aqi_ptr + 5, ':');
            if (aqi_ptr) result.aqi = strtof(aqi_ptr + 1, nullptr);
        }
        
        char* cat_ptr = strstr(json_start, "\"category\"");
        if (cat_ptr) {
            char* q1 = strchr(cat_ptr + 10, '"');
            if (q1) {
                q1++;
                char* q2 = strchr(q1, '"');
                if (q2) {
                    int clen = q2 - q1;
                    if (clen > 23) clen = 23;
                    memcpy(result.category, q1, clen);
                    result.category[clen] = '\0';
                }
            }
        }
    } else {
        ai_fail_count++;
    }
    
    return result;
}

// Forward declarations for M5 fire detection (defined after SensorSnapshot)
void sendFireAlertToAPI(float score, float pm_delta, float mq_delta, float temp_delta);
void logFireEventToSD(float score, float pm_delta, float mq_delta, float temp_delta);

// ========== M4: REMOTE CONFIG POLLING ==========
// Forward declaration (defined later, needed by pollRemoteConfig)
void sendDeviceStatusToAPI(bool armed, bool active, const char* cause);

// Sensor data struct (moved here — needed by pollRemoteConfig for sim data)
struct SensorSnapshot {
    uint16_t pm1_0, pm2_5, pm10;
    uint16_t tvoc, eco2;
    int aqi;
    float temperature, humidity, pressure, gas_res;
    float mq_analog;
    int mq_digital;
    bool pms_valid;
};

static SensorSnapshot last_snapshot = {};

// ========== DIGITAL TWIN: Poll commands from web panel (Virtual → Physical) ==========
// Polls the Render server for pending web control panel commands.
// Arm/disarm toggle, threshold sliders, test alarm — all sync from web to board.
// Uses AT+HTTPCPOST to POST /config endpoint.
// Called every 3rd cycle (~6s) to reduce network overhead.
void pollRemoteConfig() {
    char resp[512];
    const char* body = "{\"device_id\":\"K64F-ember\"}";
    int body_len = strlen(body);

    char cmd[128];
    snprintf(cmd, sizeof(cmd),
        "AT+HTTPCPOST=\"https://ember-ai-ews2.onrender.com/config\",%d,1,\"Content-Type: application/json\"",
        body_len);

    // Drain stale serial data
    { char junk[256]; while (esp.readable()) { esp.read(junk, sizeof(junk)); ThisThread::sleep_for(5ms); } }

    esp.write(cmd, strlen(cmd));
    esp.write("\r\n", 2);

    // Wait for '>' prompt (up to 3s)
    Timer t;
    t.start();
    bool found_prompt = false;
    while (t.elapsed_time() < 3s) {
        if (esp.readable()) {
            char c;
            if (esp.read(&c, 1) == 1 && c == '>') { found_prompt = true; break; }
        } else {
            ThisThread::sleep_for(5ms);
        }
    }
    if (!found_prompt) return;

    // Send body
    esp.write(body, body_len);

    // Wait for response (3s timeout)
    memset(resp, 0, sizeof(resp));
    int total = 0;
    t.reset();
    while (t.elapsed_time() < 3s && total < (int)sizeof(resp) - 1) {
        if (esp.readable()) {
            int n = esp.read(resp + total, sizeof(resp) - 1 - total);
            if (n > 0) total += n;
            if (strstr(resp, "\r\nOK") || strstr(resp, "ERROR")) break;
        } else {
            ThisThread::sleep_for(10ms);
        }
    }

    // Find JSON body in response
    char* json = strchr(resp, '{');
    if (!json || strlen(json) < 3) return;  // empty {} = no pending commands

    // Check if response is just "{}" (no commands)
    char* close = strchr(json, '}');
    if (close && (close - json) <= 1) return;

    char pmsg[64];
    snprintf(pmsg, sizeof(pmsg), "[M4-CONFIG] Received: ");
    pc_print(pmsg);
    pc_print(json);
    pc_print("\r\n");

    char msg[96];

    // ========== DIGITAL TWIN: Apply arm/disarm from web panel (Virtual → Physical) ==========
    char* arm_ptr = strstr(json, "\"arm\"");
    if (arm_ptr) {
        bool new_armed = (strstr(arm_ptr, "true") != nullptr &&
                          (strstr(arm_ptr, "true") - arm_ptr) < 15);
        if (new_armed != alarm_armed) {
            alarm_armed = new_armed;
            snprintf(msg, sizeof(msg),
                "[M4-CONFIG] Alarm %s via web panel\r\n",
                alarm_armed ? "ARMED" : "DISARMED");
            pc_print(msg);
            if (!alarm_armed && alarm_active) {
                alarm = 0;
                alarm_active = false;
                pc_print("[M4-CONFIG] Alarm silenced (disarmed)\r\n");
            }
            if (alarm_armed) {
                // Re-arming: clear any cooldown so alarm can trigger immediately
                alarm_cooldown_active = false;
                alarm_cooldown_timer.stop();
                // Force alarm ON if current AQI warrants it
                if (last_aqi_score >= 151 && !alarm_active) {
                    alarm = 1;
                    alarm_active = true;
                    pc_print("[M4-CONFIG] Alarm re-engaged (AQI high)\r\n");
                }
            }
            // Request OLED overlay (handled in main loop where OLED functions are in scope)
            oled_arm_overlay_requested = true;
            // DIGITAL TWIN: sync new state back to server so /status is correct
            sendDeviceStatusToAPI(alarm_armed, alarm_active, alarm_armed ? "web_armed" : "web_disarmed");
        }
    }

    // Parse "aqi_trigger" — currently informational (RL model handles thresholds)
    char* trig_ptr = strstr(json, "\"aqi_trigger\"");
    if (trig_ptr) {
        char* colon = strchr(trig_ptr + 13, ':');
        if (colon) {
            int val = atoi(colon + 1);
            if (val >= 50 && val <= 500) {
                snprintf(msg, sizeof(msg),
                    "[M4-CONFIG] AQI_TRIGGER noted: %d\r\n", val);
                pc_print(msg);
            }
        }
    }

    // Parse "aqi_clear"
    char* clear_ptr = strstr(json, "\"aqi_clear\"");
    if (clear_ptr) {
        char* colon = strchr(clear_ptr + 11, ':');
        if (colon) {
            int val = atoi(colon + 1);
            if (val >= 25 && val <= 300) {
                snprintf(msg, sizeof(msg),
                    "[M4-CONFIG] AQI_CLEAR noted: %d\r\n", val);
                pc_print(msg);
            }
        }
    }

    // Parse "debounce"
    char* deb_ptr = strstr(json, "\"debounce\"");
    if (deb_ptr) {
        char* colon = strchr(deb_ptr + 10, ':');
        if (colon) {
            int val = atoi(colon + 1);
            if (val >= 1 && val <= 10) {
                snprintf(msg, sizeof(msg),
                    "[M4-CONFIG] DEBOUNCE noted: %d\r\n", val);
                pc_print(msg);
            }
        }
    }

    // Parse "test_alarm": true
    char* test_ptr = strstr(json, "\"test_alarm\"");
    if (test_ptr && strstr(test_ptr, "true") && (strstr(test_ptr, "true") - test_ptr) < 20) {
        test_alarm_requested = true;
        pc_print("[M4-CONFIG] Test alarm requested from web panel\r\n");
    }

    // ========== SIMULATION DATA: Override sensor snapshot from web panel ==========
    char* sim_ptr = strstr(json, "\"sim_pm25\"");
    if (sim_ptr) {
        // Helper: extract float after key
        auto extract_f = [](const char* j, const char* key) -> float {
            const char* p = strstr(j, key);
            if (!p) return -1;
            p = strchr(p, ':');
            if (!p) return -1;
            return strtof(p + 1, nullptr);
        };

        float spm25  = extract_f(json, "\"sim_pm25\"");
        float spm10  = extract_f(json, "\"sim_pm10\"");
        float stvoc  = extract_f(json, "\"sim_tvoc\"");
        float seco2  = extract_f(json, "\"sim_eco2\"");
        float stemp  = extract_f(json, "\"sim_temp\"");
        float shumid = extract_f(json, "\"sim_humid\"");
        float spress = extract_f(json, "\"sim_press\"");
        float smq    = extract_f(json, "\"sim_mq\"");
        float saqi   = extract_f(json, "\"sim_aqi\"");

        if (spm25 >= 0) last_snapshot.pm2_5 = (uint16_t)spm25;
        if (spm10 >= 0) last_snapshot.pm10  = (uint16_t)spm10;
        if (stvoc >= 0) last_snapshot.tvoc   = (uint16_t)stvoc;
        if (seco2 >= 0) last_snapshot.eco2   = (uint16_t)seco2;
        if (stemp > -100) last_snapshot.temperature = stemp;
        if (shumid >= 0) last_snapshot.humidity = shumid;
        if (spress >= 0) last_snapshot.pressure = spress;
        if (smq >= 0) last_snapshot.mq_analog = smq;
        if (saqi >= 0) { last_snapshot.aqi = (int)saqi; last_aqi_score = (int)saqi; }

        // Check sim_alarm
        char* salarm_ptr = strstr(json, "\"sim_alarm\"");
        if (salarm_ptr && strstr(salarm_ptr, "ON") && (strstr(salarm_ptr, "ON") - salarm_ptr) < 20) {
            if (alarm_armed && !alarm_active) {
                alarm = 1;
                alarm_active = true;
            }
        }

        pc_print("[SIM] Web simulation data applied to sensors\r\n");
        char sim_msg[128];
        snprintf(sim_msg, sizeof(sim_msg),
            "[SIM] PM2.5=%u TVOC=%u eCO2=%u T=%.1f AQI=%d\r\n",
            last_snapshot.pm2_5, last_snapshot.tvoc, last_snapshot.eco2,
            last_snapshot.temperature, last_snapshot.aqi);
        pc_print(sim_msg);
    }
}

// (SensorSnapshot moved above pollRemoteConfig for sim data access)

// ========== SOFTAP WEB PORTAL — WiFi Provisioning ==========
// Saved WiFi credentials structure
struct WiFiCreds {
    char ssid[64];
    char password[64];
    bool valid;
};

static WiFiCreds saved_wifi = {"", "", false};
static bool wifi_connected = false;
static bool softap_mode = false;
static char connected_ssid[64] = "";  // Currently connected WiFi SSID
static char setup_pin[5] = "0000";   // 4-digit PIN for SoftAP access
static bool pin_verified = false;      // Whether PIN has been entered correctly

// WiFi network scan result
struct WiFiNetwork {
    char ssid[64];
    int rssi;
    int security;  // 0=open, 2=WPA, 3=WPA2, 4=WPA_WPA2
};
static WiFiNetwork scanned_nets[20];
static int scanned_count = 0;

// --- SD Card WiFi Credential Management ---
bool loadWiFiCreds() {
    if (!sd_ready) return false;
    FILE* fp = fopen("/sd/wifi_config.txt", "r");
    if (!fp) {
        pc_print("[WiFi] No saved credentials on SD\r\n");
        return false;
    }
    char line[128];
    saved_wifi.ssid[0] = '\0';
    saved_wifi.password[0] = '\0';
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char* cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (strncmp(line, "SSID=", 5) == 0) {
            strncpy(saved_wifi.ssid, line + 5, sizeof(saved_wifi.ssid) - 1);
        } else if (strncmp(line, "PASS=", 5) == 0) {
            strncpy(saved_wifi.password, line + 5, sizeof(saved_wifi.password) - 1);
        }
    }
    fclose(fp);
    if (strlen(saved_wifi.ssid) > 0) {
        saved_wifi.valid = true;
        char msg[128];
        snprintf(msg, sizeof(msg), "[WiFi] Loaded saved SSID: %s\r\n", saved_wifi.ssid);
        pc_print(msg);
        return true;
    }
    return false;
}

bool saveWiFiCreds(const char* ssid, const char* password) {
    if (!sd_ready) {
        pc_print("[WiFi] SD not ready, cannot save credentials\r\n");
        return false;
    }
    FILE* fp = fopen("/sd/wifi_config.txt", "w");
    if (!fp) {
        pc_print("[WiFi] Failed to open wifi_config.txt for writing\r\n");
        return false;
    }
    fprintf(fp, "SSID=%s\n", ssid);
    fprintf(fp, "PASS=%s\n", password);
    fclose(fp);
    strncpy(saved_wifi.ssid, ssid, sizeof(saved_wifi.ssid) - 1);
    strncpy(saved_wifi.password, password, sizeof(saved_wifi.password) - 1);
    saved_wifi.valid = true;
    char msg[128];
    snprintf(msg, sizeof(msg), "[WiFi] Credentials saved to SD: %s\r\n", ssid);
    pc_print(msg);
    return true;
}

// --- WiFi Network Scanning ---
void scanWiFiNetworks() {
    pc_print("[WiFi] Scanning networks...\r\n");
    
    // Use static buffer to avoid stack overflow
    static char scan_resp[4096];
    memset(scan_resp, 0, sizeof(scan_resp));
    esp_send_cmd("AT+CWLAP", scan_resp, sizeof(scan_resp), 10000);
    
    // Drain any remaining scan data from UART
    ThisThread::sleep_for(200ms);
    { char junk[256]; while (esp.readable()) esp.read(junk, sizeof(junk)); }
    
    scanned_count = 0;
    
    // Parse line by line manually (no strtok - it modifies the buffer)
    char* p = scan_resp;
    while (*p && scanned_count < 15) {
        // Find "+CWLAP:("
        char* cwlap = strstr(p, "+CWLAP:(");
        if (!cwlap) break;
        
        cwlap += 8; // skip "+CWLAP:("
        int sec = atoi(cwlap);
        
        // Find SSID between quotes
        char* q1 = strchr(cwlap, '"');
        if (!q1) break;
        q1++;
        char* q2 = strchr(q1, '"');
        if (!q2 || (q2 - q1) >= 63) { p = cwlap; continue; }
        
        int ssid_len = q2 - q1;
        if (ssid_len > 0) {
            strncpy(scanned_nets[scanned_count].ssid, q1, ssid_len);
            scanned_nets[scanned_count].ssid[ssid_len] = '\0';
            
            // Parse RSSI after closing quote
            char* comma = strchr(q2, ',');
            scanned_nets[scanned_count].rssi = comma ? atoi(comma + 1) : -90;
            scanned_nets[scanned_count].security = sec;
            scanned_count++;
        }
        
        // Move past this entry
        p = q2 + 1;
    }
    
    // Sort by signal strength (strongest first)
    for (int i = 0; i < scanned_count - 1; i++) {
        for (int j = i + 1; j < scanned_count; j++) {
            if (scanned_nets[j].rssi > scanned_nets[i].rssi) {
                WiFiNetwork tmp = scanned_nets[i];
                scanned_nets[i] = scanned_nets[j];
                scanned_nets[j] = tmp;
            }
        }
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "[WiFi] Found %d networks\r\n", scanned_count);
    pc_print(msg);
}

// --- Connect to WiFi ---
bool connectToWiFi(const char* ssid, const char* password) {
    char cmd[256];
    char resp[1024];
    
    // Set station mode
    esp_send_cmd("AT+CWMODE=1", resp, sizeof(resp), 2000);
    ThisThread::sleep_for(500ms);
    
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    char pmsg[128];
    snprintf(pmsg, sizeof(pmsg), "[WiFi] Connecting to: %s\r\n", ssid);
    pc_print(pmsg);
    
    esp_send_cmd(cmd, resp, sizeof(resp), 15000);
    pc_print(resp);
    
    if (resp_ok(resp) && strstr(resp, "WIFI GOT IP")) {
        wifi_connected = true;
        // Get IP
        esp_send_cmd("AT+CIFSR", resp, sizeof(resp), 3000);
        pc_print(resp);
        pc_print("[WiFi] CONNECTED!\r\n");
        return true;
    }
    pc_print("[WiFi] Connection FAILED\r\n");
    return false;
}

// --- URL decode helper ---
void url_decode(const char* src, char* dst, int dst_len) {
    int i = 0, j = 0;
    while (src[i] && j < dst_len - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = {src[i+1], src[i+2], '\0'};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

// --- Send data to TCP client via ESP32 ---
bool esp_send_data(int link_id, const char* data, int len) {
    char cmd[32];
    char resp[256];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d", link_id, len);
    
    // Drain any stale UART data before sending command
    ThisThread::sleep_for(20ms);
    { char junk[256]; while (esp.readable()) { esp.read(junk, sizeof(junk)); ThisThread::sleep_for(5ms); } }
    ThisThread::sleep_for(20ms);
    
    // Send CIPSEND command
    esp.write(cmd, strlen(cmd));
    esp.write("\r\n", 2);
    
    // Wait for '>' prompt - read byte by byte, scan through up to 512 bytes of junk
    Timer t;
    t.start();
    bool found_prompt = false;
    int scanned = 0;
    while (t.elapsed_time() < 4s && scanned < 512) {
        if (esp.readable()) {
            char c;
            int n = esp.read(&c, 1);
            if (n == 1) {
                scanned++;
                if (c == '>') { found_prompt = true; break; }
            }
        } else {
            ThisThread::sleep_for(5ms);
        }
    }
    
    if (!found_prompt) {
        pc_print("[TCP] No > prompt\r\n");
        return false;
    }
    
    // Send actual data
    esp.write(data, len);
    
    // Wait for SEND OK
    memset(resp, 0, sizeof(resp));
    int total = 0;
    t.reset();
    while (t.elapsed_time() < 5s && total < 255) {
        if (esp.readable()) {
            int n = esp.read(resp + total, 255 - total);
            if (n > 0) total += n;
            if (strstr(resp, "SEND OK") || strstr(resp, "ERROR")) break;
        } else {
            ThisThread::sleep_for(5ms);
        }
    }
    return strstr(resp, "SEND OK") != nullptr;
}

// --- Send HTML in chunks (ESP32 buffer is limited) ---
void esp_send_html_chunk(int link_id, const char* html) {
    int total_len = strlen(html);
    int sent = 0;
    const int CHUNK = 1024;  // Send 1KB at a time
    
    while (sent < total_len) {
        int remaining = total_len - sent;
        int to_send = (remaining > CHUNK) ? CHUNK : remaining;
        bool ok = false;
        for (int retry = 0; retry < 3 && !ok; retry++) {
            if (retry > 0) {
                pc_print("[TCP] Retrying chunk...\r\n");
                ThisThread::sleep_for(200ms);
            }
            ok = esp_send_data(link_id, html + sent, to_send);
        }
        if (!ok) {
            pc_print("[TCP] Chunk failed after 3 retries\r\n");
            break;
        }
        sent += to_send;
        ThisThread::sleep_for(50ms);  // Give ESP32 time to flush
    }
}

// --- Build PIN entry page ---
void buildAndSendPinPage(int link_id, bool wrong_pin) {
    const char* header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n";
    esp_send_data(link_id, header, strlen(header));
    ThisThread::sleep_for(30ms);

    const char* pin_html =
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Ember Setup</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
        "background:#f5f5f7;display:flex;align-items:center;"
        "justify-content:center;min-height:100vh}"
        ".c{background:#fff;border-radius:20px;padding:40px 32px;"
        "max-width:380px;width:90%;text-align:center;"
        "box-shadow:0 4px 24px rgba(0,0,0,.08)}"
        ".logo{font-size:48px;margin-bottom:16px}"
        "h1{font-size:24px;font-weight:700;margin-bottom:8px}"
        "p{color:#86868b;font-size:15px;margin-bottom:24px}"
        ".pins{display:flex;gap:12px;justify-content:center;margin-bottom:24px}"
        ".pins input{width:52px;height:60px;text-align:center;font-size:28px;"
        "font-weight:700;border:2px solid #d2d2d7;border-radius:14px;"
        "outline:none;background:#fafafa;-webkit-appearance:none}"
        ".pins input:focus{border-color:#0071e3;background:#fff}"
        "button{width:100%;padding:14px;background:#0071e3;color:#fff;"
        "border:none;border-radius:14px;font-size:17px;font-weight:600;"
        "cursor:pointer;margin-bottom:12px}"
        "button:active{background:#005bb5}"
        ".err{color:#ff3b30;font-size:14px;font-weight:500;margin-bottom:16px}"
        ".hint{color:#86868b;font-size:13px}"
        "</style></head><body><div class='c'>"
        "<div class='logo'>&#x1f525;</div>"
        "<h1>Ember Setup</h1>"
        "<p>Enter the 4-digit PIN shown on the serial monitor</p>";
    esp_send_html_chunk(link_id, pin_html);
    ThisThread::sleep_for(30ms);

    // Error message if wrong PIN
    if (wrong_pin) {
        esp_send_html_chunk(link_id, "<div class='err'>Incorrect PIN. Try again.</div>");
        ThisThread::sleep_for(20ms);
    }

    const char* pin_form =
        "<form method='POST' action='http://192.168.4.1/verify-pin' "
        "onsubmit=\"document.getElementById('pin').value="
        "document.querySelector('[name=d1]').value+"
        "document.querySelector('[name=d2]').value+"
        "document.querySelector('[name=d3]').value+"
        "document.querySelector('[name=d4]').value\">"
        "<div class='pins'>"
        "<input type='tel' name='d1' maxlength='1' pattern='[0-9]' required autofocus "
        "oninput=\"this.value=this.value.replace(/[^0-9]/g,'');if(this.value)this.nextElementSibling.focus()\">"
        "<input type='tel' name='d2' maxlength='1' pattern='[0-9]' required "
        "oninput=\"this.value=this.value.replace(/[^0-9]/g,'');if(this.value)this.nextElementSibling.focus()\">"
        "<input type='tel' name='d3' maxlength='1' pattern='[0-9]' required "
        "oninput=\"this.value=this.value.replace(/[^0-9]/g,'');if(this.value)this.nextElementSibling.focus()\">"
        "<input type='tel' name='d4' maxlength='1' pattern='[0-9]' required "
        "oninput=\"this.value=this.value.replace(/[^0-9]/g,'')\">"
        "</div>"
        "<input type='hidden' name='pin' id='pin'>"
        "<button type='submit'>"
        "Verify</button>"
        "</form>"
        "<div class='hint'>Check the device's serial console for the PIN</div>"
        "</div></body></html>";
    esp_send_html_chunk(link_id, pin_form);
}

// --- Build the Apple-style WiFi config HTML page ---
void buildAndSendConfigPage(int link_id) {
    // Get signal strength icon helper
    // Build the page in sections to manage memory
    
    // HTTP header with no-cache to prevent stale pages
    const char* header = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Cache-Control: no-store, no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n\r\n";
    esp_send_data(link_id, header, strlen(header));
    ThisThread::sleep_for(30ms);
    
    // HTML head + CSS (Apple/Tailwind-inspired inline styles)
    const char* html_head = 
        "<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Ember Setup</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'SF Pro Display','Segoe UI',Roboto,sans-serif;"
        "background:#f5f5f7;color:#1d1d1f;min-height:100vh}"
        ".container{max-width:480px;margin:0 auto;padding:20px}"
        ".header{text-align:center;padding:40px 0 24px}"
        ".logo{width:48px;height:48px;background:linear-gradient(135deg,#ff6b35,#ff3b30);"
        "border-radius:12px;margin:0 auto 16px;display:flex;align-items:center;justify-content:center;"
        "font-size:24px;color:#fff;box-shadow:0 4px 12px rgba(255,59,48,0.3)}"
        "h1{font-size:28px;font-weight:700;letter-spacing:-0.02em;margin-bottom:4px}"
        ".subtitle{color:#86868b;font-size:15px;font-weight:400}"
        ".card{background:#fff;border-radius:16px;padding:0;margin-bottom:16px;"
        "box-shadow:0 1px 3px rgba(0,0,0,0.08),0 1px 2px rgba(0,0,0,0.04);"
        "overflow:hidden}"
        ".card-title{font-size:13px;font-weight:600;letter-spacing:0.02em;"
        "text-transform:uppercase;color:#86868b;padding:16px 20px 8px}"
        ".net-item{display:flex;align-items:center;padding:13px 20px;cursor:pointer;"
        "border-bottom:0.5px solid #e8e8ed;transition:background 0.15s}"
        ".net-item:last-child{border-bottom:none}"
        ".net-item:hover{background:#f5f5f7}"
        ".net-item:active{background:#e8e8ed}"
        ".net-name{flex:1;font-size:16px;font-weight:400}"
        ".net-lock{color:#86868b;margin-right:8px;font-size:14px}"
        ".signal{display:flex;align-items:flex-end;gap:1.5px;height:16px}"
        ".signal .bar{width:3px;background:#c7c7cc;border-radius:1px}"
        ".signal .bar.active{background:#1d1d1f}"
        ".s1 .b1{height:4px}.s1 .b2{height:7px}.s1 .b3{height:11px}.s1 .b4{height:16px}"
        ".s1 .b1.active{background:#34c759}"
        ".s2 .b1,.s2 .b2{height:4px;height:7px}"
        ".s2 .b1{height:4px}.s2 .b2{height:7px}.s2 .b3{height:11px}.s2 .b4{height:16px}"
        ".modal-overlay{display:none;position:fixed;top:0;left:0;right:0;bottom:0;"
        "background:rgba(0,0,0,0.4);z-index:100;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);"
        "align-items:center;justify-content:center}"
        ".modal-overlay.show{display:flex}"
        ".modal{background:#fff;border-radius:20px;width:90%;max-width:360px;"
        "padding:28px 24px 20px;box-shadow:0 20px 60px rgba(0,0,0,0.2);text-align:center}"
        ".modal h2{font-size:18px;font-weight:600;margin-bottom:4px}"
        ".modal .msub{color:#86868b;font-size:14px;margin-bottom:20px}"
        "input[type=password],input[type=text]{width:100%;padding:12px 16px;font-size:16px;"
        "border:1px solid #d2d2d7;border-radius:12px;outline:none;font-family:inherit;"
        "background:#f9f9fb;transition:border 0.2s,box-shadow 0.2s}"
        "input:focus{border-color:#0071e3;box-shadow:0 0 0 3px rgba(0,113,227,0.15);background:#fff}"
        ".btn-row{display:flex;gap:10px;margin-top:20px}"
        ".btn{flex:1;padding:12px;border:none;border-radius:12px;font-size:16px;"
        "font-weight:600;cursor:pointer;font-family:inherit;transition:all 0.2s}"
        ".btn-cancel{background:#f5f5f7;color:#1d1d1f}"
        ".btn-cancel:hover{background:#e8e8ed}"
        ".btn-join{background:#0071e3;color:#fff}"
        ".btn-join:hover{background:#0077ed}"
        ".btn-join:active{transform:scale(0.97)}"
        ".status-bar{display:flex;gap:8px;flex-wrap:wrap}"
        ".stat{flex:1;min-width:80px;background:#f5f5f7;border-radius:12px;padding:12px;text-align:center}"
        ".stat-val{font-size:20px;font-weight:700;color:#1d1d1f}"
        ".stat-label{font-size:11px;color:#86868b;margin-top:2px}"
        ".badge{display:inline-block;padding:3px 10px;border-radius:980px;font-size:12px;font-weight:600}"
        ".badge-green{background:#34c75920;color:#248a3d}"
        ".badge-red{background:#ff3b3020;color:#d63030}"
        ".toast{position:fixed;top:20px;left:50%;transform:translateX(-50%);"
        "background:#1d1d1f;color:#fff;padding:12px 24px;border-radius:980px;"
        "font-size:14px;font-weight:500;display:none;z-index:200;"
        "box-shadow:0 4px 20px rgba(0,0,0,0.3)}"
        ".toast.show{display:block;animation:slideIn 0.3s ease}"
        "@keyframes slideIn{from{opacity:0;transform:translateX(-50%) translateY(-20px)}"
        "to{opacity:1;transform:translateX(-50%) translateY(0)}}"
        ".refresh-btn{background:none;border:none;color:#0071e3;font-size:14px;"
        "font-weight:600;cursor:pointer;padding:8px 16px;border-radius:8px;font-family:inherit}"
        ".refresh-btn:hover{background:#0071e320}"
        ".scan-spin{animation:spin 1s linear infinite;display:inline-block}"
        "@keyframes spin{to{transform:rotate(360deg)}}"
        "</style></head><body><div class='container'>"
        "<div class='header'>"
        "<div class='logo'>&#x1f525;</div>"
        "<h1>Ember Setup</h1>"
        "<p class='subtitle'>Air Quality Monitor</p>"
        "</div>";
    esp_send_html_chunk(link_id, html_head);
    ThisThread::sleep_for(30ms);
    
    // Sensor status card (show placeholder if sensors haven't been read yet)
    char sensor_card[512];
    if (last_snapshot.pm2_5 == 0 && last_snapshot.temperature < 1.0f && last_aqi_score == 0) {
        snprintf(sensor_card, sizeof(sensor_card),
            "<div class='card'>"
            "<div class='card-title'>Sensor Status</div>"
            "<div style='padding:16px 20px;text-align:center;color:#86868b;font-size:14px'>"
            "&#x1f4a8; Sensors will activate after WiFi setup"
            "</div></div>");
    } else {
        snprintf(sensor_card, sizeof(sensor_card),
            "<div class='card'>"
            "<div class='card-title'>Live Sensor Status</div>"
            "<div style='padding:12px 20px 16px'>"
            "<div class='status-bar'>"
            "<div class='stat'><div class='stat-val'>%u</div><div class='stat-label'>PM2.5</div></div>"
            "<div class='stat'><div class='stat-val'>%.1f&deg;</div><div class='stat-label'>Temp</div></div>"
            "<div class='stat'><div class='stat-val'>%.0f%%</div><div class='stat-label'>Humidity</div></div>"
            "<div class='stat'><div class='stat-val'>%d</div><div class='stat-label'>AQI</div></div>"
            "</div></div></div>",
            last_snapshot.pm2_5,
            last_snapshot.temperature,
            last_snapshot.humidity,
            last_aqi_score);
    }
    esp_send_html_chunk(link_id, sensor_card);
    ThisThread::sleep_for(30ms);
    
    // WiFi networks card header
    const char* wifi_card_top = 
        "<div class='card'>"
        "<div style='display:flex;justify-content:space-between;align-items:center;padding-right:12px'>"
        "<div class='card-title'>WiFi Networks</div>"
        "<button class='refresh-btn' onclick=\"location.reload()\">&#x21bb; Scan</button>"
        "</div>";
    esp_send_html_chunk(link_id, wifi_card_top);
    ThisThread::sleep_for(20ms);
    
    // Build network list items
    for (int i = 0; i < scanned_count && i < 15; i++) {
        // Determine signal strength level (1-4 bars)
        int bars = 1;
        if (scanned_nets[i].rssi > -50) bars = 4;
        else if (scanned_nets[i].rssi > -65) bars = 3;
        else if (scanned_nets[i].rssi > -80) bars = 2;
        
        bool has_lock = (scanned_nets[i].security > 0);
        bool is_connected = (connected_ssid[0] != '\0' && strcmp(scanned_nets[i].ssid, connected_ssid) == 0);
        
        char net_item[768];
        if (is_connected) {
            snprintf(net_item, sizeof(net_item),
                "<div style='background:#f0faf0;border-bottom:0.5px solid #e8e8ed'>"
                "<div class='net-item' style='background:transparent'>"
                "<div class='net-name'>%s "
                "<span class='badge badge-green'>Connected</span></div>"
                "%s"
                "<div class='signal s%d'>"
                "<div class='bar b1%s'></div>"
                "<div class='bar b2%s'></div>"
                "<div class='bar b3%s'></div>"
                "<div class='bar b4%s'></div>"
                "</div></div>"
                "<div style='padding:0 20px 12px;display:flex;gap:8px'>"
                "<a href='/continue' class='btn btn-join' "
                "style='flex:1;text-align:center;text-decoration:none;padding:10px;font-size:14px'>"
                "Continue</a>"
                "<a href='/disconnect' class='btn btn-cancel' "
                "style='flex:1;text-align:center;text-decoration:none;padding:10px;font-size:14px'>"
                "Disconnect</a></div></div>",
                scanned_nets[i].ssid,
                has_lock ? "<div class='net-lock'>&#x1f512;</div>" : "",
                bars,
                bars >= 1 ? " active" : "",
                bars >= 2 ? " active" : "",
                bars >= 3 ? " active" : "",
                bars >= 4 ? " active" : "");
        } else {
            snprintf(net_item, sizeof(net_item),
                "<div class='net-item' onclick=\"openModal('%s',%d)\">"
                "<div class='net-name'>%s</div>"
                "%s"
                "<div class='signal s%d'>"
                "<div class='bar b1%s'></div>"
                "<div class='bar b2%s'></div>"
                "<div class='bar b3%s'></div>"
                "<div class='bar b4%s'></div>"
                "</div></div>",
                scanned_nets[i].ssid,
                has_lock ? 1 : 0,
                scanned_nets[i].ssid,
                has_lock ? "<div class='net-lock'>&#x1f512;</div>" : "",
                bars,
                bars >= 1 ? " active" : "",
                bars >= 2 ? " active" : "",
                bars >= 3 ? " active" : "",
                bars >= 4 ? " active" : "");
        }
        esp_send_html_chunk(link_id, net_item);
        ThisThread::sleep_for(20ms);
    }
    
    if (scanned_count == 0) {
        esp_send_html_chunk(link_id, 
            "<div style='padding:20px;text-align:center;color:#86868b'>"
            "No networks found. Tap Scan to retry.</div>");
    }
    
    // Close WiFi card
    esp_send_html_chunk(link_id, "</div>");
    ThisThread::sleep_for(20ms);
    
    // Hidden network / manual entry card
    const char* manual_card = 
        "<div class='card'>"
        "<div class='card-title'>Other Network</div>"
        "<form id='manual-form' method='POST' action='http://192.168.4.1/connect' style='padding:12px 20px 16px'>"
        "<input type='text' name='ssid' placeholder='Network Name (SSID)' "
        "style='margin-bottom:10px' required>"
        "<input type='password' name='pass' placeholder='Password'>"
        "<button type='submit' class='btn btn-join' style='width:100%;margin-top:14px'>"
        "Connect</button></form></div>";
    esp_send_html_chunk(link_id, manual_card);
    ThisThread::sleep_for(20ms);
    
    // Footer with system info
    const char* footer = 
        "<div style='text-align:center;padding:16px;color:#aeaeb2;font-size:12px'>"
        "Ember Air Quality Monitor &bull; FRDM-K64F<br>"
        "Connect to a WiFi network to begin monitoring"
        "</div>";
    esp_send_html_chunk(link_id, footer);
    ThisThread::sleep_for(20ms);
    
    // Modal dialog + JavaScript (XHR for captive portal compatibility)
    const char* modal_js = 
        "<div class='modal-overlay' id='modal'>"
        "<div class='modal'>"
        "<h2 id='m-title'></h2>"
        "<p class='msub' id='m-sub'></p>"
        "<input type='hidden' id='m-ssid'>"
        "<div id='m-pf'>"
        "<input type='password' id='m-pass' placeholder='Password'>"
        "</div>"
        "<p id='m-err' style='color:#ff3b30;font-size:13px;margin-top:8px;display:none'></p>"
        "<div class='btn-row'>"
        "<button class='btn btn-cancel' onclick='closeModal()'>Cancel</button>"
        "<button class='btn btn-join' id='m-btn' onclick='doJoin()'>Join</button>"
        "</div></div></div>"
        "<script>"
        "function openModal(s,l){"
        "document.getElementById('m-title').textContent=s;"
        "document.getElementById('m-ssid').value=s;"
        "document.getElementById('m-sub').textContent="
        "l?'Enter the password for this network.':'This network is open.';"
        "document.getElementById('m-pf').style.display=l?'block':'none';"
        "document.getElementById('m-err').style.display='none';"
        "document.getElementById('m-btn').textContent='Join';"
        "document.getElementById('m-btn').disabled=false;"
        "document.getElementById('modal').classList.add('show');"
        "if(l)setTimeout(function(){document.getElementById('m-pass').focus()},200)}"
        "function closeModal(){"
        "document.getElementById('modal').classList.remove('show');"
        "document.getElementById('m-pass').value=''}"
        "document.getElementById('modal').addEventListener('click',function(e){"
        "if(e.target===this)closeModal()})\n"
        "function sendWifi(s,p,btn){"
        "btn.textContent='Connecting...';"
        "btn.disabled=true;"
        "var x=new XMLHttpRequest();"
        "x.timeout=25000;"
        "x.open('POST','http://192.168.4.1/connect',true);"
        "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
        "x.onload=function(){"
        "document.open();document.write(x.responseText);document.close()};"
        "x.onerror=function(){"
        "btn.textContent='Retry';btn.disabled=false;"
        "var e=document.getElementById('m-err');"
        "if(e){e.textContent='Connection error. Try again.';e.style.display='block'}};"
        "x.ontimeout=x.onerror;"
        "x.send('ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))}\n"
        "function doJoin(){"
        "sendWifi(document.getElementById('m-ssid').value,"
        "document.getElementById('m-pass').value,"
        "document.getElementById('m-btn'))}\n"
        "document.getElementById('m-pass').onkeydown=function(e){"
        "if(e.key=='Enter'){e.preventDefault();doJoin()}}\n"
        "var mf=document.getElementById('manual-form');"
        "if(mf){mf.onsubmit=function(e){e.preventDefault();"
        "sendWifi(this.ssid.value,this.pass.value,"
        "this.querySelector('.btn-join'))}}\n"
        "</script></div></body></html>";
    esp_send_html_chunk(link_id, modal_js);
    ThisThread::sleep_for(30ms);
}

// --- Send success/redirect page ---
void sendSuccessPage(int link_id, const char* ssid, const char* ip) {
    char page[1024];
    snprintf(page, sizeof(page),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Connected!</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
        "background:#f5f5f7;display:flex;align-items:center;justify-content:center;min-height:100vh}"
        ".done{text-align:center;padding:40px}"
        ".check{width:64px;height:64px;background:#34c759;border-radius:50%%;"
        "display:flex;align-items:center;justify-content:center;margin:0 auto 20px;"
        "font-size:32px;color:#fff;box-shadow:0 4px 16px rgba(52,199,89,0.3)}"
        "h1{font-size:24px;font-weight:700;margin-bottom:8px}"
        "p{color:#86868b;font-size:15px;line-height:1.5}"
        ".ip{background:#fff;border-radius:12px;padding:12px 20px;margin-top:16px;"
        "font-size:14px;color:#1d1d1f;display:inline-block;box-shadow:0 1px 3px rgba(0,0,0,0.08)}"
        "</style></head><body><div class='done'>"
        "<div class='check'>&#x2713;</div>"
        "<h1>Connected!</h1>"
        "<p>Ember is now connected to<br><strong>%s</strong></p>"
        "<div class='ip'>IP: %s</div>"
        "<p style='margin-top:20px;font-size:13px'>This portal will close.<br>"
        "Ember is now monitoring air quality.</p>"
        "</div></body></html>",
        ssid, ip);
    esp_send_html_chunk(link_id, page);
}

// --- Send failure page ---
void sendFailPage(int link_id, const char* ssid) {
    char page[768];
    snprintf(page, sizeof(page),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Failed</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
        "background:#f5f5f7;display:flex;align-items:center;justify-content:center;min-height:100vh}"
        ".fail{text-align:center;padding:40px}"
        ".x{width:64px;height:64px;background:#ff3b30;border-radius:50%%;"
        "display:flex;align-items:center;justify-content:center;margin:0 auto 20px;"
        "font-size:32px;color:#fff}"
        "h1{font-size:24px;font-weight:700;margin-bottom:8px}"
        "p{color:#86868b;font-size:15px}"
        "a{color:#0071e3;text-decoration:none;font-weight:600}"
        "</style></head><body><div class='fail'>"
        "<div class='x'>&#x2717;</div>"
        "<h1>Connection Failed</h1>"
        "<p>Could not connect to <strong>%s</strong><br>"
        "Check password and try again.</p>"
        "<p style='margin-top:20px'><a href='/'>&#x2190; Back to Setup</a></p>"
        "</div></body></html>",
        ssid);
    esp_send_html_chunk(link_id, page);
}

// --- Start SoftAP Mode ---
void startSoftAP() {
    char resp[512];
    pc_print("\r\n=== Starting SoftAP Web Portal ===\r\n");
    
    // Set AP+Station mode
    esp_send_cmd("AT+CWMODE=3", resp, sizeof(resp), 2000);
    ThisThread::sleep_for(500ms);
    
    // Configure the access point: "Ember-Setup", open, channel 1
    esp_send_cmd("AT+CWSAP=\"Ember-Setup\",\"\",1,0,4,0", resp, sizeof(resp), 3000);
    pc_print(resp);
    ThisThread::sleep_for(500ms);
    
    // Enable multiple connections
    esp_send_cmd("AT+CIPMUX=1", resp, sizeof(resp), 2000);
    ThisThread::sleep_for(500ms);
    
    // Start TCP server on port 80
    esp_send_cmd("AT+CIPSERVER=1,80", resp, sizeof(resp), 2000);
    pc_print(resp);
    
    // Get AP IP
    esp_send_cmd("AT+CIPAP?", resp, sizeof(resp), 2000);
    pc_print(resp);
    
    softap_mode = true;
    pin_verified = false;

    // Generate random 4-digit PIN
    srand((unsigned)us_ticker_read());
    int pin_num = rand() % 10000;
    snprintf(setup_pin, sizeof(setup_pin), "%04d", pin_num);

    pc_print("\r\n[SoftAP] Hotspot 'Ember-Setup' is LIVE\r\n");
    pc_print("[SoftAP] Connect your phone/laptop and open 192.168.4.1\r\n");
    char pin_msg[64];
    snprintf(pin_msg, sizeof(pin_msg), "\r\n  *** SETUP PIN: %s ***\r\n\r\n", setup_pin);
    pc_print(pin_msg);
    
    // Do initial WiFi scan
    scanWiFiNetworks();
}

// --- Stop SoftAP and switch to station mode ---
void stopSoftAP() {
    char resp[256];
    pc_print("[SoftAP] Shutting down portal...\r\n");
    
    // Stop TCP server
    esp_send_cmd("AT+CIPSERVER=0", resp, sizeof(resp), 2000);
    ThisThread::sleep_for(500ms);
    
    // Disable multiple connections
    esp_send_cmd("AT+CIPMUX=0", resp, sizeof(resp), 2000);
    ThisThread::sleep_for(500ms);
    
    // Switch to station-only mode
    esp_send_cmd("AT+CWMODE=1", resp, sizeof(resp), 2000);
    ThisThread::sleep_for(500ms);
    
    softap_mode = false;
    pc_print("[SoftAP] Portal closed\r\n");
}

// --- Handle incoming TCP data (HTTP requests) ---
// Called from main loop when in SoftAP mode
// Returns true when WiFi connection is established (exit portal)
//
// Uses a PERSISTENT static buffer that accumulates data across calls.
// This prevents data loss when +IPD arrives between polling cycles.
bool handleSoftAPClient() {
    static char buf[2048];
    static int buf_pos = 0;

    // Drain all available UART data into static buffer
    while (esp.readable() && buf_pos < (int)sizeof(buf) - 1) {
        int n = esp.read(buf + buf_pos, sizeof(buf) - 1 - buf_pos);
        if (n > 0) buf_pos += n;
    }
    buf[buf_pos] = '\0';

    if (buf_pos == 0) return false;

    // --- Log any STA connect / disconnect events for debug ---
    if (strstr(buf, "+STA_CONNECTED")) {
        pc_print("[AP] Device connected to hotspot\r\n");
    }
    if (strstr(buf, "+STA_DISCONNECTED")) {
        pc_print("[AP] Device left hotspot\r\n");
    }

    // --- Check for +IPD (incoming TCP data) ---
    char* ipd = strstr(buf, "+IPD,");
    if (!ipd) {
        // No HTTP request yet.  Print debug if there IS data (CONNECT/CLOSED etc.)
        if (buf_pos > 0) {
            // Trim to printable preview
            char preview[80];
            int plen = buf_pos > 60 ? 60 : buf_pos;
            // Replace CR/LF with '|' for readable one-line debug
            for (int i = 0; i < plen; i++)
                preview[i] = (buf[i] == '\r' || buf[i] == '\n') ? '|' : buf[i];
            preview[plen] = '\0';
            char dbg[160];
            snprintf(dbg, sizeof(dbg), "[ESP] (%d bytes) %s\r\n", buf_pos, preview);
            pc_print(dbg);
            // Clear consumed non-IPD data to prevent buffer growth
            buf_pos = 0;
            buf[0] = '\0';
        }
        return false;
    }

    // We have +IPD — but we may not have the full HTTP request yet.
    // Wait up to 2 s for the rest of the data (headers + body).
    Timer t;
    t.start();
    bool have_headers = false;
    while (t.elapsed_time() < 2s) {
        // Try draining more data
        while (esp.readable() && buf_pos < (int)sizeof(buf) - 1) {
            int n = esp.read(buf + buf_pos, sizeof(buf) - 1 - buf_pos);
            if (n > 0) buf_pos += n;
        }
        buf[buf_pos] = '\0';
        // Re-locate +IPD (pointer might have shifted if buf was realloc'd — but it's static so OK)
        ipd = strstr(buf, "+IPD,");
        char* colon = ipd ? strchr(ipd, ':') : nullptr;
        if (colon && strstr(colon, "\r\n\r\n")) {
            // Have the full HTTP headers (and possibly POST body)
            have_headers = true;
            // Grab a bit more data in case POST body is still arriving
            ThisThread::sleep_for(100ms);
            while (esp.readable() && buf_pos < (int)sizeof(buf) - 1) {
                int n = esp.read(buf + buf_pos, sizeof(buf) - 1 - buf_pos);
                if (n > 0) buf_pos += n;
            }
            buf[buf_pos] = '\0';
            break;
        }
        ThisThread::sleep_for(20ms);
    }

    if (!have_headers) {
        pc_print("[TCP] Got +IPD but incomplete request - discarding\r\n");
        buf_pos = 0;
        buf[0] = '\0';
        return false;
    }

    // Parse link_id
    int link_id = 0;
    sscanf(ipd + 5, "%d", &link_id);

    // Find HTTP data after the ':'
    char* http_data = strchr(ipd, ':');
    if (!http_data) { buf_pos = 0; buf[0] = '\0'; return false; }
    http_data++;

    char dbg[128];
    // Extract first line of HTTP request for debug
    {
        char first_line[80];
        int fl = 0;
        while (http_data[fl] && http_data[fl] != '\r' && fl < 79) { first_line[fl] = http_data[fl]; fl++; }
        first_line[fl] = '\0';
        snprintf(dbg, sizeof(dbg), "[TCP] Client %d: %s\r\n", link_id, first_line);
        pc_print(dbg);
    }

    // ===== CAPTIVE PORTAL DETECTION =====
    if (strstr(http_data, "/hotspot-detect") ||
        strstr(http_data, "/library/test/success") ||
        strstr(http_data, "captive.apple.com") ||
        strstr(http_data, "/generate_204") ||
        strstr(http_data, "/gen_204") ||
        strstr(http_data, "connectivitycheck") ||
        strstr(http_data, "/connecttest.txt") ||
        strstr(http_data, "/redirect") ||
        strstr(http_data, "detectportal.firefox"))
    {
        pc_print("[TCP] Captive portal probe\r\n");
        if (pin_verified) {
            buildAndSendConfigPage(link_id);
        } else {
            buildAndSendPinPage(link_id, false);
        }
        ThisThread::sleep_for(300ms);
        char close_cmd[32]; char resp2[64];
        snprintf(close_cmd, sizeof(close_cmd), "AT+CIPCLOSE=%d", link_id);
        esp_send_cmd(close_cmd, resp2, sizeof(resp2), 1000);
        buf_pos = 0; buf[0] = '\0';
        return false;
    }

    // ===== POST /verify-pin =====
    if (strstr(http_data, "POST /verify-pin")) {
        pc_print("[TCP] PIN verification attempt\r\n");
        char* body = strstr(http_data, "\r\n\r\n");
        char entered_pin[8] = "";
        if (body) {
            body += 4;
            // Try hidden 'pin' field first
            char* pp = strstr(body, "pin=");
            if (pp) {
                pp += 4;
                int i = 0;
                while (pp[i] && pp[i] != '&' && pp[i] != ' ' && pp[i] != '\r' && i < 4) {
                    entered_pin[i] = pp[i]; i++;
                }
                entered_pin[i] = '\0';
            }
            // Fallback: parse d1,d2,d3,d4 individually (iOS onclick may not fire)
            if (strlen(entered_pin) < 4) {
                char digits[5] = "";
                const char* dnames[] = {"d1=", "d2=", "d3=", "d4="};
                for (int d = 0; d < 4; d++) {
                    char* dp = strstr(body, dnames[d]);
                    if (dp) digits[d] = dp[3]; else digits[d] = '0';
                }
                digits[4] = '\0';
                if (strlen(digits) == 4) {
                    strncpy(entered_pin, digits, 5);
                }
            }
        }
        if (strcmp(entered_pin, setup_pin) == 0) {
            pc_print("[TCP] PIN correct!\r\n");
            pin_verified = true;
            // Drain UART then serve config page
            ThisThread::sleep_for(100ms);
            { char junk[256]; while (esp.readable()) esp.read(junk, sizeof(junk)); }
            buildAndSendConfigPage(link_id);
        } else {
            pc_print("[TCP] PIN incorrect\r\n");
            buildAndSendPinPage(link_id, true);
        }
        ThisThread::sleep_for(300ms);
        char close_cmd[32]; char resp2[64];
        snprintf(close_cmd, sizeof(close_cmd), "AT+CIPCLOSE=%d", link_id);
        esp_send_cmd(close_cmd, resp2, sizeof(resp2), 1000);
        buf_pos = 0; buf[0] = '\0';
        return false;
    }

    // ===== POST /connect — WiFi provisioning =====
    if (strstr(http_data, "POST /connect")) {
        pc_print("[TCP] WiFi connect request received\r\n");

        char* body = strstr(http_data, "\r\n\r\n");
        if (body) {
            body += 4;
            char ssid_raw[64] = "", pass_raw[64] = "";
            char ssid[64] = "", pass[64] = "";

            char* ssid_ptr = strstr(body, "ssid=");
            if (ssid_ptr) {
                ssid_ptr += 5;
                char* end = strchr(ssid_ptr, '&');
                int len = end ? (end - ssid_ptr) : (int)strlen(ssid_ptr);
                if (len > 63) len = 63;
                strncpy(ssid_raw, ssid_ptr, len);
                ssid_raw[len] = '\0';
                url_decode(ssid_raw, ssid, sizeof(ssid));
            }
            char* pass_ptr = strstr(body, "pass=");
            if (pass_ptr) {
                pass_ptr += 5;
                char* end = strchr(pass_ptr, '&');
                if (!end) end = pass_ptr + strlen(pass_ptr);
                while (end > pass_ptr && (*(end-1)=='\r'||*(end-1)=='\n'||*(end-1)==' ')) end--;
                int len = end - pass_ptr;
                if (len > 63) len = 63;
                strncpy(pass_raw, pass_ptr, len);
                pass_raw[len] = '\0';
                url_decode(pass_raw, pass, sizeof(pass));
            }

            if (strlen(ssid) > 0) {
                char cmsg[128];
                snprintf(cmsg, sizeof(cmsg), "[WiFi] Will connect to: SSID='%s'\r\n", ssid);
                pc_print(cmsg);

                // ---- SEND "CONNECTING" PAGE FIRST ----
                // ESP32 kills TCP when joining WiFi, so we must respond NOW.
                // Send in 2 parts to avoid snprintf %% escaping issues.
                {
                    const char* conn_hdr =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=UTF-8\r\n"
                        "Cache-Control: no-store\r\n"
                        "Connection: close\r\n\r\n";
                    esp_send_data(link_id, conn_hdr, strlen(conn_hdr));
                    ThisThread::sleep_for(30ms);

                    // Part 1: HTML + CSS (no % characters)
                    const char* conn_p1 =
                        "<!DOCTYPE html><html><head>"
                        "<meta charset='UTF-8'>"
                        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                        "<title>Connecting</title>"
                        "<style>"
                        "*{margin:0;padding:0;box-sizing:border-box}"
                        "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
                        "background:#f5f5f7;display:flex;align-items:center;"
                        "justify-content:center;min-height:100vh;text-align:center}"
                        ".w{padding:40px}"
                        "@keyframes spin{to{transform:rotate(360deg)}}"
                        ".sp{width:48px;height:48px;border:4px solid #e8e8ed;"
                        "border-top-color:#0071e3;border-radius:100%;"
                        "animation:spin .8s linear infinite;margin:0 auto 24px}"
                        "h1{font-size:22px;font-weight:700;margin-bottom:8px}"
                        "p{color:#86868b;font-size:15px}"
                        ".st{margin-top:20px;padding:12px 20px;background:#fff;"
                        "border-radius:12px;font-size:14px;color:#86868b;"
                        "box-shadow:0 1px 3px rgba(0,0,0,.08)}"
                        ".ok{background:#34c759;color:#fff;width:64px;height:64px;"
                        "border-radius:100%;display:none;align-items:center;"
                        "justify-content:center;margin:0 auto 20px;font-size:32px}"
                        ".fl{background:#ff3b30;color:#fff;width:64px;height:64px;"
                        "border-radius:100%;display:none;align-items:center;"
                        "justify-content:center;margin:0 auto 20px;font-size:32px}"
                        "a{color:#0071e3;text-decoration:none}"
                        "</style></head><body><div class='w'>"
                        "<div class='sp' id='sp'></div>"
                        "<div class='ok' id='ok'>&#10003;</div>"
                        "<div class='fl' id='fl'>&#10007;</div>";
                    esp_send_html_chunk(link_id, conn_p1);
                    ThisThread::sleep_for(30ms);

                    // Part 2: Dynamic content (SSID)
                    char conn_p2[256];
                    snprintf(conn_p2, sizeof(conn_p2),
                        "<h1 id='t'>Connecting...</h1>"
                        "<p>Joining <b>%s</b></p>"
                        "<div class='st' id='st'>Setting up WiFi</div>",
                        ssid);
                    esp_send_html_chunk(link_id, conn_p2);
                    ThisThread::sleep_for(30ms);

                    // Part 3: JavaScript - timed success (no /status polling needed)
                    // The phone can't reliably reach the server during CWJAP,
                    // so we show a timed animation then display success.
                    const char* conn_p3 =
                        "<script>"
                        "var n=0;"
                        "var m=['Setting up WiFi','Authenticating...','Getting IP address...','Almost there...','Finalizing connection...'];"
                        "var iv=setInterval(function(){n++;"
                        "if(n<m.length)document.getElementById('st').textContent=m[n];"
                        "if(n>=12){"
                        "clearInterval(iv);"
                        "document.getElementById('sp').style.display='none';"
                        "document.getElementById('ok').style.display='flex';"
                        "document.getElementById('t').textContent='Connected!';"
                        "document.getElementById('st').innerHTML="
                        "'<b>WiFi setup complete</b><br><br>"
                        "<small style=color:#86868b>Ember is now monitoring air quality.<br>"
                        "You can close this page.</small>'"
                        "}},1500);"
                        "</script></div></body></html>";
                    esp_send_html_chunk(link_id, conn_p3);
                    ThisThread::sleep_for(500ms);

                    char close_cmd2[32]; char resp2[64];
                    snprintf(close_cmd2, sizeof(close_cmd2), "AT+CIPCLOSE=%d", link_id);
                    esp_send_cmd(close_cmd2, resp2, sizeof(resp2), 1000);
                    pc_print("[TCP] Sent connecting page, now doing CWJAP\r\n");
                }

                // ---- NOW CONNECT TO WIFI ----
                // Drain UART before calling CWJAP
                { char junk[256]; while (esp.readable()) esp.read(junk, sizeof(junk)); }
                ThisThread::sleep_for(200ms);

                // Stop TCP server and switch to Station mode for clean CWJAP
                char stop_resp[64];
                esp_send_cmd("AT+CIPSERVER=0", stop_resp, sizeof(stop_resp), 1000);
                ThisThread::sleep_for(200ms);
                esp_send_cmd("AT+CIPMUX=0", stop_resp, sizeof(stop_resp), 1000);
                ThisThread::sleep_for(100ms);
                esp_send_cmd("AT+CWMODE=1", stop_resp, sizeof(stop_resp), 1000);
                ThisThread::sleep_for(300ms);
                // Drain again after mode switch
                { char junk[256]; while (esp.readable()) esp.read(junk, sizeof(junk)); }

                char cmd[256]; char resp[1024];
                snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
                
                // Debug: print exact command (mask password)
                char dbg[256];
                snprintf(dbg, sizeof(dbg), "[WiFi] CMD: AT+CWJAP=\"%s\",\"***\" (passlen=%d)\r\n", ssid, (int)strlen(pass));
                pc_print(dbg);
                
                esp_send_cmd(cmd, resp, sizeof(resp), 20000);
                
                // Debug: print response
                pc_print("[WiFi] CWJAP response: ");
                pc_print(resp);
                pc_print("\r\n");

                // Check for connection — accept either WIFI GOT IP or just OK with no ERROR/FAIL
                bool cwjap_ok = false;
                if (strstr(resp, "WIFI GOT IP")) {
                    cwjap_ok = true;
                } else if (resp_ok(resp) && !strstr(resp, "FAIL") && !strstr(resp, "ERROR")) {
                    // Some firmware versions don't echo "WIFI GOT IP" in response
                    // Verify by checking CWJAP? status
                    ThisThread::sleep_for(500ms);
                    char verify[256];
                    esp_send_cmd("AT+CWJAP?", verify, sizeof(verify), 3000);
                    if (strstr(verify, "+CWJAP:")) cwjap_ok = true;
                }

                if (cwjap_ok) {
                    char ip_resp[256];
                    esp_send_cmd("AT+CIFSR", ip_resp, sizeof(ip_resp), 3000);
                    char ip[32] = "0.0.0.0";
                    char* ip_start = strstr(ip_resp, "STAIP,\"");
                    if (ip_start) {
                        ip_start += 7;
                        char* ip_end = strchr(ip_start, '"');
                        if (ip_end && (ip_end - ip_start) < 31) {
                            int iplen = ip_end - ip_start;
                            strncpy(ip, ip_start, iplen);
                            ip[iplen] = '\0';
                        }
                    }
                    snprintf(cmsg, sizeof(cmsg), "[WiFi] CONNECTED! IP=%s\r\n", ip);
                    pc_print(cmsg);
                    saveWiFiCreds(ssid, pass);
                    strncpy(connected_ssid, ssid, 63);
                    connected_ssid[63] = '\0';

                    // Enable auto-connect so ESP32 remembers this WiFi
                    esp_send_cmd("AT+CWAUTOCONN=1", resp, sizeof(resp), 1000);

                    wifi_connected = true;
                    buf_pos = 0; buf[0] = '\0';
                    return true;
                } else {
                    pc_print("[WiFi] Connection failed\r\n");
                    // Re-enable SoftAP server so user can retry
                    esp_send_cmd("AT+CIPMUX=1", resp, sizeof(resp), 1000);
                    ThisThread::sleep_for(200ms);
                    esp_send_cmd("AT+CIPSERVER=1,80", resp, sizeof(resp), 1000);
                    ThisThread::sleep_for(500ms);
                    buf_pos = 0; buf[0] = '\0';
                    scanWiFiNetworks();
                }
            }
        }
    }
    // ===== GET /continue (skip setup, keep current WiFi) =====
    else if (strstr(http_data, "GET /continue")) {
        pc_print("[SoftAP] User pressed Continue - exiting setup\r\n");
        const char* ok_page =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
            "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<style>body{font-family:-apple-system,sans-serif;background:#f5f5f7;"
            "display:flex;align-items:center;justify-content:center;min-height:100vh}"
            ".d{text-align:center;padding:40px}"
            ".c{width:64px;height:64px;background:#34c759;border-radius:50%;"
            "display:flex;align-items:center;justify-content:center;margin:0 auto 20px;"
            "font-size:32px;color:#fff}h1{font-size:24px;font-weight:700}"
            "p{color:#86868b;font-size:15px}</style></head>"
            "<body><div class='d'><div class='c'>&#x2713;</div>"
            "<h1>Resuming</h1><p>Ember is returning to<br>monitoring mode.</p>"
            "</div></body></html>";
        esp_send_data(link_id, ok_page, strlen(ok_page));
        ThisThread::sleep_for(300ms);
        char close_cmd[32]; char resp[64];
        snprintf(close_cmd, sizeof(close_cmd), "AT+CIPCLOSE=%d", link_id);
        esp_send_cmd(close_cmd, resp, sizeof(resp), 1000);
        // Signal wifi_connected so the SoftAP loop exits
        wifi_connected = true;
        buf_pos = 0; buf[0] = '\0';
        return true;
    }
    // ===== GET /disconnect =====
    else if (strstr(http_data, "GET /disconnect")) {
        pc_print("[SoftAP] User pressed Disconnect\r\n");
        char dresp[256];
        esp_send_cmd("AT+CWQAP", dresp, sizeof(dresp), 3000);
        connected_ssid[0] = '\0';
        ThisThread::sleep_for(500ms);
        // Redirect back to config page
        const char* redir = "HTTP/1.1 302 Found\r\nLocation: /\r\nConnection: close\r\n\r\n";
        esp_send_data(link_id, redir, strlen(redir));
        ThisThread::sleep_for(200ms);
        char close_cmd[32]; char resp[64];
        snprintf(close_cmd, sizeof(close_cmd), "AT+CIPCLOSE=%d", link_id);
        esp_send_cmd(close_cmd, resp, sizeof(resp), 1000);
        scanWiFiNetworks();
    }
    // ===== GET /scan =====
    else if (strstr(http_data, "GET /scan")) {
        scanWiFiNetworks();
        const char* redir = "HTTP/1.1 302 Found\r\nLocation: /\r\nConnection: close\r\n\r\n";
        esp_send_data(link_id, redir, strlen(redir));
        ThisThread::sleep_for(200ms);
        char close_cmd[32]; char resp[64];
        snprintf(close_cmd, sizeof(close_cmd), "AT+CIPCLOSE=%d", link_id);
        esp_send_cmd(close_cmd, resp, sizeof(resp), 1000);
    }
    // ===== GET / (config page or PIN page) =====
    else {
        // Drain any stale UART data (leftover scan results etc.)
        ThisThread::sleep_for(100ms);
        { char junk[256]; while (esp.readable()) esp.read(junk, sizeof(junk)); }
        if (pin_verified) {
            pc_print("[TCP] Serving config page...\r\n");
            buildAndSendConfigPage(link_id);
        } else {
            pc_print("[TCP] Serving PIN page...\r\n");
            buildAndSendPinPage(link_id, false);
        }
        ThisThread::sleep_for(500ms);
        char close_cmd[32]; char resp[64];
        snprintf(close_cmd, sizeof(close_cmd), "AT+CIPCLOSE=%d", link_id);
        esp_send_cmd(close_cmd, resp, sizeof(resp), 1000);
    }

    // Clear buffer after processing
    buf_pos = 0;
    buf[0] = '\0';
    return false;
}
// ========== END SOFTAP WEB PORTAL ==========

// PMS5003 data structure
struct PMS5003Data {
    uint16_t pm1_0;   // PM1.0 concentration (ug/m3) - standard
    uint16_t pm2_5;   // PM2.5 concentration (ug/m3) - standard
    uint16_t pm10;    // PM10  concentration (ug/m3) - standard
    uint16_t pm1_0_atm; // PM1.0 atmospheric
    uint16_t pm2_5_atm; // PM2.5 atmospheric
    uint16_t pm10_atm;  // PM10  atmospheric
    bool valid;       // true if checksum passed
};

// Read a 32-byte packet from PMS5003 and parse PM values
PMS5003Data readPMS5003() {
    PMS5003Data data = {};
    data.valid = false;
    uint8_t buf[32];
    uint8_t byte;
    Timer t;
    t.start();

    // Search for start bytes 0x42 0x4D within timeout
    while (t.elapsed_time() < 2s) {
        if (!pms_serial.readable()) {
            ThisThread::sleep_for(5ms);
            continue;
        }
        pms_serial.read(&byte, 1);
        if (byte != 0x42) continue;

        // Wait for second start byte
        int waited = 0;
        while (!pms_serial.readable() && waited < 100) {
            ThisThread::sleep_for(1ms);
            waited++;
        }
        if (!pms_serial.readable()) continue;
        pms_serial.read(&byte, 1);
        if (byte != 0x4D) continue;

        // Found header, read remaining 30 bytes
        buf[0] = 0x42;
        buf[1] = 0x4D;
        int idx = 2;
        while (idx < 32 && t.elapsed_time() < 3s) {
            if (pms_serial.readable()) {
                pms_serial.read(&buf[idx], 1);
                idx++;
            } else {
                ThisThread::sleep_for(1ms);
            }
        }
        if (idx < 32) continue; // Incomplete packet

        // Validate checksum: sum of first 30 bytes == last 2 bytes
        uint16_t checksum = 0;
        for (int i = 0; i < 30; i++) {
            checksum += buf[i];
        }
        uint16_t pkt_checksum = (buf[30] << 8) | buf[31];
        if (checksum != pkt_checksum) {
            continue;  // Silent retry
        }

        // Parse data (big-endian, bytes 4-15 are PM values)
        data.pm1_0     = (buf[4]  << 8) | buf[5];   // Standard PM1.0
        data.pm2_5     = (buf[6]  << 8) | buf[7];   // Standard PM2.5
        data.pm10      = (buf[8]  << 8) | buf[9];   // Standard PM10
        data.pm1_0_atm = (buf[10] << 8) | buf[11];  // Atmospheric PM1.0
        data.pm2_5_atm = (buf[12] << 8) | buf[13];  // Atmospheric PM2.5
        data.pm10_atm  = (buf[14] << 8) | buf[15];  // Atmospheric PM10
        data.valid = true;
        break;
    }
    t.stop();
    return data;
}

// --- I2C helper functions (shared bus, direct calls) ---
static bool ens160_available = false;
static bool bme680_available = false;
static bool oled_available = false;

bool i2c_write_reg(int addr, char reg, char val) {
    char data[2] = {reg, val};
    int ret = sensor_i2c.write(addr, data, 2);
    if (ret != 0) sensor_i2c.stop();
    return ret == 0;
}

bool i2c_read_reg(int addr, char reg, char* buf, int len) {
    if (sensor_i2c.write(addr, &reg, 1, true) != 0) {
        sensor_i2c.stop();
        return false;
    }
    int ret = sensor_i2c.read(addr, buf, len);
    if (ret != 0) { sensor_i2c.stop(); return false; }
    return true;
}

// ========== SH1106 OLED DRIVER (128x64, I2C, raw commands) ==========
// 5x7 font - ASCII 32-127 (space to ~)
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 space
    {0x00,0x00,0x5F,0x00,0x00}, // 33 !
    {0x00,0x07,0x00,0x07,0x00}, // 34 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 $
    {0x23,0x13,0x08,0x64,0x62}, // 37 %
    {0x36,0x49,0x55,0x22,0x50}, // 38 &
    {0x00,0x05,0x03,0x00,0x00}, // 39 '
    {0x00,0x1C,0x22,0x41,0x00}, // 40 (
    {0x00,0x41,0x22,0x1C,0x00}, // 41 )
    {0x08,0x2A,0x1C,0x2A,0x08}, // 42 *
    {0x08,0x08,0x3E,0x08,0x08}, // 43 +
    {0x00,0x50,0x30,0x00,0x00}, // 44 ,
    {0x08,0x08,0x08,0x08,0x08}, // 45 -
    {0x00,0x60,0x60,0x00,0x00}, // 46 .
    {0x20,0x10,0x08,0x04,0x02}, // 47 /
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 0
    {0x00,0x42,0x7F,0x40,0x00}, // 49 1
    {0x42,0x61,0x51,0x49,0x46}, // 50 2
    {0x21,0x41,0x45,0x4B,0x31}, // 51 3
    {0x18,0x14,0x12,0x7F,0x10}, // 52 4
    {0x27,0x45,0x45,0x45,0x39}, // 53 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 6
    {0x01,0x71,0x09,0x05,0x03}, // 55 7
    {0x36,0x49,0x49,0x49,0x36}, // 56 8
    {0x06,0x49,0x49,0x29,0x1E}, // 57 9
    {0x00,0x36,0x36,0x00,0x00}, // 58 :
    {0x00,0x56,0x36,0x00,0x00}, // 59 ;
    {0x00,0x08,0x14,0x22,0x41}, // 60 <
    {0x14,0x14,0x14,0x14,0x14}, // 61 =
    {0x41,0x22,0x14,0x08,0x00}, // 62 >
    {0x02,0x01,0x51,0x09,0x06}, // 63 ?
    {0x32,0x49,0x79,0x41,0x3E}, // 64 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 A
    {0x7F,0x49,0x49,0x49,0x36}, // 66 B
    {0x3E,0x41,0x41,0x41,0x22}, // 67 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 D
    {0x7F,0x49,0x49,0x49,0x41}, // 69 E
    {0x7F,0x09,0x09,0x01,0x01}, // 70 F
    {0x3E,0x41,0x41,0x51,0x32}, // 71 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 H
    {0x00,0x41,0x7F,0x41,0x00}, // 73 I
    {0x20,0x40,0x41,0x3F,0x01}, // 74 J
    {0x7F,0x08,0x14,0x22,0x41}, // 75 K
    {0x7F,0x40,0x40,0x40,0x40}, // 76 L
    {0x7F,0x02,0x04,0x02,0x7F}, // 77 M
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 N
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 O
    {0x7F,0x09,0x09,0x09,0x06}, // 80 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 82 R
    {0x46,0x49,0x49,0x49,0x31}, // 83 S
    {0x01,0x01,0x7F,0x01,0x01}, // 84 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 V
    {0x7F,0x20,0x18,0x20,0x7F}, // 87 W
    {0x63,0x14,0x08,0x14,0x63}, // 88 X
    {0x03,0x04,0x78,0x04,0x03}, // 89 Y
    {0x61,0x51,0x49,0x45,0x43}, // 90 Z
    {0x00,0x00,0x7F,0x41,0x41}, // 91 [
    {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
    {0x41,0x41,0x7F,0x00,0x00}, // 93 ]
    {0x04,0x02,0x01,0x02,0x04}, // 94 ^
    {0x40,0x40,0x40,0x40,0x40}, // 95 _
    {0x00,0x01,0x02,0x04,0x00}, // 96 `
    {0x20,0x54,0x54,0x54,0x78}, // 97 a
    {0x7F,0x48,0x44,0x44,0x38}, // 98 b
    {0x38,0x44,0x44,0x44,0x20}, // 99 c
    {0x38,0x44,0x44,0x48,0x7F}, // 100 d
    {0x38,0x54,0x54,0x54,0x18}, // 101 e
    {0x08,0x7E,0x09,0x01,0x02}, // 102 f
    {0x08,0x14,0x54,0x54,0x3C}, // 103 g
    {0x7F,0x08,0x04,0x04,0x78}, // 104 h
    {0x00,0x44,0x7D,0x40,0x00}, // 105 i
    {0x20,0x40,0x44,0x3D,0x00}, // 106 j
    {0x00,0x7F,0x10,0x28,0x44}, // 107 k
    {0x00,0x41,0x7F,0x40,0x00}, // 108 l
    {0x7C,0x04,0x18,0x04,0x78}, // 109 m
    {0x7C,0x08,0x04,0x04,0x78}, // 110 n
    {0x38,0x44,0x44,0x44,0x38}, // 111 o
    {0x7C,0x14,0x14,0x14,0x08}, // 112 p
    {0x08,0x14,0x14,0x18,0x7C}, // 113 q
    {0x7C,0x08,0x04,0x04,0x08}, // 114 r
    {0x48,0x54,0x54,0x54,0x20}, // 115 s
    {0x04,0x3F,0x44,0x40,0x20}, // 116 t
    {0x3C,0x40,0x40,0x20,0x7C}, // 117 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 118 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 119 w
    {0x44,0x28,0x10,0x28,0x44}, // 120 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 121 y
    {0x44,0x64,0x54,0x4C,0x44}, // 122 z
    {0x00,0x08,0x36,0x41,0x00}, // 123 {
    {0x00,0x00,0x7F,0x00,0x00}, // 124 |
    {0x00,0x41,0x36,0x08,0x00}, // 125 }
    {0x08,0x08,0x2A,0x1C,0x08}, // 126 ~
};

// Frame buffer for 128x64 display (8 pages x 128 bytes = 1024 bytes)
static uint8_t oled_buf[8][128];

void oled_cmd(uint8_t cmd) {
    char data[2] = {0x00, (char)cmd}; // Co=0, D/C#=0 = command
    int ret = sensor_i2c.write(SH1106_ADDR, data, 2);
    if (ret != 0) sensor_i2c.stop();
}

void oled_init() {
    ThisThread::sleep_for(100ms); // Wait for OLED power up
    
    // Test if OLED actually responds
    char test[1] = {0x00};
    if (sensor_i2c.write(SH1106_ADDR, test, 1) != 0) {
        sensor_i2c.stop();
        pc_print("[OLED] No ACK from 0x3C - disabling OLED\r\n");
        oled_available = false;
        return;
    }
    
    oled_cmd(0xAE); // Display OFF
    oled_cmd(0xD5); oled_cmd(0x80); // Clock div
    oled_cmd(0xA8); oled_cmd(0x3F); // Mux ratio 64
    oled_cmd(0xD3); oled_cmd(0x00); // Display offset 0
    oled_cmd(0x40);                 // Start line 0
    oled_cmd(0xAD); oled_cmd(0x8B); // Charge pump (SH1106 internal DC-DC)
    oled_cmd(0xA1);                 // Segment re-map (flip horizontal)
    oled_cmd(0xC8);                 // COM scan direction (flip vertical)
    oled_cmd(0xDA); oled_cmd(0x12); // COM pins config
    oled_cmd(0x81); oled_cmd(0xFF); // Contrast max
    oled_cmd(0xD9); oled_cmd(0x1F); // Pre-charge period
    oled_cmd(0xDB); oled_cmd(0x40); // VCOMH deselect level
    oled_cmd(0xA4);                 // Resume to RAM content
    oled_cmd(0xA6);                 // Normal display (not inverted)
    oled_cmd(0xAF);                 // Display ON
    ThisThread::sleep_for(50ms);
    pc_print("[OLED] SH1106 initialized OK\r\n");
}

void oled_clear() {
    memset(oled_buf, 0, sizeof(oled_buf));
}

void oled_flush() {
    if (!oled_available) return;
    for (int page = 0; page < 8; page++) {
        oled_cmd(0xB0 + page);       // Set page
        oled_cmd(0x02);              // Lower column = 2 (SH1106 offset)
        oled_cmd(0x10);              // Upper column = 0
        // Send data in 16-byte chunks (safe for I2C buffer)
        for (int offset = 0; offset < 128; offset += 16) {
            char buf[17];
            buf[0] = 0x40; // Co=0, D/C#=1 = data
            memcpy(buf + 1, oled_buf[page] + offset, 16);
            int ret = sensor_i2c.write(SH1106_ADDR, buf, 17);
            if (ret != 0) { sensor_i2c.stop(); return; }
        }
    }
}

void oled_pixel(int x, int y, bool on) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    if (on) oled_buf[y / 8][x] |=  (1 << (y % 8));
    else    oled_buf[y / 8][x] &= ~(1 << (y % 8));
}

void oled_char(int x, int y, char c) {
    if (c < 32 || c > 126) c = 32;
    const uint8_t* glyph = font5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            oled_pixel(x + col, y + row, glyph[col] & (1 << row));
        }
    }
}

void oled_str(int x, int y, const char* str) {
    while (*str) {
        oled_char(x, y, *str++);
        x += 6; // 5px char + 1px space
    }
}

// Draw big digit (2x size) for AQI score
void oled_char_big(int x, int y, char c) {
    if (c < 32 || c > 126) c = 32;
    const uint8_t* glyph = font5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            bool on = glyph[col] & (1 << row);
            oled_pixel(x + col*2,     y + row*2,     on);
            oled_pixel(x + col*2 + 1, y + row*2,     on);
            oled_pixel(x + col*2,     y + row*2 + 1, on);
            oled_pixel(x + col*2 + 1, y + row*2 + 1, on);
        }
    }
}

void oled_str_big(int x, int y, const char* str) {
    while (*str) {
        oled_char_big(x, y, *str++);
        x += 12; // 10px char + 2px space
    }
}

// Medium font (1.4x scale = 7x10 pixels per character)
void oled_char_med(int x, int y, char c) {
    if (c < 32 || c > 126) c = 32;
    const uint8_t* glyph = font5x7[c - 32];
    for (int col = 0; col < 7; col++) {
        int src_col = col * 5 / 7;
        for (int row = 0; row < 10; row++) {
            int src_row = row * 7 / 10;
            bool on = glyph[src_col] & (1 << src_row);
            oled_pixel(x + col, y + row, on);
        }
    }
}

void oled_str_med(int x, int y, const char* str) {
    while (*str) {
        oled_char_med(x, y, *str++);
        x += 8; // 7px char + 1px space
    }
}

// Center a medium-font string on screen (128px wide, 8px per char)
void oled_str_med_c(int y, const char* str) {
    int len = strlen(str);
    int x = (128 - len * 8) / 2;
    if (x < 0) x = 0;
    oled_str_med(x, y, str);
}

// Draw horizontal progress bar
void oled_bar(int x, int y, int w, int h, int pct) {
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    int fill = (w * pct) / 100;
    // Border
    for (int i = x; i < x + w; i++) { oled_pixel(i, y, true); oled_pixel(i, y + h - 1, true); }
    for (int j = y; j < y + h; j++) { oled_pixel(x, j, true); oled_pixel(x + w - 1, j, true); }
    // Fill
    for (int i = x + 1; i < x + 1 + fill; i++)
        for (int j = y + 1; j < y + h - 1; j++)
            oled_pixel(i, j, true);
}

// Invert a rectangular region (for flashing DANGER)
static bool oled_invert_flag = false;
void oled_invert_rect(int x, int y, int w, int h) {
    for (int i = x; i < x + w && i < 128; i++)
        for (int j = y; j < y + h && j < 64; j++)
            oled_pixel(i, j, true);
}

// ========== DIGITAL TWIN: Push device status to server ==========
// POSTs current alarm state to /device_status so the web control panel
// can sync its toggle switch, show cause text, and reflect cable state.
void sendDeviceStatusToAPI(bool armed, bool active, const char* cause) {
    if (!wifi_connected || softap_mode) return;

    char body[256];
    int body_len = snprintf(body, sizeof(body),
        "{\"device_id\":\"K64F-ember\","
        "\"alarm_armed\":%s,"
        "\"alarm_active\":%s,"
        "\"fire_alert\":%s,"
        "\"cause\":\"%s\"}",
        armed ? "true" : "false",
        active ? "true" : "false",
        fire_alert_active ? "true" : "false",
        cause);

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "AT+HTTPCPOST=\"https://%s/device_status\",%d,1,\"Content-Type: application/json\"",
        AI_SERVER_HOST, body_len);

    { char junk[256]; while (esp.readable()) { esp.read(junk, sizeof(junk)); ThisThread::sleep_for(5ms); } }

    esp.write(cmd, strlen(cmd));
    esp.write("\r\n", 2);

    Timer t;
    t.start();
    bool found_prompt = false;
    while (t.elapsed_time() < 3s) {
        if (esp.readable()) {
            char c;
            if (esp.read(&c, 1) == 1 && c == '>') { found_prompt = true; break; }
        } else {
            ThisThread::sleep_for(5ms);
        }
    }
    if (!found_prompt) return;

    esp.write(body, body_len);

    char resp[256];
    memset(resp, 0, sizeof(resp));
    int total = 0;
    t.reset();
    while (t.elapsed_time() < 3s && total < (int)sizeof(resp) - 1) {
        if (esp.readable()) {
            int n = esp.read(resp + total, sizeof(resp) - 1 - total);
            if (n > 0) total += n;
            if (strstr(resp, "\r\nOK") || strstr(resp, "ERROR")) break;
        } else {
            ThisThread::sleep_for(10ms);
        }
    }

    char pmsg[128];
    snprintf(pmsg, sizeof(pmsg), "[DT-SYNC] Status sent: armed=%s active=%s cause=%s\r\n",
        armed ? "Y" : "N", active ? "Y" : "N", cause);
    pc_print(pmsg);
}

// ========== M5: FIRE ALERT — HTTP POST & SD BASELINE LOG ==========

void sendFireAlertToAPI(float score, float pm_delta, float mq_delta, float temp_delta) {
    if (!wifi_connected || softap_mode) {
        pc_print("[M5-FIRE] No WiFi — skipping API POST\r\n");
        return;
    }

    // Build JSON body
    char body[512];
    int body_len = snprintf(body, sizeof(body),
        "{\"device_id\":\"K64F-ember\",\"event\":\"FIRE_ALERT\","
        "\"fire_score\":%.3f,\"pm_delta\":%.2f,\"mq_delta\":%.4f,\"temp_delta\":%.2f,"
        "\"pm2_5\":%u,\"pm10\":%u,\"temperature\":%.1f,\"humidity\":%.1f,"
        "\"mq_analog\":%.4f,\"mq_digital\":%d,\"tvoc\":%u,\"eco2\":%u}",
        score, pm_delta, mq_delta, temp_delta,
        last_snapshot.pm2_5, last_snapshot.pm10,
        last_snapshot.temperature, last_snapshot.humidity,
        last_snapshot.mq_analog, last_snapshot.mq_digital,
        last_snapshot.tvoc, last_snapshot.eco2);

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "AT+HTTPCPOST=\"https://%s/fire_alert\",%d,1,\"Content-Type: application/json\"",
        AI_SERVER_HOST, body_len);

    // Drain stale serial data
    { char junk[256]; while (esp.readable()) { esp.read(junk, sizeof(junk)); ThisThread::sleep_for(5ms); } }

    esp.write(cmd, strlen(cmd));
    esp.write("\r\n", 2);

    // Wait for '>' prompt (up to 5s)
    Timer t;
    t.start();
    bool found_prompt = false;
    while (t.elapsed_time() < 5s) {
        if (esp.readable()) {
            char c;
            if (esp.read(&c, 1) == 1 && c == '>') { found_prompt = true; break; }
        } else {
            ThisThread::sleep_for(5ms);
        }
    }

    if (!found_prompt) {
        pc_print("[M5-FIRE] API POST failed — no prompt\r\n");
        return;
    }

    esp.write(body, body_len);

    // Wait for response (5s)
    char resp[256];
    memset(resp, 0, sizeof(resp));
    int total = 0;
    t.reset();
    while (t.elapsed_time() < 5s && total < (int)sizeof(resp) - 1) {
        if (esp.readable()) {
            int n = esp.read(resp + total, sizeof(resp) - 1 - total);
            if (n > 0) total += n;
            if (strstr(resp, "\r\nOK") || strstr(resp, "ERROR")) break;
        } else {
            ThisThread::sleep_for(10ms);
        }
    }

    if (strstr(resp, "OK")) {
        pc_print("[M5-FIRE] Fire alert POST sent OK\r\n");
    } else {
        pc_print("[M5-FIRE] Fire alert POST failed\r\n");
    }
}

void logFireEventToSD(float score, float pm_delta, float mq_delta, float temp_delta) {
    if (!sd_ready) {
        pc_print("[M5-FIRE] SD not ready — printing baseline to serial\r\n");

        // Print baseline header
        char hdr[128];
        snprintf(hdr, sizeof(hdr),
            "[M5-FIRE] === FIRE EVENT  score=%.3f  pm_d=%.2f mq_d=%.4f temp_d=%.2f ===\r\n",
            score, pm_delta, mq_delta, temp_delta);
        pc_print(hdr);

        // Print pre-incident baseline from ring buffer
        int base_count = 0;
        const FireBaselineEntry_t* base = ember_fire_get_baseline(&base_count);
        int slots = (base_count > 0) ? base_count : FIRE_BASELINE_SLOTS;
        for (int i = 0; i < slots; i++) {
            char line[160];
            snprintf(line, sizeof(line),
                "[M5-BASE] slot%d  PM2.5=%u PM10=%u MQ=%.4f T=%.1f H=%.1f P=%.1f TVOC=%u eCO2=%u Gas=%.0f MQd=%d\r\n",
                i, base[i].pm2_5, base[i].pm10, base[i].mq_analog,
                base[i].temperature, base[i].humidity, base[i].pressure,
                base[i].tvoc, base[i].eco2, base[i].gas_res, base[i].mq_digital);
            pc_print(line);
        }
        return;
    }

    // If SD is ready, write to file
    FILE* fp = fopen("/sd/fire_log.csv", "a");
    if (!fp) {
        pc_print("[M5-FIRE] Failed to open fire_log.csv\r\n");
        return;
    }

    // Write event header
    fprintf(fp, "FIRE_EVENT,score=%.3f,pm_delta=%.2f,mq_delta=%.4f,temp_delta=%.2f\n",
        score, pm_delta, mq_delta, temp_delta);

    // Write baseline
    int base_count = 0;
    const FireBaselineEntry_t* base = ember_fire_get_baseline(&base_count);
    int slots = (base_count > 0) ? base_count : FIRE_BASELINE_SLOTS;
    for (int i = 0; i < slots; i++) {
        fprintf(fp, "BASELINE,%d,%u,%u,%.4f,%.1f,%.1f,%.1f,%u,%u,%.0f,%d\n",
            i, base[i].pm2_5, base[i].pm10, base[i].mq_analog,
            base[i].temperature, base[i].humidity, base[i].pressure,
            base[i].tvoc, base[i].eco2, base[i].gas_res, base[i].mq_digital);
    }
    fclose(fp);
    pc_print("[M5-FIRE] Fire event + baseline written to SD\r\n");
}

// Update OLED with current sensor dashboard
void oled_update() {
    if (!oled_available) return;

    oled_clear();
    char line1[17], line2[17], line3[17];

    if (fire_alert_active) {
        // FIRE ALERT mode: distinct from AQI alarm
        oled_str_med_c(10, "!! FIRE !!");
        oled_str_med_c(30, "!! SMOKE !!");
        snprintf(line2, sizeof(line2), "PM2.5:%u MQ:%.2f",
            last_snapshot.pm2_5, last_snapshot.mq_analog * 3.3f);
        oled_str_med_c(50, line2);
    } else if (alarm_active) {
        // DANGER mode: 3 centered lines
        oled_str_med_c(16, "!! DANGER !!");
        snprintf(line2, sizeof(line2), "AQI:%d", last_aqi_score);
        oled_str_med_c(34, line2);
        snprintf(line3, sizeof(line3), "PM2.5:%u ug", last_snapshot.pm2_5);
        oled_str_med_c(52, line3);
    } else {
        // === Line 1 (y=2): AQI + category (always) ===
        const char* lvl;
        if      (last_aqi_score <= 50)  lvl = "GOOD";
        else if (last_aqi_score <= 100) lvl = "MOD";
        else if (last_aqi_score <= 150) lvl = "USG";
        else if (last_aqi_score <= 200) lvl = "BAD";
        else if (last_aqi_score <= 300) lvl = "V.BAD";
        else                            lvl = "HAZ";
        snprintf(line1, sizeof(line1), "AQI:%d %s", last_aqi_score, lvl);
        oled_str_med_c(16, line1);

        // === Lines 2-3: cycle with button (4 pages) ===
        switch (oled_page) {
            case 0: // PM2.5 + PM10
                snprintf(line2, sizeof(line2), "PM2.5:%u ug", last_snapshot.pm2_5);
                snprintf(line3, sizeof(line3), "PM10:%u ug", last_snapshot.pm10);
                break;
            case 1: // Temp + Humidity
                snprintf(line2, sizeof(line2), "Temp:%.1fC", last_snapshot.temperature);
                snprintf(line3, sizeof(line3), "Hum:%.0f%%", last_snapshot.humidity);
                break;
            case 2: // MQ Gas + TVOC
                { float mqv = last_snapshot.mq_analog * 3.3f;
                snprintf(line2, sizeof(line2), "MQ Gas:%.2fV", mqv); }
                snprintf(line3, sizeof(line3), "TVOC:%u ppb", last_snapshot.tvoc);
                break;
            case 3: // eCO2 + Pressure
                snprintf(line2, sizeof(line2), "eCO2:%u ppm", last_snapshot.eco2);
                snprintf(line3, sizeof(line3), "Pres:%.0fhPa", last_snapshot.pressure);
                break;
        }
        oled_str_med_c(34, line2);
        oled_str_med_c(52, line3);
    }

    oled_flush();
}

// ENS160 wrappers (use shared bus)
bool ens160_write_reg(char reg, char val) {
    return i2c_write_reg(ENS160_ADDR, reg, val);
}

bool ens160_read_reg(char reg, char* buf, int len) {
    return i2c_read_reg(ENS160_ADDR, reg, buf, len);
}

// BME680 wrappers (use shared bus)
bool bme680_write_reg(char reg, char val) {
    return i2c_write_reg(BME680_ADDR, reg, val);
}

bool bme680_read_reg(char reg, char* buf, int len) {
    return i2c_read_reg(BME680_ADDR, reg, buf, len);
}

// Initialize and read ENS160 sensor
void readENS160() {
    static bool initialized = false;

    if (!initialized) {
        pc_print("[ENS160] Starting init...\r\n");
        sensor_i2c.frequency(100000); // 100kHz I2C

        // Step 1: Reset - set Deep Sleep mode
        ens160_write_reg(ENS160_REG_OPMODE, ENS160_OPMODE_DEEP_SLEEP);
        ThisThread::sleep_for(30ms);

        // Step 2: Set Idle mode
        ens160_write_reg(ENS160_REG_OPMODE, ENS160_OPMODE_IDLE);
        ThisThread::sleep_for(30ms);

        // Step 3: Clear any command register flags
        ens160_write_reg(ENS160_REG_COMMAND, (char)0xCC); // CLRGPR - clear registers
        ThisThread::sleep_for(10ms);

        // Verify part ID (should be 0x0160)
        char id_buf[2];
        if (!ens160_read_reg(ENS160_REG_PART_ID, id_buf, 2)) {
            pc_print("[ENS160] I2C communication failed!\r\n");
            return;
        }
        uint16_t part_id = (id_buf[1] << 8) | (id_buf[0] & 0xFF);
        char msg[64];
        snprintf(msg, sizeof(msg), "[ENS160] Part ID: 0x%04X\r\n", part_id);
        pc_print(msg);

        // Step 4: Write ambient temp & humidity compensation
        // Temp = 25°C -> (25+273.15)*64 = 19084.6 -> 19085
        uint16_t temp_val = 19085;
        char temp_data[3] = {ENS160_REG_TEMP_IN, (char)(temp_val & 0xFF), (char)(temp_val >> 8)};
        sensor_i2c.write(ENS160_ADDR, temp_data, 3);

        // RH = 50% -> 50*512 = 25600
        uint16_t rh_val = 25600;
        char rh_data[3] = {ENS160_REG_RH_IN, (char)(rh_val & 0xFF), (char)(rh_val >> 8)};
        sensor_i2c.write(ENS160_ADDR, rh_data, 3);

        // Step 5: Set Standard operating mode
        if (!ens160_write_reg(ENS160_REG_OPMODE, ENS160_OPMODE_STD)) {
            pc_print("[ENS160] Failed to set operating mode!\r\n");
            return;
        }
        ThisThread::sleep_for(50ms); // Wait for mode change
        initialized = true;
        pc_print("[ENS160] Initialized in Standard mode\r\n");
    }

    // Check data status (don't skip if no new data - just read last values)
    char status;
    if (!ens160_read_reg(ENS160_REG_DATA_STATUS, &status, 1)) {
        pc_print("[ENS160] I2C read failed, skipping\r\n");
        return;
    }

    // Read AQI (1 byte: 1=Excellent, 2=Good, 3=Moderate, 4=Poor, 5=Unhealthy)
    char aqi_raw;
    if (!ens160_read_reg(ENS160_REG_DATA_AQI, &aqi_raw, 1)) return;
    int aqi = aqi_raw & 0x07;

    // Read TVOC (2 bytes, little-endian, in ppb)
    char tvoc_buf[2];
    if (!ens160_read_reg(ENS160_REG_DATA_TVOC, tvoc_buf, 2)) return;
    uint16_t tvoc = ((tvoc_buf[1] & 0xFF) << 8) | (tvoc_buf[0] & 0xFF);

    // Read eCO2 (2 bytes, little-endian, in ppm)
    char eco2_buf[2];
    if (!ens160_read_reg(ENS160_REG_DATA_ECO2, eco2_buf, 2)) return;
    uint16_t eco2 = ((eco2_buf[1] & 0xFF) << 8) | (eco2_buf[0] & 0xFF);

    // AQI level text
    const char* aqi_text[] = {"???", "Excellent", "Good", "Moderate", "Poor", "Unhealthy"};
    const char* level = (aqi >= 1 && aqi <= 5) ? aqi_text[aqi] : "Unknown";

    // Store in snapshot for SD logging (output consolidated in main loop)
    last_snapshot.aqi = aqi;
    last_snapshot.tvoc = tvoc;
    last_snapshot.eco2 = eco2;
}

// Read BME680 environmental sensor
void readBME680() {
    static bool bme_initialized = false;
    char msg[128];

    if (!bme_initialized) {
        // Check chip ID (should be 0x61)
        char chip_id;
        if (!bme680_read_reg(BME680_REG_CHIP_ID, &chip_id, 1)) {
            pc_print("[BME680] I2C communication failed!\r\n");
            return;
        }
        snprintf(msg, sizeof(msg), "[BME680] Chip ID: 0x%02X\r\n", (uint8_t)chip_id);
        pc_print(msg);

        if ((uint8_t)chip_id != 0x61) {
            pc_print("[BME680] Wrong chip ID! Expected 0x61\r\n");
            return;
        }

        // Soft reset
        bme680_write_reg(BME680_REG_RESET, (char)0xB6);
        ThisThread::sleep_for(10ms);

        // Read calibration data from registers
        char cal1[25]; // 0x89..0xA1 (25 bytes)
        char cal2[16]; // 0xE1..0xF0 (16 bytes)
        bme680_read_reg((char)0x89, cal1, 25);
        bme680_read_reg((char)0xE1, cal2, 16);

        // Additional calibration byte
        char cal3[1];
        bme680_read_reg((char)0x02, cal3, 1); // res_heat_range
        char cal4[1];
        bme680_read_reg((char)0x00, cal4, 1); // res_heat_val
        char cal5[1];
        bme680_read_reg((char)0x04, cal5, 1); // range_sw_err

        // Parse temperature calibration
        bme_cal.par_t1 = (uint16_t)((cal2[9] << 8) | (cal2[8] & 0xFF));
        bme_cal.par_t2 = (int16_t)((cal1[2] << 8) | (cal1[1] & 0xFF));
        bme_cal.par_t3 = (int8_t)cal1[3];

        // Parse pressure calibration
        bme_cal.par_p1 = (uint16_t)((cal1[6] << 8) | (cal1[5] & 0xFF));
        bme_cal.par_p2 = (int16_t)((cal1[8] << 8) | (cal1[7] & 0xFF));
        bme_cal.par_p3 = (int8_t)cal1[9];
        bme_cal.par_p4 = (int16_t)((cal1[12] << 8) | (cal1[11] & 0xFF));
        bme_cal.par_p5 = (int16_t)((cal1[14] << 8) | (cal1[13] & 0xFF));
        bme_cal.par_p6 = (int8_t)cal1[16];
        bme_cal.par_p7 = (int8_t)cal1[15];
        bme_cal.par_p8 = (int16_t)((cal1[20] << 8) | (cal1[19] & 0xFF));
        bme_cal.par_p9 = (int16_t)((cal1[22] << 8) | (cal1[21] & 0xFF));
        bme_cal.par_p10 = (uint8_t)cal1[23];

        // Parse humidity calibration
        bme_cal.par_h1 = (uint16_t)(((cal2[2] & 0xFF) << 4) | (cal2[1] & 0x0F));
        bme_cal.par_h2 = (uint16_t)(((cal2[0] & 0xFF) << 4) | ((cal2[1] >> 4) & 0x0F));
        bme_cal.par_h3 = (int8_t)cal2[3];
        bme_cal.par_h4 = (int8_t)cal2[4];
        bme_cal.par_h5 = (int8_t)cal2[5];
        bme_cal.par_h6 = (uint8_t)cal2[6];
        bme_cal.par_h7 = (int8_t)cal2[7];

        // Parse gas calibration
        bme_cal.par_g1 = (int8_t)cal2[12];
        bme_cal.par_g2 = (int16_t)((cal2[11] << 8) | (cal2[10] & 0xFF));
        bme_cal.par_g3 = (int8_t)cal2[13];
        bme_cal.res_heat_range = ((uint8_t)cal3[0] >> 4) & 0x03;
        bme_cal.res_heat_val = (int8_t)cal4[0];
        bme_cal.range_sw_err = ((int8_t)cal5[0] >> 4) & 0x0F;

        bme_initialized = true;
        pc_print("[BME680] Calibration loaded\r\n");
    }

    // Configure: humidity oversampling x1
    bme680_write_reg(BME680_REG_CTRL_HUM, (char)0x01);

    // Configure gas heater: 320°C target, 150ms heat time
    // Calculate heater resistance (simplified)
    bme680_write_reg(BME680_REG_RES_HEAT_0, (char)0x73); // ~320C
    bme680_write_reg(BME680_REG_GAS_WAIT_0, (char)0x59); // 100ms

    // Enable gas measurement, set heater profile 0
    bme680_write_reg(BME680_REG_CTRL_GAS_1, (char)0x10);

    // Configure: temp oversampling x2, pressure oversampling x16, forced mode
    bme680_write_reg(BME680_REG_CTRL_MEAS, (char)0x55); // osrs_t=010, osrs_p=101, mode=01(forced)

    // Wait for measurement to complete
    ThisThread::sleep_for(200ms);

    // Check if measurement is done
    char meas_status;
    bme680_read_reg(BME680_REG_MEAS_STATUS, &meas_status, 1);
    if (meas_status & 0x20) {
        // Measurement still running
        ThisThread::sleep_for(100ms);
    }

    // Read raw temperature (20-bit)
    char temp_raw[3];
    bme680_read_reg(BME680_REG_TEMP_MSB, temp_raw, 3);
    int32_t temp_adc = ((int32_t)(temp_raw[0] & 0xFF) << 12) | 
                       ((int32_t)(temp_raw[1] & 0xFF) << 4) | 
                       ((int32_t)(temp_raw[2] & 0xFF) >> 4);

    // Read raw pressure (20-bit)
    char press_raw[3];
    bme680_read_reg(BME680_REG_PRESS_MSB, press_raw, 3);
    int32_t press_adc = ((int32_t)(press_raw[0] & 0xFF) << 12) | 
                        ((int32_t)(press_raw[1] & 0xFF) << 4) | 
                        ((int32_t)(press_raw[2] & 0xFF) >> 4);

    // Read raw humidity (16-bit)
    char hum_raw[2];
    bme680_read_reg(BME680_REG_HUM_MSB, hum_raw, 2);
    int32_t hum_adc = ((int32_t)(hum_raw[0] & 0xFF) << 8) | (int32_t)(hum_raw[1] & 0xFF);

    // Read raw gas resistance (10-bit + range)
    char gas_raw[2];
    bme680_read_reg(BME680_REG_GAS_MSB, gas_raw, 2);
    uint16_t gas_adc = ((uint16_t)(gas_raw[0] & 0xFF) << 2) | ((gas_raw[1] & 0xC0) >> 6);
    uint8_t gas_range = gas_raw[1] & 0x0F;
    bool gas_valid = (gas_raw[1] & 0x20) != 0;

    // --- Compensate Temperature (from BME680 datasheet) ---
    int64_t var1_t = ((int64_t)temp_adc >> 3) - ((int64_t)bme_cal.par_t1 << 1);
    int64_t var2_t = (var1_t * (int64_t)bme_cal.par_t2) >> 11;
    int64_t var3_t = ((((var1_t >> 1) * (var1_t >> 1)) >> 12) * ((int64_t)bme_cal.par_t3 << 4)) >> 14;
    int32_t t_fine = (int32_t)(var2_t + var3_t);
    float temperature = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    // --- Compensate Pressure ---
    int32_t var1_p = (t_fine >> 1) - 64000;
    int32_t var2_p = ((((var1_p >> 2) * (var1_p >> 2)) >> 11) * (int32_t)bme_cal.par_p6) >> 2;
    var2_p = var2_p + ((var1_p * (int32_t)bme_cal.par_p5) << 1);
    var2_p = (var2_p >> 2) + ((int32_t)bme_cal.par_p4 << 16);
    var1_p = (((((var1_p >> 2) * (var1_p >> 2)) >> 13) * ((int32_t)bme_cal.par_p3 << 5)) >> 3) +
             (((int32_t)bme_cal.par_p2 * var1_p) >> 1);
    var1_p = var1_p >> 18;
    var1_p = ((32768 + var1_p) * (int32_t)bme_cal.par_p1) >> 15;
    int32_t pressure = 1048576 - press_adc;
    pressure = (int32_t)((pressure - (var2_p >> 12)) * ((int32_t)3125));
    if (pressure >= (int32_t)0x40000000)
        pressure = ((pressure / var1_p) << 1);
    else
        pressure = ((pressure << 1) / var1_p);
    var1_p = ((int32_t)bme_cal.par_p9 * (int32_t)(((pressure >> 3) * (pressure >> 3)) >> 13)) >> 12;
    var2_p = ((int32_t)(pressure >> 2) * (int32_t)bme_cal.par_p8) >> 13;
    int32_t var3_p = ((int32_t)(pressure >> 8) * (int32_t)(pressure >> 8) * (int32_t)(pressure >> 8) * (int32_t)bme_cal.par_p10) >> 17;
    pressure = (int32_t)(pressure) + ((var1_p + var2_p + var3_p + ((int32_t)bme_cal.par_p7 << 7)) >> 4);
    float press_hpa = (float)pressure / 100.0f;

    // --- Compensate Humidity ---
    int32_t temp_scaled = (int32_t)((t_fine * 5 + 128) >> 8);
    int32_t var1_h = (int32_t)(hum_adc - ((int32_t)((int32_t)bme_cal.par_h1 << 4))) -
                     (((temp_scaled * (int32_t)bme_cal.par_h3) / ((int32_t)100)) >> 1);
    int32_t var2_h = ((int32_t)bme_cal.par_h2 *
                      (((temp_scaled * (int32_t)bme_cal.par_h4) / ((int32_t)100)) +
                       (((temp_scaled * ((temp_scaled * (int32_t)bme_cal.par_h5) / ((int32_t)100))) >> 6) / ((int32_t)100)) +
                       (int32_t)(1 << 14))) >> 10;
    int32_t var3_h = var1_h * var2_h;
    int32_t var4_h = ((int32_t)bme_cal.par_h6 << 7) +
                     ((temp_scaled * (int32_t)bme_cal.par_h7) / ((int32_t)100));
    var4_h = (var4_h >> 4) + (((var3_h >> 14) * (var3_h >> 14)) >> 10);
    int32_t hum_comp = (var3_h + var4_h) >> 12;
    // Clamp 0-100%
    if (hum_comp > 100000) hum_comp = 100000;
    if (hum_comp < 0) hum_comp = 0;
    float humidity = (float)hum_comp / 1000.0f;

    // --- Gas Resistance (simplified lookup) ---
    static const uint32_t gas_range_lut[16] = {
        2147483647u, 2147483647u, 2147483647u, 2147483647u,
        2147483647u, 2126008810u, 2147483647u, 2130303777u,
        2147483647u, 2147483647u, 2143188679u, 2136746228u,
        2147483647u, 2126008810u, 2147483647u, 2147483647u
    };
    int64_t var1_g = (int64_t)(1340 + (5 * (int64_t)bme_cal.range_sw_err)) *
                     ((int64_t)gas_range_lut[gas_range]) >> 16;
    int64_t var2_g = ((int64_t)gas_adc << 15) - (int64_t)(1 << 24) + var1_g;
    float gas_res = 0.0f;
    if (var2_g > 0 && gas_valid) {
        gas_res = (float)(((int64_t)(10000 * (int64_t)var1_g) / var2_g) * (int64_t)1000);
    }

    // Store in snapshot for SD logging (output consolidated in main loop)
    last_snapshot.temperature = temperature;
    last_snapshot.humidity = humidity;
    last_snapshot.pressure = press_hpa;
    last_snapshot.gas_res = gas_res;
}

// Read MQ gas sensor analog and digital values
void readMQSensor() {
    float analog_val = mq_analog.read();
    float voltage = analog_val * 3.3f;
    int digital_val = mq_digital.read();
    // Output consolidated in main loop
}

// Sync time from ESP32 via AT+CIPSNTPCFG and AT+CIPSNTPTIME
void syncTimeFromESP32() {
    char resp[512];
    pc_print("\r\n--- Syncing time via NTP ---\r\n");

    // Enable SNTP, timezone +0 (UTC), use pool.ntp.org
    esp_send_cmd("AT+CIPSNTPCFG=1,0,\"pool.ntp.org\"", resp, sizeof(resp), 5000);
    ThisThread::sleep_for(2s);

    // Get SNTP time
    esp_send_cmd("AT+CIPSNTPTIME?", resp, sizeof(resp), 5000);
    pc_print("NTP Response: ");
    pc_print(resp);
    pc_print("\r\n");

    // Parse response: +CIPSNTPTIME:Thu Feb 20 15:30:45 2026
    char* time_str = strstr(resp, "+CIPSNTPTIME:");
    if (time_str) {
        time_str += 13; // Skip "+CIPSNTPTIME:"
        // Parse: Day Mon DD HH:MM:SS YYYY
        char day_name[4], mon_name[4];
        int day, hour, min, sec, year;
        if (sscanf(time_str, "%3s %3s %d %d:%d:%d %d",
                   day_name, mon_name, &day, &hour, &min, &sec, &year) == 7) {
            // Convert month name to number
            const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
            int month = 0;
            for (int i = 0; i < 12; i++) {
                if (strncmp(mon_name, months[i], 3) == 0) {
                    month = i;
                    break;
                }
            }
            struct tm t_tm = {};
            t_tm.tm_year = year - 1900;
            t_tm.tm_mon = month;
            t_tm.tm_mday = day;
            t_tm.tm_hour = hour;
            t_tm.tm_min = min;
            t_tm.tm_sec = sec;
            time_t epoch = mktime(&t_tm);
            set_time(epoch);

            char msg[64];
            snprintf(msg, sizeof(msg), "[TIME] Set to %04d-%02d-%02d %02d:%02d:%02d\r\n",
                     year, month + 1, day, hour, min, sec);
            pc_print(msg);
        } else {
            pc_print("[TIME] Failed to parse NTP time\r\n");
        }
    } else {
        pc_print("[TIME] No NTP response\r\n");
    }
}

// Initialize SD card (non-blocking - sensors keep running if SD fails)
void initSD() {
    pc_print("\r\n--- Initializing SD Card ---\r\n");
    
    int err = sd.init();
    if (err != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[SD] Init failed: %d (no card?)\r\n", err);
        pc_print(msg);
        pc_print("[SD] Continuing without SD card\r\n");
        return;
    }

    char msg[128];
    uint64_t sd_size = sd.size();
    snprintf(msg, sizeof(msg), "[SD] Card size: %llu MB\r\n", sd_size / (1024 * 1024));
    pc_print(msg);

    err = fs.mount(&sd);
    if (err != 0) {
        pc_print("[SD] Mount failed - card not FAT32?\r\n");
        pc_print("[SD] Format card as FAT32 on PC/Mac first!\r\n");
        pc_print("[SD] Trying on-device format (may take a while)...\r\n");
        err = FATFileSystem::format(&sd, 32768);  // 32KB cluster for large cards
        if (err != 0) {
            snprintf(msg, sizeof(msg), "[SD] Format failed: %d\r\n", err);
            pc_print(msg);
            sd.deinit();
            return;
        }
        pc_print("[SD] Formatted OK\r\n");
        err = fs.mount(&sd);
        if (err != 0) {
            snprintf(msg, sizeof(msg), "[SD] Mount after format failed: %d\r\n", err);
            pc_print(msg);
            sd.deinit();
            return;
        }
    }

    // Create data directory
    mkdir("/sd/data", 0777);

    sd_ready = true;
    pc_print("[SD] Ready!\r\n");
}

// Get total size of files in /sd/data/ directory
uint64_t getDataFolderSize() {
    uint64_t total = 0;
    DIR* dir = opendir("/sd/data");
    if (!dir) return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char path[64];
            snprintf(path, sizeof(path), "/sd/data/%s", entry->d_name);
            struct stat st;
            if (stat(path, &st) == 0) {
                total += st.st_size;
            }
        }
    }
    closedir(dir);
    return total;
}

// Find and delete the oldest file in /sd/data/
void deleteOldestFile() {
    DIR* dir = opendir("/sd/data");
    if (!dir) return;

    char oldest_name[64] = "";
    char oldest_path[80] = "";

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            // Files are named YYYY-MM-DD.csv, so alphabetical = chronological
            if (oldest_name[0] == '\0' || strcmp(entry->d_name, oldest_name) < 0) {
                strncpy(oldest_name, entry->d_name, sizeof(oldest_name) - 1);
            }
        }
    }
    closedir(dir);

    if (oldest_name[0] != '\0') {
        snprintf(oldest_path, sizeof(oldest_path), "/sd/data/%s", oldest_name);
        remove(oldest_path);
        char msg[128];
        snprintf(msg, sizeof(msg), "[SD] Deleted old file: %s\r\n", oldest_name);
        pc_print(msg);
    }
}

// Save sensor data to SD card as CSV
void saveToSD() {
    if (!sd_ready) return;

    // Get current time
    time_t now = time(NULL);
    struct tm* t = localtime(&now);

    // Build filename: /sd/data/YYYY-MM-DD.csv
    char filename[64];
    snprintf(filename, sizeof(filename), "/sd/data/%04d-%02d-%02d.csv",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

    // Check if file exists (to write header)
    struct stat st;
    bool new_file = (stat(filename, &st) != 0);

    FILE* fp = fopen(filename, "a");
    if (!fp) {
        pc_print("[SD] Failed to open file\r\n");
        return;
    }

    // Write CSV header if new file
    if (new_file) {
        fprintf(fp, "timestamp,PM1.0,PM2.5,PM10,TVOC,eCO2,temperature,humidity,pressure,gas,MQ_analog,MQ_digital\n");
    }

    // Write data row
    char timestamp[24];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    fprintf(fp, "%s,%u,%u,%u,%u,%u,%.1f,%.1f,%.1f,%.0f,%.3f,%d\n",
            timestamp,
            last_snapshot.pm1_0, last_snapshot.pm2_5, last_snapshot.pm10,
            last_snapshot.tvoc, last_snapshot.eco2,
            last_snapshot.temperature, last_snapshot.humidity,
            last_snapshot.pressure, last_snapshot.gas_res,
            last_snapshot.mq_analog, last_snapshot.mq_digital);

    fclose(fp);

    // Check data folder size and enforce 30GB limit
    uint64_t folder_size = getDataFolderSize();
    while (folder_size > MAX_DATA_SIZE) {
        deleteOldestFile();
        folder_size = getDataFolderSize();
    }

    pc_print("[SD] Data saved\r\n");
}

int main() {
    // All LEDs off
    led_red = 1; led_green = 1; led_blue = 1;

    char resp[1024];

    pc_print("\r\n=== Ember Air Quality Monitor ===\r\n");

    // --- I2C Bus Scan ---
    pc_print("\r\n--- I2C Bus Scan ---\r\n");
    sensor_i2c.frequency(100000);
    int found = 0;
    for (int addr = 0x08; addr < 0x78; addr++) {
        int addr8 = addr << 1;
        if (sensor_i2c.write(addr8, NULL, 0) == 0) {
            char msg[48];
            snprintf(msg, sizeof(msg), "  Found device at 0x%02X\r\n", addr);
            pc_print(msg);
            found++;
        }
    }
    char fmsg[48];
    snprintf(fmsg, sizeof(fmsg), "  Total: %d device(s) found\r\n", found);
    pc_print(fmsg);
    pc_print("--- End I2C Scan ---\r\n");

    // --- I2C Device Init (before WiFi so OLED can show setup screen) ---
    pc_print("\r\n--- I2C Device Check ---\r\n");
    ThisThread::sleep_for(200ms);

    char probe[1] = {0};
    ens160_available = (sensor_i2c.write(ENS160_ADDR, probe, 1) == 0);
    if (ens160_available) pc_print("  ENS160 (0x52): OK\r\n");
    else { sensor_i2c.stop(); pc_print("  ENS160 (0x52): NOT FOUND\r\n"); }

    bme680_available = (sensor_i2c.write(BME680_ADDR, probe, 1) == 0);
    if (bme680_available) pc_print("  BME680 (0x77): OK\r\n");
    else { sensor_i2c.stop(); pc_print("  BME680 (0x77): NOT FOUND\r\n"); }

    oled_available = (sensor_i2c.write(SH1106_ADDR, probe, 1) == 0);
    if (oled_available) pc_print("  OLED SH1106 (0x3C): OK\r\n");
    else { sensor_i2c.stop(); pc_print("  OLED SH1106 (0x3C): NOT FOUND\r\n"); }
    if (oled_available) {
        oled_init();
        // PullUp already set in DigitalIn constructor
        oled_clear();
        oled_str_med_c(16, "EMBER AIR");
        oled_str_med_c(38, "Booting...");
        oled_flush();
    }

    pc_print("Waiting for ESP32 to boot...\r\n");
    ThisThread::sleep_for(5s);

    // Flush any garbage from ESP32 boot
    esp_send_cmd("AT", resp, sizeof(resp), 2000);
    ThisThread::sleep_for(1s);

    // Test basic AT communication
    pc_print("\r\n--- Testing AT command ---\r\n");
    esp_send_cmd("AT", resp, sizeof(resp), 2000);
    pc_print("Response: ");
    pc_print(resp);
    pc_print("\r\n");

    if (resp_ok(resp)) {
        led_green = 0;
        pc_print(">> ESP32 is responding! <<\r\n");
    } else {
        led_red = 0;
        pc_print(">> ESP32 not responding. Check wiring! <<\r\n");
    }
    ThisThread::sleep_for(500ms);

    // Get firmware version
    pc_print("\r\n--- Firmware Version ---\r\n");
    esp_send_cmd("AT+GMR", resp, sizeof(resp), 2000);
    pc_print(resp);
    pc_print("\r\n");
    ThisThread::sleep_for(500ms);

    // Skip SD card - consistently fails (-5005) and hangs on init
    // WiFi credentials now stored in ESP32 flash via AT+CWAUTOCONN=1
    // Alert log will work in serial-only mode (no CSV on SD)
    pc_print("[SD] Skipped (using ESP32 flash for WiFi creds)\r\n");
    // initSD();

    // ===== WiFi Connection with SoftAP Fallback =====
    bool wifi_ok = false;

    // Step 0: Enable ESP32 auto-connect (saves last WiFi in ESP32 flash)
    esp_send_cmd("AT+CWAUTOCONN=1", resp, sizeof(resp), 1000);
    ThisThread::sleep_for(200ms);

    // Auto-connect enabled - ESP32 will reconnect to last saved WiFi

    // Step 1: Check if ESP32 already auto-connected from its flash
    pc_print("\r\n--- Checking ESP32 auto-connect ---\r\n");
    esp_send_cmd("AT+CWJAP?", resp, sizeof(resp), 3000);
    pc_print(resp);
    if (strstr(resp, "+CWJAP:")) {
        // Already connected! Extract SSID
        char* q1 = strchr(resp, '"');
        if (q1) {
            q1++;
            char* q2 = strchr(q1, '"');
            if (q2 && (q2 - q1) < 63) {
                strncpy(connected_ssid, q1, q2 - q1);
                connected_ssid[q2 - q1] = '\0';
            }
        }
        wifi_connected = true;
        wifi_ok = true;
        led_blue = 0;
        char amsg[128];
        snprintf(amsg, sizeof(amsg), ">> Auto-connected to '%s'! <<\r\n", connected_ssid);
        pc_print(amsg);
    }

    // Step 2: If not auto-connected, try SD card saved creds
    if (!wifi_ok && loadWiFiCreds()) {
        pc_print("\r\n--- Connecting with saved credentials ---\r\n");
        wifi_ok = connectToWiFi(saved_wifi.ssid, saved_wifi.password);
        if (wifi_ok) {
            led_blue = 0;
            strncpy(connected_ssid, saved_wifi.ssid, 63);
            pc_print(">> Connected with saved credentials! <<\r\n");
        } else {
            pc_print(">> Saved credentials failed <<\r\n");
        }
    }
    if (!wifi_ok) {
        // Step 3: If still not connected → SoftAP Web Portal
        pc_print("\r\n--- No WiFi connection - Starting SoftAP Portal ---\r\n");
        startSoftAP();

        // Update OLED if available (show setup instructions)
        if (oled_available) {
            oled_clear();
            oled_str_med_c(16, "Connect WiFi:");
            oled_str_med_c(34, "Ember-Setup");
            char pin_line[18];
            snprintf(pin_line, sizeof(pin_line), "PIN: %s", setup_pin);
            oled_str_med_c(52, pin_line);
            oled_flush();
        }

        // Stay in SoftAP mode until user connects to WiFi
        pc_print("[SoftAP] Waiting for WiFi provisioning...\r\n");
        pc_print("[SoftAP] ** On iPhone: disable cellular data first! **\r\n");

        // Flush any stale UART data before polling
        { char junk[256]; while (esp.readable()) esp.read(junk, sizeof(junk)); }

        while (!wifi_connected) {
            if (handleSoftAPClient()) {
                // WiFi connected via web portal!
                pc_print("\r\n>> WiFi provisioned via SoftAP! <<\r\n");
                led_blue = 0;
                break;
            }
            led_green = !led_green;  // Blink green while in setup mode
            ThisThread::sleep_for(10ms);   // Poll fast — was 100ms, too slow
        }

        // Shut down the SoftAP portal
        stopSoftAP();
        ThisThread::sleep_for(1000ms);
    }

    // Ping Google to verify internet
    pc_print("\r\n--- Pinging google.com ---\r\n");
    esp_send_cmd("AT+PING=\"google.com\"", resp, sizeof(resp), 5000);
    pc_print(resp);
    pc_print("\r\n");

    if (resp_ok(resp)) {
        led_red = 0; led_green = 0; led_blue = 0;
        pc_print(">> Internet is working! <<\r\n");
    }

    // Sync time via NTP
    syncTimeFromESP32();

    // Initialize on-board RL inference engine
    ember_inference_reset();
    pc_print("[RL] On-board inference engine initialized\r\n");

    // M4: init alert log on SD card (works without SD too — just prints to serial)
    ember_alert_log_init(sd_ready, pc_print);

    // M5: init fire/smoke detection
    ember_fire_init(pc_print);

    // M5(Rajini): load sensor calibration from SD (if exists) and apply to RL
    ember_calibration_load(sd_ready, pc_print);
    ember_inference_set_calib(ember_calibration_get());

    // Main loop: read sensors + blink LED
    pc_print("\r\n--- Starting Sensor Loop ---\r\n");
    pc_print("Reading PMS5003 + MQ + ENS160 + BME680 sensors every 2 seconds...\r\n");
    char msg[128];

    while (true) {
        // --- Poll buttons for edge detection ---
        poll_button();
        poll_arm_button();

        // --- OLED auto-cycle: switch pages every 3 seconds ---
        if (!oled_auto_started) {
            oled_auto_timer.start();
            oled_auto_started = true;
        }
        if (oled_auto_timer.elapsed_time() >= 3s) {
            oled_page = (oled_page + 1) % OLED_PAGES;
            oled_auto_timer.reset();
        }

        // --- Test alarm button (PTC2): single press = 3s test alarm ---
        if (btn_pressed) {
            btn_pressed = false;
            if (!test_alarm_active) {
                test_alarm_requested = true;
                pc_print("[BTN] Test alarm triggered (3 seconds)\r\n");
            }
        }

        // --- Arm/Disarm button (PTA1): toggle arm state ---
        if (arm_btn_pressed) {
            arm_btn_pressed = false;
            alarm_armed = !alarm_armed;
            if (!alarm_armed) {
                // DISARMED: turn off alarm if active
                if (alarm_active) {
                    alarm = 0;
                    alarm_active = false;
                }
                if (fire_alert_active) {
                    fire_alert_active = false;
                    fire_alert_sent = false;
                    ember_fire_clear_alert();
                    pc_print("[FIRE] Fire alert cleared (disarmed)\r\n");
                }
                alarm_cooldown_active = true;
                alarm_cooldown_timer.reset();
                alarm_cooldown_timer.start();
                if (oled_available) oled_cmd(0xA6);
                pc_print("[ARM-BTN] Alarm DISARMED\r\n");
            } else {
                // ARMED: re-arm and check if alarm should trigger
                alarm_cooldown_active = false;
                alarm_cooldown_timer.stop();
                if (last_aqi_score >= 151 && !alarm_active) {
                    alarm = 1;
                    alarm_active = true;
                    pc_print("[ARM-BTN] Alarm ARMED (AQI high — alarm engaged)\r\n");
                } else {
                    pc_print("[ARM-BTN] Alarm ARMED\r\n");
                }
            }
            // DIGITAL TWIN: sync to server
            sendDeviceStatusToAPI(alarm_armed, alarm_active, alarm_armed ? "button_armed" : "button_disarmed");

            // Show arm/disarm status on OLED for 2 seconds
            if (oled_available) {
                oled_clear();
                oled_str_med_c(10, "================");
                if (alarm_armed) {
                    oled_str_med_c(30, "  ALARM ARMED");
                } else {
                    oled_str_med_c(30, "ALARM DISARMED");
                }
                oled_str_med_c(50, "================");
                oled_flush();
                oled_arm_overlay = true;
                oled_arm_overlay_timer.reset();
                oled_arm_overlay_timer.start();
            }
        }

        // Show arm/disarm overlay if requested by pollRemoteConfig (web panel)
        if (oled_arm_overlay_requested) {
            oled_arm_overlay_requested = false;
            if (oled_available) {
                oled_clear();
                oled_str_med_c(10, "================");
                if (alarm_armed) {
                    oled_str_med_c(30, "  ALARM ARMED");
                } else {
                    oled_str_med_c(30, "ALARM DISARMED");
                }
                oled_str_med_c(50, "================");
                oled_flush();
                oled_arm_overlay = true;
                oled_arm_overlay_timer.reset();
                oled_arm_overlay_timer.start();
            }
        }

        // Clear arm/disarm OLED overlay after 2 seconds
        if (oled_arm_overlay && oled_arm_overlay_timer.elapsed_time() >= 2s) {
            oled_arm_overlay = false;
            oled_arm_overlay_timer.stop();
            oled_update();
        }

        // ========== FAST PATH: Test alarm — tight loop, skip heavy work ==========
        if (test_alarm_requested && !test_alarm_active) {
            test_alarm_requested = false;
            test_alarm_active = true;
            test_alarm_timer.reset();
            test_alarm_timer.start();
            alarm = 1;
            pc_print("[M4-TEST] Test alarm ON (3 seconds)\r\n");
        }
        if (test_alarm_active) {
            if (test_alarm_timer.elapsed_time() >= std::chrono::milliseconds(TEST_ALARM_DURATION_MS)) {
                test_alarm_active = false;
                test_alarm_timer.stop();
                alarm = 0;
                pc_print("[M4-TEST] Test alarm OFF (auto-cleared)\r\n");
            } else {
                alarm = 1;
            }
            // Tight 200ms loop with button polling — NO sensor/network work
            for (int sl = 0; sl < 10; sl++) {
                poll_button();
                poll_arm_button();
                if (btn_pressed || arm_btn_pressed) break;
                ThisThread::sleep_for(20ms);
            }
            led_blue = !led_blue;
            continue;  // Skip sensor reads, AI server, etc.
        }

        PMS5003Data pm = readPMS5003();
        if (pm.valid) {
            last_snapshot.pm1_0 = pm.pm1_0;
            last_snapshot.pm2_5 = pm.pm2_5;
            last_snapshot.pm10 = pm.pm10;
            last_snapshot.pms_valid = true;
        }

        readMQSensor();
        last_snapshot.mq_analog = mq_analog.read();
        last_snapshot.mq_digital = mq_digital.read();

        // Poll buttons between sensor reads for responsiveness
        poll_button();
        poll_arm_button();

        if (bme680_available) {
            readBME680();
        }

        // Poll buttons after BME680 (300ms blocking)
        poll_button();
        poll_arm_button();

        if (ens160_available) {
            readENS160();
        }

        // --- Consolidated sensor output (1 clean line) ---
        snprintf(msg, sizeof(msg),
            "\r\n[SENS] PM2.5=%u PM10=%u | T=%.1fC H=%.1f%% P=%.0f | TVOC=%u CO2=%u | MQ=%.3f %s\r\n",
            (unsigned)last_snapshot.pm2_5, (unsigned)last_snapshot.pm10,
            last_snapshot.temperature, last_snapshot.humidity, last_snapshot.pressure,
            (unsigned)last_snapshot.tvoc, (unsigned)last_snapshot.eco2,
            last_snapshot.mq_analog, last_snapshot.mq_digital ? "OK" : "ALERT");
        pc_print(msg);

        // ========== M4: Poll remote config from web control panel (every 3rd cycle) ==========
        static int config_cycle = 0;
        config_cycle++;
        if (wifi_connected && !softap_mode && config_cycle >= 3) {
            config_cycle = 0;
            pollRemoteConfig();
        }

        // Poll buttons after network call
        poll_button();
        poll_arm_button();

        // ========== DIGITAL TWIN: Cooldown re-arm (Physical → Virtual) ==========
        if (alarm_cooldown_active) {
            if (alarm_cooldown_timer.elapsed_time() >= std::chrono::seconds(ALARM_COOLDOWN_SEC)) {
                alarm_cooldown_active = false;
                alarm_cooldown_timer.stop();
                alarm_armed = true;  // DIGITAL TWIN: re-arm after cooldown
                pc_print("[ALARM] Cooldown expired, alarm re-armed\r\n");
                // DIGITAL TWIN: notify server so web panel updates toggle back to ARMED
                sendDeviceStatusToAPI(true, false, "cooldown_rearmed");
            }
        }

        // (Cable detection removed — PTA1 is now arm/disarm button)

        // ========== ALARM DECISION ==========
        // 1) On-board RL runs EVERY cycle (~1ms) for instant alarm response
        // 2) AI server called every 5th cycle (~10s) — updates AQI display
        // M4: Skip alarm actuation if not armed or test alarm is running
        static int ai_cycle = 0;

        // M4 alert log: capture Q-values and mode for logging
        float  m4_q_off = 0.0f, m4_q_on = 0.0f, m4_aqi = 0.0f;
        const char* m4_mode = "LOCAL";

        EmberInferenceResult_t rl = ember_inference_run(
            (uint16_t)last_snapshot.pm1_0,
            (uint16_t)last_snapshot.pm2_5,
            (uint16_t)last_snapshot.pm10,
            last_snapshot.temperature,
            last_snapshot.humidity,
            last_snapshot.pressure,
            last_snapshot.gas_res,
            last_snapshot.mq_analog,
            last_snapshot.mq_digital,
            (uint16_t)last_snapshot.tvoc,
            (uint16_t)last_snapshot.eco2);

        // RL controls alarm immediately — M4: only if armed and no test running
        if (!alarm_cooldown_active && alarm_armed && !test_alarm_active) {
            if (rl.alarm_on && !alarm_active) {
                alarm = 1;
                alarm_active = true;
            } else if (!rl.alarm_on && alarm_active) {
                alarm = 0;
                alarm_active = false;
            }
        } else if (!alarm_armed && alarm_active && !test_alarm_active) {
            // Disarmed — force alarm off
            alarm = 0;
            alarm_active = false;
        }
        last_snapshot.aqi = rl.aqi;
        last_aqi_score = (int)rl.aqi;
        m4_q_off = rl.q_off;
        m4_q_on  = rl.q_on;
        m4_aqi   = rl.aqi;

        // AI server every 5th cycle — but SKIP when alarm is active (buzzer priority)
        ai_cycle++;
        if (wifi_connected && !softap_mode && ai_cycle >= 5 && !alarm_active) {
            ai_cycle = 0;
            AIServerResult ai = sendToAIServer(
                last_snapshot.pm2_5 > 0 ? last_snapshot.pm1_0 : 0,
                last_snapshot.pm2_5,
                last_snapshot.pm10,
                last_snapshot.tvoc,
                last_snapshot.eco2,
                last_snapshot.temperature,
                last_snapshot.humidity,
                last_snapshot.pressure,
                last_snapshot.gas_res,
                last_snapshot.mq_analog,
                last_snapshot.mq_digital);

            if (ai.valid) {
                // AI overrides AQI display + alarm if different from RL
                last_snapshot.aqi = ai.aqi;
                last_aqi_score = (int)ai.aqi;
                if (!alarm_cooldown_active && alarm_armed && !test_alarm_active) {
                    if (ai.alarm_on && !alarm_active) {
                        alarm = 1;
                        alarm_active = true;
                    } else if (!ai.alarm_on && alarm_active) {
                        alarm = 0;
                        alarm_active = false;
                    }
                }
                // M4: AI server overrides mode + AQI for alert log
                m4_mode = "CLOUD";
                m4_aqi  = ai.aqi;
                char ai_line[96];
                snprintf(ai_line, sizeof(ai_line),
                    "[AI] AQI=%.0f %s -> %s\r\n",
                    ai.aqi, ai.category, ai.alarm_on ? "ALARM" : "OK");
                pc_print(ai_line);
            } else {
                char rl_line[96];
                snprintf(rl_line, sizeof(rl_line),
                    "[RL] AQI=%.0f -> %s\r\n",
                    rl.aqi, rl.alarm_on ? "ALARM" : "OK");
                pc_print(rl_line);
            }
        } else {
            char rl_line[96];
            snprintf(rl_line, sizeof(rl_line),
                "[RL] AQI=%.0f -> %s\r\n",
                rl.aqi, rl.alarm_on ? "ALARM" : "OK");
            pc_print(rl_line);
        }

        // ========== M5: Fire & Smoke Detection ==========
        {
            FireResult_t fire = ember_fire_check(
                last_snapshot.pm2_5, last_snapshot.pm10,
                last_snapshot.mq_analog, last_snapshot.temperature,
                last_snapshot.humidity, last_snapshot.pressure,
                last_snapshot.tvoc, last_snapshot.eco2,
                last_snapshot.gas_res, last_snapshot.mq_digital);

            if (fire.alert && !fire_alert_active) {
                // New FIRE ALERT triggered!
                fire_alert_active = true;
                fire_alert_sent = false;
                alarm = 1;
                alarm_active = true;
                pc_print("\r\n[FIRE] *** ENTERING FIRE ALERT MODE ***\r\n");

                // Immediate HTTP POST to API
                sendFireAlertToAPI(fire.score, fire.pm_delta, fire.mq_delta, fire.temp_delta);
                fire_alert_sent = true;

                // Log pre-incident baseline to SD
                logFireEventToSD(fire.score, fire.pm_delta, fire.mq_delta, fire.temp_delta);
            }

            // Auto-clear fire alert when module says it's over
            if (fire_alert_active && !fire.in_alert) {
                fire_alert_active = false;
                fire_alert_sent = false;
                pc_print("[FIRE] Fire alert auto-cleared (conditions normalized)\r\n");
            }
        }

        // M4: Log alarm event (only triggers on OFF->ON transition)
        {
            AlertSnapshot_t snap;
            snap.pm1_0       = last_snapshot.pm1_0;
            snap.pm2_5       = last_snapshot.pm2_5;
            snap.pm10        = last_snapshot.pm10;
            snap.temperature = last_snapshot.temperature;
            snap.humidity    = last_snapshot.humidity;
            snap.pressure    = last_snapshot.pressure;
            snap.gas_res     = last_snapshot.gas_res;
            snap.mq_analog   = last_snapshot.mq_analog;
            snap.mq_digital  = last_snapshot.mq_digital;
            snap.tvoc        = last_snapshot.tvoc;
            snap.eco2        = last_snapshot.eco2;
            snap.pms_valid   = last_snapshot.pms_valid;
            ember_alert_log_check(alarm, m4_q_off, m4_q_on, m4_aqi, m4_mode, &snap);
        }

        // M4: Check for REPLAY / STATS commands typed in serial terminal
        ember_alert_check_serial_cmd();

        // M5(Rajini): Sensor calibration — detects SW2 3-second hold, runs 60s baseline
        // Also re-applies calibration to RL inference after each completion
        ember_calibration_check(
            last_snapshot.pm1_0, last_snapshot.pm2_5, last_snapshot.pm10,
            last_snapshot.temperature, last_snapshot.humidity,
            last_snapshot.pressure, last_snapshot.gas_res,
            last_snapshot.mq_analog, last_snapshot.mq_digital,
            last_snapshot.tvoc, last_snapshot.eco2,
            sd_ready, led_red, led_green, led_blue, pc_print);
        ember_inference_set_calib(ember_calibration_get());

        // Don't overwrite OLED if arm/disarm overlay is showing
        if (!oled_arm_overlay) {
            oled_update();
        }
        saveToSD();

        led_blue = !led_blue;

        if (test_alarm_active) {
            // M4: Test alarm — continuous buzz, short loop
            alarm = 1;
            ThisThread::sleep_for(200ms);
        } else if (fire_alert_active) {
            // M5: FIRE ALERT — rapid triple-beep pattern (distinct from AQI alarm)
            // Pattern: beep-beep-beep-pause (3x 150ms on + 100ms off + 500ms pause)
            for (int burst = 0; burst < 3; burst++) {
                alarm = 1;
                if (oled_available) oled_cmd(0xA7); // OLED inverted
                ThisThread::sleep_for(150ms);
                alarm = 0;
                if (oled_available) oled_cmd(0xA6);
                ThisThread::sleep_for(100ms);
            }
            ThisThread::sleep_for(500ms); // pause between triple-beeps
        } else if (alarm_active && alarm_armed) {
            // Buzzer burst: 0.5s beep + 0.5s silent, synced with OLED flash
            // 4 x 500ms = 2s total (matches normal loop timing)
            for (int i = 0; i < 4; i++) {
                if (i % 2 == 0) {
                    alarm = 1;  // Buzzer ON
                    if (oled_available) oled_cmd(0xA7); // OLED inverted
                } else {
                    alarm = 0;  // Buzzer OFF
                    if (oled_available) oled_cmd(0xA6); // OLED normal
                }
                ThisThread::sleep_for(500ms);
            }
            // Ensure normal display at end of cycle
            if (oled_available) oled_cmd(0xA6);
        } else {
            alarm = 0;
            // Sleep 2s in 50ms chunks with button polling
            for (int sl = 0; sl < 40; sl++) {
                poll_button();
                poll_arm_button();
                if (btn_pressed || arm_btn_pressed) break;
                ThisThread::sleep_for(50ms);
            }
        }
    }
}
