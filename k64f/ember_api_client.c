/*
 * ember_api_client.c
 * Ember AI — K64F HTTP Client Implementation
 *
 * Milestone 1: K64F-to-Cloud Sensor Pipeline
 *
 * Flow each sensor cycle (every 2.5 seconds):
 *   1. Build JSON payload from SensorData_t
 *   2. Open SSL TCP connection to Render.com via AT+CIPSTART
 *   3. Send HTTP POST via AT+CIPSEND
 *   4. Read HTTP response
 *   5. Parse alarm / aqi / command from JSON body
 *   6. Close connection
 *
 * Serial output format:
 *   [CLOUD] Sent 243 bytes → HTTP 200 | alarm=ON  aqi=500.0  cmd=none
 *   [CLOUD] Sent 243 bytes → HTTP 200 | alarm=OFF aqi=57.4   cmd=none
 */

#include "ember_api_client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Internal buffer sizes ────────────────────────────────── */
#define JSON_BUF_SIZE    512
#define HTTP_BUF_SIZE    1024
#define AT_BUF_SIZE      256

/* ── Internal helpers ─────────────────────────────────────── */

/**
 * build_json_payload
 * Constructs the sensor JSON string and returns its length.
 */
static int build_json_payload(const SensorData_t *s, char *buf, int buf_size)
{
    return snprintf(buf, buf_size,
        "{"
        "\"PM1.0\":%.1f,"
        "\"PM2.5\":%.1f,"
        "\"PM10\":%.1f,"
        "\"TVOC\":%.1f,"
        "\"eCO2\":%.1f,"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"pressure\":%.2f,"
        "\"gas\":%.1f,"
        "\"MQ_analog\":%.4f,"
        "\"MQ_digital\":%d,"
        "\"device_id\":\"%s\""
        "}",
        s->pm1_0, s->pm2_5, s->pm10,
        s->tvoc, s->eco2,
        s->temperature, s->humidity, s->pressure,
        s->gas,
        s->mq_analog, s->mq_digital,
        EMBER_API_DEVICE_ID
    );
}

/**
 * build_http_request
 * Wraps the JSON body in a full HTTP/1.0 POST request string.
 * HTTP/1.0 is used (not 1.1) to avoid chunked transfer encoding.
 */
static int build_http_request(const char *json_body, int json_len,
                               char *buf, int buf_size)
{
    return snprintf(buf, buf_size,
        "POST " EMBER_API_PATH " HTTP/1.0\r\n"
        "Host: " EMBER_API_HOST "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        json_len, json_body
    );
}

/**
 * parse_json_string_field
 * Finds "key":"value" in src and copies value into dest (max dest_size bytes).
 * Returns 1 on success, 0 if not found.
 */
static int parse_json_string_field(const char *src, const char *key,
                                    char *dest, int dest_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(src, search);
    if (!p) return 0;
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (!end) return 0;
    int len = (int)(end - p);
    if (len >= dest_size) len = dest_size - 1;
    strncpy(dest, p, len);
    dest[len] = '\0';
    return 1;
}

/**
 * parse_json_float_field
 * Finds "key":value in src and returns the float value.
 * Returns default_val if not found.
 */
static float parse_json_float_field(const char *src, const char *key,
                                     float default_val)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(src, search);
    if (!p) return default_val;
    p += strlen(search);
    return (float)atof(p);
}

/**
 * extract_http_status
 * Extracts the 3-digit HTTP status code from the response line.
 * e.g. "HTTP/1.0 200 OK" → 200
 */
static int extract_http_status(const char *response)
{
    const char *p = strstr(response, "HTTP/");
    if (!p) return 0;
    p = strchr(p, ' ');
    if (!p) return 0;
    return atoi(p + 1);
}

/**
 * find_json_body
 * Returns a pointer to the start of the JSON body in the HTTP response
 * (after the blank line separating headers from body).
 */
static const char *find_json_body(const char *response)
{
    const char *p = strstr(response, "\r\n\r\n");
    if (p) return p + 4;
    p = strstr(response, "\n\n");
    if (p) return p + 2;
    return NULL;
}

/* ── Public: ember_api_predict ────────────────────────────── */

