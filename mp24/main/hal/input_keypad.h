/*
 * mp24/main/hal/input_keypad.h — interrupt-driven keypad input.
 *
 * Owns the GPIO interrupts on PIN_INT_U5 / PIN_INT_U9, the FreeRTOS
 * dispatcher task, and the listener chain. Decodes raw 16-bit
 * XL9555 input changes into `btn_id_t` press / release events using
 * the bit map lifted from /mnt/project/dashboard.c k_buttons[].
 *
 * Usage:
 *
 *   static input_listener_t my_listener = {
 *       .cb  = on_button_event,
 *       .ctx = NULL,
 *   };
 *   input_keypad_add_listener(&my_listener);
 *   input_keypad_init();  // installs ISRs, spawns task; idempotent
 *
 * Listener structs must outlive input_keypad_init(). The chain is
 * intrusive — `next` is filled in by `add_listener`, the caller must
 * leave it zero on first registration.
 *
 * Session S-MP05+ will add a C++ shim in components/circuitos_shim
 * that subscribes a single listener here and re-emits the events
 * through the CircuitOS `Input::buttonPressed/Released` API the
 * legacy firmware expects.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "hal/buttons.h"

/* Listener callback type. `pressed` is true for press, false for
 * release. `ctx` is the user-supplied opaque pointer. Called from
 * the dispatcher task context — NOT from ISR — so calling I²C /
 * display routines from the callback is fine. */
typedef void (*input_event_cb_t)(btn_id_t btn, bool pressed, void *ctx);

typedef struct input_listener {
    input_event_cb_t          cb;
    void                     *ctx;
    struct input_listener    *next;   /* filled by add_listener */
} input_listener_t;

/* Register a listener. Must be called BEFORE `input_keypad_init()`
 * so that any startup-time presses are dispatched cleanly. */
void input_keypad_add_listener(input_listener_t *l);

/* Configure INT pins, spawn the dispatch task, install ISRs.
 * Requires xl9555_init() to have been called on both expanders.
 * Idempotent. */
esp_err_t input_keypad_init(void);

/* Direct polled query — returns true if the named button is held
 * right now. Reads the latest cached state; does not re-poll I²C. */
bool input_keypad_is_pressed(btn_id_t b);

/* Cached raw 16-bit state from one of the two expanders. Useful for
 * on-screen debug overlays that want to show the whole register at
 * once. `chip` = 0 (U5 numpad/face A-B) or 1 (U9 joystick/face C-D/vol). */
uint16_t input_keypad_raw(uint8_t chip);
