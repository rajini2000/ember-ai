# M2 — On-Board RL Inference: Run Commands

## Step 1: Generate the weights header (laptop, one time only)

```bash
python m2/export_policy.py
```

This reads `models/best_model.zip` and writes `m2/ember_rl_policy.h`.
You only need to do this once (or again if you retrain the model).

---

## Step 2: Copy files into the Mbed project

Copy these 3 files into `C:\Users\Acer\Mbed Programs\ember\ember\`:

```
m2/ember_rl_policy.h       → ember/ember_rl_policy.h
m2/ember_rl_inference.h    → ember/ember_rl_inference.h
m2/ember_rl_inference.cpp  → ember/ember_rl_inference.cpp
```

---

## Step 3: Add to main.cpp

At the top of `main.cpp`, add:
```cpp
#include "ember/ember_rl_inference.h"
```

In your `main()` before the `while(true)` loop:
```cpp
ember_inference_reset();
```

Inside the `while(true)` sensor loop, replace the cloud call with:
```cpp
EmberInferenceResult_t res = ember_inference_run(
    pm1_0, pm2_5, pm10,
    temperature, humidity, pressure, gas_res,
    mq_analog, mq_digital,
    tvoc, eco2);

// Drive alarm LED
if (res.alarm_on) {
    led_red   = 0;   // RED on  (active low)
    led_green = 1;
} else {
    led_red   = 1;
    led_green = 0;   // GREEN on (active low)
}

// Print result to serial terminal
char msg[128];
snprintf(msg, sizeof(msg),
    "[LOCAL] AQI=%.1f  Q(OFF)=%.3f  Q(ON)=%.3f  -> ALARM %s\r\n",
    res.aqi, res.q_off, res.q_on,
    res.alarm_on ? "ON" : "OFF");
pc_print(msg);
```

---

## Step 4: Build and flash

1. Open Mbed Studio
2. Build project (hammer icon)
3. Flash to FRDM-K64F
4. Open serial terminal at **115200 baud**

---

## Expected serial output

```
[LOCAL] AQI=12.3   Q(OFF)=+4.821  Q(ON)=-2.105  -> ALARM OFF
[LOCAL] AQI=248.7  Q(OFF)=-8.432  Q(ON)=+19.64  -> ALARM ON
```

No internet connection needed. Decision made entirely on the K64F.
