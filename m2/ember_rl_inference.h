/*
 * ember_rl_inference.h  —  M2: On-Board RL Inference
 *
 * Runs the trained DQN policy network directly on the FRDM-K64F.
 * No cloud required. Decision made in < 1 ms.
 *
 * Usage in main.cpp:
 *   #include "ember_rl_inference.h"
 *
 *   // Once per sensor cycle:
 *   EmberInferenceResult_t res = ember_inference_run(
 *       pm1_0, pm2_5, pm10,
 *       temperature, humidity, pressure, gas_res,
 *       mq_analog, mq_digital,
 *       tvoc, eco2);
 *
 *   if (res.alarm_on) {
 *       led_red   = 0;   // RED on  (active low)
 *       led_green = 1;
 *   } else {
 *       led_red   = 1;
 *       led_green = 0;   // GREEN on (active low)
 *   }
 */

#ifndef EMBER_RL_INFERENCE_H
#define EMBER_RL_INFERENCE_H

#include <cstdint>

/* ── Result struct ─────────────────────────────────────────────────────────── */
typedef struct {
    int   alarm_on;    /* 1 = ALARM ON, 0 = ALARM OFF               */
    float q_off;       /* Q-value for action 0 (ALARM OFF)          */
    float q_on;        /* Q-value for action 1 (ALARM ON)           */
    float aqi;         /* Composite AQI estimate (0–500)            */
} EmberInferenceResult_t;

/* ── Public API ────────────────────────────────────────────────────────────── */

/**
 * ember_inference_reset()
 * Call once at startup to clear alarm state history.
 */
void ember_inference_reset(void);

/**
 * ember_inference_run()
 * Normalises sensor readings, runs the NN forward pass, returns action + Q-values.
 *
 * Parameters match the K64F sensor drivers directly (no pre-processing needed).
 *
 * @param pm1_0       PM1.0  (µg/m³)
 * @param pm2_5       PM2.5  (µg/m³)
 * @param pm10        PM10   (µg/m³)
 * @param temperature deg C
 * @param humidity    %RH
 * @param pressure    hPa
 * @param gas_res     BME680 gas resistance (Ohms)
 * @param mq_analog   MQ sensor ADC ratio (0.0 – 1.0, where 1.0 = 3.3 V)
 * @param mq_digital  MQ digital output (0 or 1)
 * @param tvoc        ENS160 TVOC (ppb),  0 if sensor offline
 * @param eco2        ENS160 eCO2 (ppm),  0 if sensor offline
 *
 * @return EmberInferenceResult_t
 */
EmberInferenceResult_t ember_inference_run(
    uint16_t pm1_0,  uint16_t pm2_5, uint16_t pm10,
    float    temperature, float humidity, float pressure, float gas_res,
    float    mq_analog,   int   mq_digital,
    uint16_t tvoc,   uint16_t eco2);

#endif /* EMBER_RL_INFERENCE_H */