int ember_api_predict(const SensorData_t *sensors, EmberResponse_t *response)
{
    char json_buf[JSON_BUF_SIZE];
    char http_buf[HTTP_BUF_SIZE];
    char at_buf[AT_BUF_SIZE];
    char rx_buf[HTTP_BUF_SIZE];

    /* Default safe response */
    memset(response, 0, sizeof(EmberResponse_t));
    response->alarm_on = 0;
    response->aqi      = 0.0f;
    strncpy(response->category, "UNKNOWN", sizeof(response->category));
    strncpy(response->command,  "",        sizeof(response->command));

    /* ── Step 1: Build JSON payload ── */
    int json_len = build_json_payload(sensors, json_buf, sizeof(json_buf));
    if (json_len <= 0 || json_len >= JSON_BUF_SIZE) {
        return EMBER_ERR_SEND;
    }

    /* ── Step 2: Build HTTP request ── */
    int http_len = build_http_request(json_buf, json_len,
                                       http_buf, sizeof(http_buf));
    if (http_len <= 0 || http_len >= HTTP_BUF_SIZE) {
        return EMBER_ERR_SEND;
    }

    /* ── Step 3: Open SSL connection via ESP32 AT ── */
    /* Close any existing connection first */
    esp32_send("AT+CIPCLOSE\r\n");
    delay_ms(100);

    snprintf(at_buf, sizeof(at_buf),
             "AT+CIPSTART=\"SSL\",\"%s\",%d\r\n",
             EMBER_API_HOST, EMBER_API_PORT);
    esp32_send(at_buf);

    if (!esp32_recv(rx_buf, sizeof(rx_buf), "OK", EMBER_API_TIMEOUT)) {
        esp32_send("AT+CIPCLOSE\r\n");
        return EMBER_ERR_CONNECT;
    }

    /* ── Step 4: Tell ESP32 how many bytes we will send ── */
    snprintf(at_buf, sizeof(at_buf), "AT+CIPSEND=%d\r\n", http_len);
    esp32_send(at_buf);

    /* ESP32 responds with '>' prompt when ready to receive data */
    if (!esp32_recv(rx_buf, sizeof(rx_buf), ">", 2000)) {
        esp32_send("AT+CIPCLOSE\r\n");
        return EMBER_ERR_SEND;
    }

    /* ── Step 5: Send the HTTP request bytes ── */
    esp32_send(http_buf);

    /* Wait for SEND OK from ESP32 */
    if (!esp32_recv(rx_buf, sizeof(rx_buf), "SEND OK", EMBER_API_TIMEOUT)) {
        esp32_send("AT+CIPCLOSE\r\n");
        return EMBER_ERR_SEND;
    }

    /* ── Step 6: Read HTTP response ── */
    /*
     * After SEND OK, the ESP32 forwards the server response as:
     *   +IPD,<len>:<data>
     * We wait for the HTTP response headers + body.
     * We look for "CLOSED" which signals end of connection (HTTP/1.0).
     */
    memset(rx_buf, 0, sizeof(rx_buf));
    if (!esp32_recv(rx_buf, sizeof(rx_buf), "CLOSED", EMBER_API_TIMEOUT)) {
        /* Partial response — try to parse what we have */
    }

    /* ── Step 7: Parse HTTP status ── */
    response->http_status = extract_http_status(rx_buf);

    /* ── Step 8: Extract JSON body ── */
    const char *json_body = find_json_body(rx_buf);

    /* Skip +IPD header if present: "+IPD,123:" */
    if (!json_body) {
        json_body = strstr(rx_buf, "+IPD,");
        if (json_body) {
            json_body = strchr(json_body, ':');
            if (json_body) json_body++;
        }
    }

    if (!json_body || strchr(json_body, '{') == NULL) {
        esp32_send("AT+CIPCLOSE\r\n");
        return EMBER_ERR_PARSE;
    }

    /* Move to the '{' of the JSON object */
    json_body = strchr(json_body, '{');

    /* ── Step 9: Parse alarm field ── */
    char alarm_str[8] = {0};
    parse_json_string_field(json_body, "alarm", alarm_str, sizeof(alarm_str));
    response->alarm_on = (strcmp(alarm_str, "ON") == 0) ? 1 : 0;

    /* ── Step 10: Parse AQI and category ── */
    response->aqi = parse_json_float_field(json_body, "aqi", 0.0f);
    parse_json_string_field(json_body, "category",
                             response->category, sizeof(response->category));

    /* ── Step 11: Parse optional command field (Milestone 4) ── */
    char cmd_str[16] = {0};
    if (parse_json_string_field(json_body, "command", cmd_str, sizeof(cmd_str))) {
        strncpy(response->command, cmd_str, sizeof(response->command) - 1);
    } else {
        strncpy(response->command, "", sizeof(response->command));
    }

    response->success = 1;

    /* ── Step 12: Print result to serial terminal ── */
    printf("[CLOUD] Sent %d bytes → HTTP %d | alarm=%-3s  aqi=%.1f  cmd=%s\r\n",
           http_len,
           response->http_status,
           response->alarm_on ? "ON" : "OFF",
           response->aqi,
           response->command[0] ? response->command : "none");

    /* Close TCP connection */
    esp32_send("AT+CIPCLOSE\r\n");
    delay_ms(50);

    return EMBER_OK;
}

/* ── Public: ember_api_check_wifi ─────────────────────────── */

int ember_api_check_wifi(void)
{
    char rx_buf[AT_BUF_SIZE];

    esp32_send("AT+CWJAP?\r\n");

    /*
     * If connected: +CWJAP:"SSID","bssid",...  followed by OK
     * If not:       No AP  followed by OK
     */
    if (!esp32_recv(rx_buf, sizeof(rx_buf), "OK", 2000)) {
        return 0;
    }

    /* "No AP" means not connected */
    if (strstr(rx_buf, "No AP") || strstr(rx_buf, "ERROR")) {
        return 0;
    }

    /* "+CWJAP:" present means connected */
    return (strstr(rx_buf, "+CWJAP:") != NULL) ? 1 : 0;
}
