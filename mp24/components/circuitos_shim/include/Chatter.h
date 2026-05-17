/*
 * MP2.4 shim — Chatter.h.
 *
 * Replaces the upstream Chatter-Library/src/Chatter.h, which:
 *   - holds 'InputShift* input;' (Chatter hardware uses a
 *     shift-register button matrix; we use I²C expanders so our
 *     Input subclass is MP24Input, NOT InputShift),
 *   - holds 'SPIClass spiLoRa;' and exposes getSPILoRa() (MP2.4
 *     has no LoRa — the cellular modem replaces it; see
 *     S-MP08+ memory),
 *   - implements setBrightness via PWM on PIN_BL (MP2.4 hardware-
 *     forces backlight on through Q1 G3035 per Decision C).
 *
 * Our shim narrows the class to what makes sense on MP2.4:
 *   - 'Input* input;' (covariant return — getInput() returns a
 *     pointer to MP24Input, but the API stays Input*),
 *   - no LoRa SPI member or accessor,
 *   - setBrightness / fadeIn / fadeOut are no-ops; backlightOff()
 *     blanks the display via hal/display_fill(0x0000) since we
 *     can't actually kill the backlight.
 *
 * The public-API signatures that DO survive match the upstream
 * exactly so app code calling Chatter.getDisplay() / getInput() /
 * setBrightness() compiles unchanged. App code that calls
 * getSPILoRa() will fail to compile — that's intentional, those
 * call sites need MP2.4-flavoured replacements.
 *
 * include-search ordering: main REQUIRES circuitos_shim BEFORE
 * chatter_library, so `<Chatter.h>` from main finds THIS file
 * first. For now no chatter_library .cpp includes Chatter.h either
 * (Settings.cpp is the only thing compiled), so there's no ODR
 * conflict between this shim header and the upstream copy.
 */
#pragma once

#include <Arduino.h>
#include <CircuitOS.h>
#include <Display/Display.h>
#include <Loop/LoopManager.h>
#include <Loop/LoopListener.h>
#include <Input/Input.h>
#include <Battery/BatteryService.h>
/*
 * S-MP20/6d: pull <Pins.hpp> transitively.
 *
 * Upstream Chatter games (Snake/SpaceInvaders/Bonk/SpaceRocks)
 * include <Chatter.h> but NOT <Pins.hpp>, then use BTN_* tokens
 * directly in setBtnPressCallback() arguments. The Decision-E
 * BTN_LEFT / BTN_A / BTN_UP / BTN_1..9 aliases live in
 * hal/buttons.h; the shim <Pins.hpp> is what pulls hal/buttons.h
 * into a TU. Including it here once makes BTN_* visible to every
 * .cpp that goes through the shim Chatter.h -- the games compile
 * unchanged.
 *
 * Safe addition for the existing 9 in-SRCS files that pull
 * <Chatter.h>: each of those that uses BTN_* already includes
 * <Pins.hpp> directly (PhoneBrightnessScreen, PhoneFlashlight,
 * SettingsScreen), and the others don't reference any of the
 * Pins.hpp macros. No redefinition / no new symbols leak. */
#include <Pins.hpp>


class ChatterImpl {
public:
    ChatterImpl();

    /* Initialise display, input, battery, settings — everything
     * an MP2.4 phone needs from the legacy 'Chatter.begin()' call
     * site. Idempotent. */
    void begin(bool backlight = true);

    /* No-op on MP2.4 — backlight is hardware-on via Q1 G3035. The
     * signature is preserved so legacy call sites compile. */
    void setBrightness(uint8_t brightness);
    void fadeIn();
    void fadeOut();
    bool backlightPowered() const { return backlightOn; }

    /* Best-effort: clears the panel to black (Decision C — the
     * backlight itself can't be killed, but a black panel is the
     * closest visual approximation). */
    void backlightOff();

    Display *getDisplay() { return display; }
    Input   *getInput()   { return input;   }

private:
    Display *display = nullptr;
    Input   *input   = nullptr;
    bool     backlightOn = true;
};

extern ChatterImpl Chatter;
