/*
 * ember_api_client.h
 * Ember AI — K64F HTTP Client for Render.com REST API
 *
 * Milestone 1: Sends sensor JSON to Render.com via ESP32 AT+CIPSEND,
 *              parses the alarm/AQI/command response.
 *
 * Usage:
 *   1. Implement the three UART functions at the bottom of this file
 *      using your K64F UART driver (UART0, UART1, etc.)
 *   2. Call ember_api_predict(&sensors, &response) each sensor cycle
 *   3. Check response.alarm_on and response.aqi
 */

#ifndef EMBER_API_CLIENT_H
#define EMBER_API_CLIENT_H

#include <stdint.h>

/* ── Configuration ────────────────────────────────────────── */
#define EMBER_API_HOST      "ember-ai-ews2.onrender.com"
#define EMBER_API_PORT      443                  /* HTTPS */
#define EMBER_API_PATH      "/predict"
#define EMBER_API_TIMEOUT   3000                 /* ms — M3 fallback trigger */
#define EMBER_API_DEVICE_ID "K64F-EMBER-01"     /* change per device */

/* ── Sensor data struct ───────────────────────────────────── */
typedef struct {
    float pm1_0;        /* PMS5003 PM1.0  µg/m³  */
    float pm2_5;        /* PMS5003 PM2.5  µg/m³  */
    float pm10;         /* PMS5003 PM10   µg/m³  */
    float temperature;  /* BME680  °C            */
    float humidity;     /* BME680  %RH           */
    float pressure;     /* BME680  hPa           */
    float gas;          /* BME680  Ohms          */
    float mq_analog;    /* MQ ADC  0.0–1.0 (3.3V scale) */
    int   mq_digital;   /* MQ DOUT 0 or 1        */
    float tvoc;         /* ENS160 — set 0 if offline */
    float eco2;         /* ENS160 — set 0 if offline */
} SensorData_t;

/* ── API response struct ──────────────────────────────────── */
typedef struct {
    int   alarm_on;        /* 1 = ALARM ON, 0 = ALARM OFF    */
    float aqi;             /* AQI estimate e.g. 287.5        */
    char  category[24];    /* e.g. "VERY_UNHEALTHY"          */
    char  command[16];     /* "arm", "disarm", "status", ""  */
    int   success;         /* 1 = parsed OK, 0 = parse error */
    int   http_status;     /* HTTP status code e.g. 200      */
} EmberResponse_t;

/* ── Return codes ─────────────────────────────────────────── */
#define EMBER_OK             0
#define EMBER_ERR_TIMEOUT   -1
#define EMBER_ERR_CONNECT   -2
#define EMBER_ERR_SEND      -3
#define EMBER_ERR_PARSE     -4

/* ── Public API ───────────────────────────────────────────── */

/**
 * ember_api_predict
 *
 * Sends one sensor reading to the Render.com API and fills in response.
 *
 * Returns EMBER_OK on success, negative error code on failure.
 * On failure, response->success = 0 and response->alarm_on = 0 (safe default).
 */
int ember_api_predict(const SensorData_t *sensors, EmberResponse_t *response);

/**
 * ember_api_check_wifi
 *
 * Queries the ESP32 with AT+CWJAP? to check WiFi connection status.
 * Returns 1 if connected, 0 if not connected.
 * Used by Milestone 3 dual-mode failover.
 */
int ember_api_check_wifi(void);

/* ── UART driver interface (implement these for your K64F) ── */

/**
 * esp32_send
 * Send a null-terminated string to the ESP32 via UART.
 * Example: esp32_send("AT\r\n");
 */
void esp32_send(const char *str);

/**
 * esp32_recv
 * Wait up to timeout_ms for a response containing expected_str.
 * Copy up to buf_len-1 bytes into buf (null-terminated).
 * Returns 1 if expected_str was found, 0 on timeout.
 */
int esp32_recv(char *buf, int buf_len, const char *expected_str, int timeout_ms);

/**
 * delay_ms
 * Blocking delay in milliseconds (use K64F SysTick or LPTMR).
 */
void delay_ms(uint32_t ms);

#endif /* EMBER_API_CLIENT_H */
