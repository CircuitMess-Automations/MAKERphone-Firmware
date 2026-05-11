/*
 * mp24/main/hal/buttons.h — canonical MP2.4 button identities.
 *
 * Two sets of names exist for backwards compatibility with the
 * legacy 270-session Chatter firmware:
 *
 *   1. The MP2.4-native enum values (`BTN_FACE_A`..`BTN_VOL_DOWN`)
 *      are used by all new mp24/ code.
 *
 *   2. Decision-E aliases (`BTN_LEFT`, `BTN_ENTER`, `BTN_L` etc.)
 *      remap legacy Chatter button identifiers to the closest
 *      MP2.4 equivalent. Legacy code that does `if (i == BTN_LEFT)`
 *      keeps working unchanged once the circuitos_shim is wired in.
 *
 * The four MP2.4 face buttons use the `BTN_FACE_*` prefix because
 * the legacy code repurposes `BTN_A`/`BTN_B`/`BTN_C` for completely
 * different functions (legacy `BTN_A` = the encoder OK button,
 * not a face button). New code must use `BTN_FACE_A` etc. to refer
 * to the physical face buttons; bare `BTN_A` resolves to whatever
 * the legacy semantic was.
 *
 * Indices match the dispatch table order in input_keypad.c.
 * BTN_COUNT is the sentinel and the size of `k_btn_map[]`.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    /* Numpad (right side of the keypad) */
    BTN_0 = 0,
    BTN_1,
    BTN_2,
    BTN_3,
    BTN_4,
    BTN_5,
    BTN_6,
    BTN_7,
    BTN_8,
    BTN_9,
    BTN_STAR,
    BTN_HASH,

    /* Face buttons (left side of the keypad) — prefixed FACE_ to
     * avoid colliding with the legacy `BTN_A`..`BTN_C` macros below. */
    BTN_FACE_A,
    BTN_FACE_B,
    BTN_FACE_C,
    BTN_FACE_D,

    /* 5-way joystick */
    BTN_JOY_UP,
    BTN_JOY_DOWN,
    BTN_JOY_LEFT,
    BTN_JOY_RIGHT,
    BTN_JOY_CLICK,

    /* Side volume rocker */
    BTN_VOL_UP,
    BTN_VOL_DOWN,

    BTN_COUNT
} btn_id_t;

/* ----- Decision-E legacy aliases (locked in MP24_PORT_PLAN.md §2) -----
 *
 *   legacy meaning                MP2.4 mapping
 *   --------------                -----------------
 *   BTN_LEFT  navigation left  →  joystick LEFT
 *   BTN_RIGHT navigation right →  joystick RIGHT
 *   BTN_ENTER OK confirm       →  joystick CLICK (centre press)
 *   BTN_BACK  cancel / back    →  face C
 *   BTN_L     left shoulder    →  face A
 *   BTN_R     right shoulder   →  face B
 *
 * Plus the second-tier aliases the original libraries/Chatter-Library/
 * src/Pins.hpp defines — they chain through the above so legacy code
 * referencing BTN_A/BTN_B/BTN_C/BTN_UP/BTN_DOWN ends up at the right
 * physical key:
 *
 *   BTN_UP    = BTN_LEFT  = BTN_JOY_LEFT      (encoder-style up)
 *   BTN_DOWN  = BTN_RIGHT = BTN_JOY_RIGHT     (encoder-style down)
 *   BTN_A     = BTN_ENTER = BTN_JOY_CLICK     (OK / select)
 *   BTN_B     = BTN_BACK  = BTN_FACE_C        (cancel)
 *   BTN_C     = BTN_R     = BTN_FACE_B        (legacy right shoulder)
 *
 * If you write new MP2.4 code that wants the physical key labelled
 * "A" on the face plate, use BTN_FACE_A — never bare BTN_A.
 */
#define BTN_LEFT   BTN_JOY_LEFT
#define BTN_RIGHT  BTN_JOY_RIGHT
#define BTN_ENTER  BTN_JOY_CLICK
#define BTN_BACK   BTN_FACE_C
#define BTN_L      BTN_FACE_A
#define BTN_R      BTN_FACE_B

#define BTN_UP     BTN_LEFT
#define BTN_DOWN   BTN_RIGHT
#define BTN_A      BTN_ENTER
#define BTN_B      BTN_BACK
#define BTN_C      BTN_R

/* Short human-readable label for a button, e.g. "0", "JLT", "VUP".
 * Returns "?" for out-of-range ids. Safe to call from log statements. */
const char *btn_name(btn_id_t b);
