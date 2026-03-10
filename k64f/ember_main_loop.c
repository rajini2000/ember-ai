/*
 * ember_main_loop.c
 * Ember AI — K64F Main Sensor Loop (Milestone 1)
 *
 * This file shows how to integrate ember_api_client.c into the K64F
 * main sensor loop. Paste the relevant parts into your existing firmware.
 *
 * Each cycle (every 2.5 seconds):
 *   1. Read all sensors (PMS5003, BME680, MQ)
 *   2. Fill SensorData_t struct
 *   3. Call ember_api_predict() → get alarm + AQI
 *   4. Drive PTA2 GPIO based on alarm decision
 *   5. Print full status to serial terminal
 *
 * Serial output example:
 *   [EMBER] PM2.5=15.0  Temp=27.3  MQ=0.031V
 *   [CLOUD] Sent 243 bytes → HTTP 200 | alarm=OFF  aqi=57.4  cmd=none
 *   [GPIO]  PTA2 = LOW  (alarm OFF)
 *
 *   [EMBER] PM2.5=709.0  Temp=27.5  MQ=0.439V
 *   [CLOUD] Sent 243 bytes → HTTP 200 | alarm=ON   aqi=500.0  cmd=none
 *   [GPIO]  PTA2 = HIGH (alarm ON)
 */

#include "ember_api_client.h"
#include <stdio.h>

/*
 * Replace these includes with your actual K64F/SDK headers:
 *   #include "fsl_gpio.h"
 *   #include "pms5003.h"
 *   #include "bme680.h"
 *   #include "mq_sensor.h"
 */

/* ── Alarm GPIO — PTA2 ───────────────────────────────────── */
/*
 * K64F SDK:
 *   GPIO_PinWrite(GPIOA, 2u, 1u);   // HIGH = alarm ON
 *   GPIO_PinWrite(GPIOA, 2u, 0u);   // LOW  = alarm OFF
 *
 * Replace the macros below with the actual SDK calls.
 */
#define ALARM_GPIO_ON()   GPIO_PinWrite(GPIOA, 2u, 1u)
#define ALARM_GPIO_OFF()  GPIO_PinWrite(GPIOA, 2u, 0u)

/* ── Main sensor loop (call from your RTOS task or main()) ── */
void ember_sensor_loop(void)
{
    SensorData_t  sensors;
    EmberResponse_t response;
    int result;

    /* ── Read PMS5003 ── */
    /*
     * Replace with your PMS5003 UART read function.
     * pms5003_read(&pm_data);
     */
    sensors.pm1_0 = 8.0f;    /* replace with pm_data.pm1_0  */
    sensors.pm2_5 = 15.0f;   /* replace with pm_data.pm2_5  */
    sensors.pm10  = 18.0f;   /* replace with pm_data.pm10   */

    /* ── Read BME680 ── */
    /*
     * Replace with your BME680 I2C read function.
     * bme680_read(&bme_data);
     */
    sensors.temperature = 27.3f;       /* replace with bme_data.temperature */
    sensors.humidity    = 18.2f;       /* replace with bme_data.humidity    */
    sensors.pressure    = 990.9f;      /* replace with bme_data.pressure    */
    sensors.gas         = 14523678.0f; /* replace with bme_data.gas_resistance */

    /* ── Read MQ sensor ── */
    /*
     * MQ analog: read ADC, convert to voltage (0–3.3V → 0.0–1.0 scale)
     * uint16_t adc_raw = ADC_Read(MQ_ADC_CHANNEL);
     * sensors.mq_analog = (float)adc_raw / 4095.0f;  // 12-bit ADC
     * sensors.mq_digital = GPIO_PinRead(MQ_DIGITAL_PIN);
     */
    sensors.mq_analog  = 0.031f;
    sensors.mq_digital = 0;

    /* ENS160 offline — set to 0 */
    sensors.tvoc = 0.0f;
    sensors.eco2 = 0.0f;

    /* ── Print raw sensor values ── */
    printf("[EMBER] PM2.5=%.1f  Temp=%.1f  Hum=%.1f  MQ=%.3fV  Gas=%.0f\r\n",
           sensors.pm2_5, sensors.temperature,
           sensors.humidity, sensors.mq_analog, sensors.gas);

    /* ── Call Ember AI API ── */
    result = ember_api_predict(&sensors, &response);

    if (result != EMBER_OK) {
        printf("[ERROR] API call failed (code=%d) — alarm staying OFF\r\n", result);
        ALARM_GPIO_OFF();
        return;
    }

    /* ── Drive alarm GPIO based on AI decision ── */
    if (response.alarm_on) {
        ALARM_GPIO_ON();
        printf("[GPIO]  PTA2 = HIGH (alarm ON)\r\n");
    } else {
        ALARM_GPIO_OFF();
        printf("[GPIO]  PTA2 = LOW  (alarm OFF)\r\n");
    }

    /*
     * Milestone 4 — Command parsing (bidirectional control)
     * The API response may include a command field sent from the dashboard.
     * Handle it here so the K64F responds on the next cycle.
     */
    if (response.command[0] != '\0') {
        if (strcmp(response.command, "arm") == 0) {
            ALARM_GPIO_ON();
            printf("[CMD]   arm — alarm forced ON by dashboard\r\n");

        } else if (strcmp(response.command, "disarm") == 0) {
            ALARM_GPIO_OFF();
            printf("[CMD]   disarm — alarm forced OFF by dashboard\r\n");

        } else if (strcmp(response.command, "status") == 0) {
            /* Print full sensor dump */
            printf("[STATUS] PM1.0=%.1f PM2.5=%.1f PM10=%.1f\r\n",
                   sensors.pm1_0, sensors.pm2_5, sensors.pm10);
            printf("[STATUS] Temp=%.2f Hum=%.2f Pres=%.2f Gas=%.0f\r\n",
                   sensors.temperature, sensors.humidity,
                   sensors.pressure, sensors.gas);
            printf("[STATUS] MQ=%.4f Digital=%d\r\n",
                   sensors.mq_analog, sensors.mq_digital);
        }
    }

    printf("\r\n");

    /* Wait 2.5 seconds before next cycle */
    delay_ms(2500);
}
