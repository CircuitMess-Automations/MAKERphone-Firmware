/*
 * MP24Input.cpp — see header.
 *
 * The button-count we register with the base class is BTN_COUNT
 * from hal/buttons.h. Every btn_id_t value in [0, BTN_COUNT) is
 * pre-registered so any code path that does
 * input->setBtnPressCallback(JOY_LEFT, ...) etc. finds a slot.
 *
 * The aliases that Chatter app code might use (BTN_LEFT, BTN_BACK,
 * BTN_A, BTN_B, BTN_C, BTN_L, BTN_R, BTN_ENTER) all resolve to one
 * of the JOY_*, BTN_* values in our enum via hal/buttons.h, so
 * there's no separate alias-registration step.
 */

#include "MP24Input.h"

extern "C" {
#include "hal/buttons.h"
#include "hal/input_keypad.h"
}

#include <Util/Vector.h>

MP24Input::MP24Input()
    /* The base class Input(uint8_t pinNumber) sizes its internal
     * callback / state vectors to `pinNumber`. We pass BTN_COUNT so
     * every btn_id_t value fits. */
    : Input((uint8_t) BTN_COUNT)
{
    /* Pre-register the full button enum range. preregisterButtons is
     * the same helper that InputShift / InputI2C use; it pushes the
     * pin numbers into the base class's `buttons` vector and resizes
     * the parallel state vectors so the scan loop can iterate them. */
    Vector<uint8_t> pins;
    pins.reserve(BTN_COUNT);
    for (uint8_t i = 0; i < (uint8_t) BTN_COUNT; ++i) {
        pins.push_back(i);
    }
    preregisterButtons(pins);
}

void MP24Input::scanButtons()
{
    /* Walk the registered buttons and synthesise press/release edge
     * events from the hal/input_keypad cached state. The base class
     * holds btnState[i] as the LAST observed value; comparing it to
     * the current cache and calling btnPress / btnRelease lets the
     * upstream callback + listener + hold-timer machinery do its
     * thing on top. */
    for (uint i = 0; i < buttons.size(); ++i) {
        const uint8_t  pin = buttons[i];
        const bool     now = input_keypad_is_pressed((btn_id_t) pin);
        const uint8_t  was = btnState[i];   /* base class state */
        if (now && was == 0) {
            btnPress(i);
        } else if (!now && was == 1) {
            btnRelease(i);
        }
    }
}
