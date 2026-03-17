# Ember AI — Full Milestone TODO List
Every task, in order, for all 5 milestones.
✅ = done | ⬜ = not done | 🔧 = needs Mirac's zip first

---

## MILESTONE 1 — RL Training Data Logger
**Demo: March 12 | Status: Code written, not yet on hardware**

| # | Task | Status | Notes |
|---|---|---|---|
| 1.1 | Write `ember_training_logger.h` | ✅ | Done — structs + function declarations |
| 1.2 | Write `ember_training_logger.c` | ✅ | Done — feature computation + SD write logic |
| 1.3 | Get Mirac's MCUXpresso project zip | ⬜ | Ask him today, he sends after 11pm |
| 1.4 | Implement `sw2_pressed()` using his GPIO driver | 🔧 | SW2 = PTC6 on FRDM-K64F |
| 1.5 | Implement `sw3_pressed()` using his GPIO driver | 🔧 | SW3 = PTA4 on FRDM-K64F |
| 1.6 | Implement `rgb_led_set(r,g,b)` using his LED pins | 🔧 | Red=PTB22, Green=PTE26, Blue=PTB21 |
| 1.7 | Implement `get_timestamp()` using his RTC or counter | 🔧 | If no RTC, use cycle count as "HH:MM:SS" |
| 1.8 | Implement `sd_append_line()` using his SD/FatFS driver | 🔧 | Match his existing SD write function |
| 1.9 | Add `ember_training_logger.h/.c` to MCUXpresso project | 🔧 | Right-click project → Add existing files |
| 1.10 | Add `#include "ember_training_logger.h"` in main.c | 🔧 | |
| 1.11 | Call `ember_logger_init()` once in startup | 🔧 | Before the main loop |
| 1.12 | Call `ember_training_loop()` in the 2.5s sensor cycle | 🔧 | Replace stubs with his sensor functions |
| 1.13 | Compile — fix any errors | 🔧 | |
| 1.14 | Flash to K64F | 🔧 | Via MCUXpresso Debug |
| 1.15 | Open serial terminal (115200 baud) | 🔧 | Check features printing every 2.5s |
| 1.16 | Press SW3 → verify LED turns green, label=SAFE(0) in serial | 🔧 | |
| 1.17 | Press SW2 → verify LED turns red, label=DANGER(1) in serial | 🔧 | |
| 1.18 | Let it run 30 seconds → remove SD card → open CSV on laptop | 🔧 | Check 16 columns + label column |
| 1.19 | Verify delta_pm25 changes when you blow near sensor | 🔧 | Should go from ~0 to positive |
| 1.20 | Prepare demo: serial terminal open + SD card ready to show | ⬜ | For March 12 |

---

## MILESTONE 2 — On-Board RL Inference in C
**Demo: March 17 | Status: Not started**

| # | Task | Status | Notes |
|---|---|---|---|
| 2.1 | Write `export_policy.py` — Python script to extract Q-table from `best_model.zip` | ⬜ | Run on laptop, outputs `ember_rl_policy.h` |
| 2.2 | Run `export_policy.py` → verify `ember_rl_policy.h` is generated | ⬜ | Should be a C array of floats |
| 2.3 | Write `ember_rl_inference.h` — structs + function declarations | ⬜ | State discretization, Q-lookup |
| 2.4 | Write `ember_rl_inference.c` — feature→state discretization | ⬜ | Bin each of 16 features into buckets |
| 2.5 | Write `ember_rl_inference.c` — Q-value lookup from flash table | ⬜ | Pick action with higher Q-value |
| 2.6 | Write `ember_rl_inference.c` — drive GPIO PTA2 based on decision | ⬜ | HIGH=alarm ON, LOW=alarm OFF |
| 2.7 | Add `ember_rl_policy.h`, `ember_rl_inference.h/.c` to MCUXpresso | 🔧 | Needs Mirac's project |
| 2.8 | Call inference from main loop after feature computation | 🔧 | Reuse same 16 features from M1 |
| 2.9 | Compile — fix any errors | 🔧 | |
| 2.10 | Flash to K64F | 🔧 | |
| 2.11 | Serial: verify Q-values printing each cycle | 🔧 | Should show Q(OFF) and Q(ON) values |
| 2.12 | Serial: verify selected action matches expected | 🔧 | Clean air → OFF, smoke → ON |
| 2.13 | Disconnect WiFi entirely → verify alarm still works | 🔧 | No network call should happen |
| 2.14 | Bring smoke/vape near sensor → verify PTA2 GPIO goes HIGH | 🔧 | Physical alarm activates |
| 2.15 | Compare local decision vs cloud decision for same reading | 🔧 | They should usually agree |
| 2.16 | Prepare demo: serial showing Q-values + PTA2 activating | ⬜ | For March 17 |

