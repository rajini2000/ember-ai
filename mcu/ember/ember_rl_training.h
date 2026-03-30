/*
 * ─── GenAI Declaration (Claude AI - Anthropic) ───
 * Lines 24-45: ember_training_loop() function signature and parameter
 *   documentation — ~30% AI-assisted
 * All code reviewed and validated on hardware.
 */

#pragma once
#include "mbed.h"

/**
 * M1: RL Training Data Logger with Ground-Truth Labeling
 *
 * - SW2 (PTC6) brief press -> label = DANGER (1), LED turns RED
 * - SW3 (PTA4) brief press -> label = SAFE   (0), LED turns GREEN
 * - Computes 16 RL features from raw sensor values every cycle
 * - Writes timestamp + 16 features + label to /sd/training_data.csv
 * - Prints computed features to serial terminal each cycle
 */

/**
 * Call once per sensor cycle inside the while(true) loop in main.cpp.
 *
 * Pass in:
 *   - All raw sensor values from last_snapshot
 *   - sd_ready flag
 *   - References to led_red and led_green (active LOW on FRDM-K64F)
 *   - pc_print function pointer for serial output
 */
void ember_training_loop(
    uint16_t pm1_0,
    uint16_t pm2_5,
    uint16_t pm10,
    float    temperature,
    float    humidity,
    float    pressure,
    float    gas_res,
    float    mq_analog,
    int      mq_digital,
    uint16_t tvoc,
    uint16_t eco2,
    bool     sd_ready,
    DigitalOut& led_red,
    DigitalOut& led_green,
    void (*print_fn)(const char*)
);
