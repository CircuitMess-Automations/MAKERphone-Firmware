/*
 * mp24/main/hal/power.h — power button + shutdown HAL.
 *
 * The MAKERphone v2.4 power system has a single physical side button
 * tied through a load-switch IC in the Power_supply sub-sheet. While
 * the system is running, the button's state is mirrored onto GPIO2
 * (uBUTTON_PWR) so software can detect:
 *   - Short press → "back to home" or wake the screen (UX layer)
 *   - Long press   → graceful shutdown (drive uPOWER_OFF on GPIO1
 *                    to tell the load switch to cut the rail)
 *
 * Phase 1 (this session) is READ-ONLY: we configure GPIO2 as input
 * with pull-up, poll it from a background task at 50 Hz, expose the
 * current state and a press-duration counter, and emit log events
 * on edges. uPOWER_OFF (GPIO1) is NOT touched — driving it wrong
 * cuts our own supply mid-test, so we verify polarity from observed
 * load-switch behaviour first.
 *
 * Phase 2 (later session) will add the actual `power_shutdown()`
 * call once the kill-signal polarity is confirmed.
 *
 * Assumed button electrical behaviour:
 *   - Idle (not pressed) → line HIGH at ESP32 (PMIC internal pull-up)
 *   - Pressed            → line LOW
 * If observation shows the opposite polarity, flip POWER_BUTTON_PRESSED_LEVEL
 * in power.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Configure GPIO2 as input pull-up; spawn the 50 Hz poller task.
 * Idempotent. Returns ESP_FAIL if GPIO config fails. */
esp_err_t power_init(void);

/* True iff the power button is currently pressed (debounced). */
bool power_button_pressed(void);

/* How long the button has been continuously held in milliseconds.
 * 0 if currently released. Wraps at UINT32_MAX after ~49 days of
 * holding which is fine. */
uint32_t power_button_held_ms(void);

/* Total number of press events observed since boot (rising edges on
 * the "pressed" signal). Lets the dashboard show a running count. */
uint32_t power_button_press_count(void);