---

## MILESTONE 3 — Dual-Mode Cloud + Local Failover
**Demo: March 19 | Status: HTTP client written, dual-mode logic not yet written**

| # | Task | Status | Notes |
|---|---|---|---|
| 3.1 | Write `ember_dual_mode.h` — mode enum, function declarations | ⬜ | CLOUD_MODE / LOCAL_MODE |
| 3.2 | Write `ember_dual_mode.c` — `AT+CWJAP?` WiFi status check | ⬜ | Parse "No AP" vs "+CWJAP:" |
| 3.3 | Write `ember_dual_mode.c` — CLOUD mode: HTTP POST to API, parse response | ⬜ | Reuse `ember_api_client.c` |
| 3.4 | Write `ember_dual_mode.c` — LOCAL mode: call M2 Q-table inference | ⬜ | Reuse `ember_rl_inference.c` |
| 3.5 | Write `ember_dual_mode.c` — 3 second timeout detection → switch to LOCAL | ⬜ | If no response within 3s |
| 3.6 | Write `ember_dual_mode.c` — auto-switch back to CLOUD when WiFi reconnects | ⬜ | Check every cycle |
| 3.7 | Add all files to MCUXpresso project | 🔧 | |
| 3.8 | Replace main loop alarm decision with `ember_dual_mode_decide()` | 🔧 | |
| 3.9 | Compile — fix any errors | 🔧 | |
| 3.10 | Flash to K64F | 🔧 | |
| 3.11 | Serial: verify `[CLOUD]` appears when WiFi connected | 🔧 | |
| 3.12 | Disconnect WiFi physically → verify switches to `[LOCAL]` within 2.5s | 🔧 | |
| 3.13 | Alarm still works during WiFi disconnection | 🔧 | No interruption |
| 3.14 | Reconnect WiFi → verify switches back to `[CLOUD]` automatically | 🔧 | |
| 3.15 | Block API URL (modify URL to wrong address) → verify timeout triggers LOCAL | 🔧 | |
| 3.16 | Open live dashboard in browser — verify data appears when in CLOUD mode | ⬜ | `https://ember-ai-ews2.onrender.com/` |
| 3.17 | Prepare demo: show CLOUD → disconnect WiFi → LOCAL → reconnect → CLOUD | ⬜ | For March 19 |

---

## MILESTONE 4 — Embedded Alert History + Event Replay
**Demo: March 24 | Status: Not started**

