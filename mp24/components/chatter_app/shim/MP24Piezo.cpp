/*
 * mp24/components/chatter_app/shim/MP24Piezo.cpp
 *
 * PiezoImpl implementation that routes CircuitOS Audio/Piezo
 * calls to our MP2.4 hal/piezo backend.
 *
 * CircuitOS Audio/Piezo.cpp conditionally pulls in one of three
 * impls (Piezo.impl / PiezoPWM.impl / PiezoDAC.impl) based on
 * CIRCUITOS_TONE / CIRCUITOS_PIEZO_PWM / CIRCUITOS_PIEZO_DAC
 * defines. None of those are set in our Setup.hpp because:
 *
 *   - CIRCUITOS_TONE uses Arduino's ::tone() — which on
 *     arduino-esp32 v3 routes through LEDC and is not always
 *     available depending on board-level wiring.
 *
 *   - CIRCUITOS_PIEZO_PWM uses the deprecated driver/timer.h
 *     API (timer_init, TIMER_GROUP_1, ...) that was removed in
 *     ESP-IDF v5. Doesn't compile.
 *
 *   - CIRCUITOS_PIEZO_DAC uses the ESP32 DAC, not present on
 *     ESP32-S3.
 *
 * So CircuitOS Audio/Piezo.cpp compiles to an empty TU on our
 * build. PhoneRingtoneEngine etc. reference Piezo / PiezoImpl
 * methods that are then undefined at link time — surfaced once
 * S-MP19's nav wiring pulled PhoneRingtoneEngine into the live
 * link graph.
 *
 * This file provides:
 *
 *   - `PiezoImpl Piezo;` global instance
 *   - tone()        → hal/piezo's piezo_tone(freq_hz, dur_ms)
 *   - noTone()      → hal/piezo's piezo_no_tone()
 *   - begin(pin)    → no-op (hal/piezo is initialised once by
 *                     app_main; CircuitOS clients calling begin()
 *                     with a pin number are ignored — the pin
 *                     is baked into hal/piezo at hal init)
 *   - setMute/isMuted → local bool tracking; tone() short-circuits
 *                       to noTone() while muted
 *
 * hal/piezo is the same backend the S-MP05 boot chime + S-MP04
 * keypad-click tones use, so behaviour is consistent across the
 * audio paths.
 */

#include <Audio/Piezo.h>

extern "C" {
#include "hal/piezo.h"
}

/* Global singleton — every TU that does `extern PiezoImpl Piezo;`
 * resolves to this. */
PiezoImpl Piezo;

void PiezoImpl::begin(uint8_t pin)
{
    /* CircuitOS callers pass the GPIO pin the piezo is wired to.
     * On MP2.4 hal/piezo already owns the pin (statically
     * configured in hal/pins.h + hal/piezo.c). Ignore the
     * argument; record the pin so getters/log lines that read
     * it back don't see -1.
     */
    this->pin = pin;
}

void PiezoImpl::tone(uint16_t freq, uint16_t duration)
{
    if (mute) return;
    if (volume == 0) return;
    /* hal/piezo handles freq==0 by calling piezo_no_tone() —
     * matches the Piezo.impl semantics. duration==0 means
     * 'until next call', matches hal/piezo. */
    piezo_tone(freq, duration);
}

void PiezoImpl::noTone()
{
    piezo_no_tone();
}

void PiezoImpl::setMute(bool m)
{
    this->mute = m;
    if (m) {
        noTone();
    }
}

bool PiezoImpl::isMuted() const
{
    return mute;
}
