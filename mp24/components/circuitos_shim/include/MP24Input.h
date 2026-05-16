/*
 * MP24Input.h — MP2.4-flavoured CircuitOS Input subclass.
 *
 * Replaces InputShift (which assumes a shift-register-based button
 * matrix on Chatter hardware) with a polled adapter on top of our
 * interrupt-driven hal/input_keypad. The keypad HAL already
 * maintains a debounced cached state via XL9555 expanders + INT
 * lines; MP24Input just consults that cache on every
 * LoopManager tick.
 *
 * Registration is automatic in the constructor — every entry in our
 * btn_id_t enum (0..BTN_COUNT-1) gets pre-registered so the upstream
 * Chatter app can call setBtnPressCallback / setButtonHeldCallback
 * against any of our buttons. Hold + repeat timing is handled by the
 * base class.
 *
 * Singleton: the base class Input constructor sets
 * Input::instance = this, so Input::getInstance() returns a valid
 * MP24Input* after construction.
 *
 * Lifecycle:
 *   MP24Input input;                  // constructor pre-registers
 *   LoopManager::addListener(&input); // hook into the main loop
 *   ...
 *   LoopManager::loop();              // calls scanButtons() each pass
 */
#pragma once

#include <Input/Input.h>

class MP24Input : public Input {
public:
    MP24Input();
    ~MP24Input() = default;

protected:
    /* Called by Input::loop() on every LoopManager tick. We don't
     * actually scan here — hal/input_keypad's ISR + dispatcher task
     * keep a cached debounced state. This method compares that cache
     * against the base class's btnState[] and calls the protected
     * btnPress / btnRelease for any edges, which fans out to the
     * registered callbacks + listeners. */
    void scanButtons() override;

private:
    /* Disable copy / move — the singleton instance is set in the
     * base class constructor and copying would break it. */
    MP24Input(const MP24Input &)            = delete;
    MP24Input &operator=(const MP24Input &) = delete;
};