| # | Task | Status | Notes |
|---|---|---|---|
| 4.1 | Write `ember_alert_log.h` — log entry struct, function declarations | ⬜ | |
| 4.2 | Write `ember_alert_log.c` — log alarm event to `alert_log.csv` on SD | ⬜ | Triggered when alarm OFF→ON |
| 4.3 | Each log entry: timestamp, 16 features, Q(OFF), Q(ON), action, mode | ⬜ | |
| 4.4 | Write `ember_alert_log.c` — serial input listener for "REPLAY" command | ⬜ | Read from UART, compare string |
| 4.5 | "REPLAY" handler: read last 10 rows from `alert_log.csv`, print formatted table | ⬜ | K64F reads its own SD card |
| 4.6 | Write `ember_alert_log.c` — "STATS" handler | ⬜ | |
| 4.7 | STATS: compute total alert count from CSV | ⬜ | Count rows |
| 4.8 | STATS: compute average alert duration | ⬜ | Need ON and OFF timestamps |
| 4.9 | STATS: find most frequent time-of-day for alerts | ⬜ | Bin by hour |
| 4.10 | STATS: find sensor feature with highest average value during alarms | ⬜ | Loop through 16 features |
| 4.11 | Add files to MCUXpresso project | 🔧 | |
| 4.12 | Call `ember_alert_log_check_alarm()` in main loop after alarm decision | 🔧 | |
| 4.13 | Compile — fix any errors | 🔧 | |
| 4.14 | Flash to K64F | 🔧 | |
| 4.15 | Trigger alarm (blow smoke near sensor) → check `alert_log.csv` on SD | 🔧 | |
| 4.16 | Type "REPLAY" in serial terminal → verify last 10 events print | 🔧 | |
| 4.17 | Type "STATS" in serial terminal → verify statistics print | 🔧 | |
| 4.18 | Trigger 3–5 alarms → run STATS → check numbers are correct | 🔧 | |
| 4.19 | Prepare demo: serial terminal open, trigger alarm, run REPLAY + STATS | ⬜ | For March 24 |

---

## MILESTONE 5 — Embedded Sensor Calibration
**Demo: March 26 | Status: Not started**

| # | Task | Status | Notes |
|---|---|---|---|
| 5.1 | Write `ember_calibration.h` — params struct, function declarations | ⬜ | |
| 5.2 | Write `ember_calibration.c` — SW2 long-press detection (3 seconds) | ⬜ | Count how long SW2 is held |
| 5.3 | Write `ember_calibration.c` — enter calibration mode: LED starts blinking | ⬜ | Toggle LED every 500ms |
| 5.4 | Write `ember_calibration.c` — collect 24 cycles (60 seconds) of all sensor readings | ⬜ | Running sum + sum-of-squares |
| 5.5 | Write `ember_calibration.c` — compute min, max, mean, std for each of 16 features | ⬜ | std = sqrt(sum_sq/N - mean²) |
| 5.6 | Write `ember_calibration.c` — write `calibration.csv` to SD card | ⬜ | One row per feature |
| 5.7 | Write `ember_calibration.c` — LED turns solid green when done | ⬜ | |
| 5.8 | Write `ember_calibration.c` — `ember_calibration_load()` reads CSV on boot | ⬜ | Parses min/max/mean/std per feature |
| 5.9 | Modify `ember_logger_compute_features()` to apply normalization if loaded | ⬜ | `normalized = (raw - mean) / std` |
| 5.10 | Add files to MCUXpresso project | 🔧 | |
| 5.11 | Call `ember_calibration_load()` at startup before main loop | 🔧 | |
| 5.12 | Call `ember_calibration_check_sw2()` in main loop | 🔧 | Detects 3-second hold |
| 5.13 | Compile — fix any errors | 🔧 | |
| 5.14 | Flash to K64F | 🔧 | |
| 5.15 | Hold SW2 3 seconds → verify LED blinks → wait 60s → LED turns solid green | 🔧 | |
| 5.16 | Remove SD card → check `calibration.csv` has correct min/max/mean/std | 🔧 | |
| 5.17 | Power off K64F → power on → serial shows "Calibration loaded from SD" | 🔧 | |
| 5.18 | Serial: verify normalized feature values are different from raw | 🔧 | Should be roughly 0–1 range |
| 5.19 | Compare AI accuracy before and after calibration | 🔧 | Optional but impressive |
| 5.20 | Prepare demo: hold SW2, show blinking, show solid green, show CSV | ⬜ | For March 26 |

---

## Summary — What's Blocking Everything

| Blocker | Unblocks |
|---|---|
| Get Mirac's project zip | Tasks 1.4 → 1.20, and all 🔧 tasks in M2–M5 |
| Write `export_policy.py` + generate `ember_rl_policy.h` | All of M2 |
| Finish M2 | M3 (needs Q-table for local mode) |
| Finish M3 | M4 (needs alarm events to log) |

**Right now: get Mirac's zip. Everything else follows from that.**
