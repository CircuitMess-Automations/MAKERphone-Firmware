/*
 * MP24Chatter.cpp — implementation of the MP2.4 ChatterImpl.
 *
 * Replaces Chatter-Library/src/Chatter.cpp (excluded from the
 * chatter_library component because it depends on the upstream
 * Display + InputShift + LoRa SPI bring-up, none of which apply
 * on MP2.4 hardware).
 *
 * Constructs in begin():
 *   - Display    160 × 128 ST7735, hal/display already up
 *   - MP24Input  hooked into hal/input_keypad, registered as a
 *                LoopManager listener so it gets scanButtons()
 *                called every tick
 *   - Settings.begin() persists profile / theme / volume settings
 *                via the NVS-backed CircuitOS Util/Settings
 *   - Battery is already a global; we hook it up to LoopManager
 *                here (its loop() is a no-op since hal/battery
 *                samples on its own task, but the registration
 *                keeps the upstream contract intact)
 *
 * The 'Chatter' singleton is the legacy entry point. Application
 * code does Chatter.begin() once at boot, then runs LoopManager
 * in its main loop.
 */

#include <Chatter.h>
#include <MP24Input.h>
#include <Settings.h>

extern "C" {
#include "hal/display.h"
}

ChatterImpl Chatter;

ChatterImpl::ChatterImpl() = default;

void ChatterImpl::begin(bool backlight)
{
    (void)backlight;  /* Decision C: backlight is hardware-on. */

    /* Display — 160 × 128 landscape ST7735. hal/display_init has
     * already run during the C HAL bring-up before initArduino(),
     * but Display::begin is idempotent and also allocates the
     * base Sprite. */
    if (display == nullptr) {
        display = new Display(160, 128, /*blPin=*/-1, /*rotation=*/3);
    }
    display->begin();

    /* Input — MP24Input pre-registers every btn_id_t in its
     * constructor and the base class Input sets the singleton
     * pointer. We add it as a LoopManager listener so
     * scanButtons() runs on every tick. */
    if (input == nullptr) {
        input = new MP24Input();
    }
    LoopManager::addListener(input);

    /* Settings — NVS-backed, wraps CircuitOS Util/Settings. */
    Settings.begin();

    /* Battery — the singleton from MP24Battery.cpp. begin() is a
     * no-op because hal/battery already runs its sampler, but we
     * still register it with LoopManager so any future loop()
     * extension fires consistently. */
    Battery.begin();
    LoopManager::addListener(&Battery);

    backlightOn = true;
}

void ChatterImpl::setBrightness(uint8_t /*brightness*/)
{
    /* No-op — Decision C, backlight hardware-on. */
}

void ChatterImpl::fadeIn()
{
    backlightOn = true;
}

void ChatterImpl::fadeOut()
{
    backlightOn = false;
}

void ChatterImpl::backlightOff()
{
    /* Best effort: clear the panel to black. The backlight LED
     * stays driven by Q1 G3035 but a black panel is the closest
     * visible approximation. */
    display_fill(0x0000);
    backlightOn = false;
}
