/*
 * MP24Battery.cpp — provides the BatteryService method bodies +
 * the `Battery` singleton in lieu of the upstream
 * Chatter-Library/src/Battery/BatteryService.cpp (excluded by
 * the chatter_library component because it depends on
 * Util/HWRevision which we also excluded and on the v4-era
 * esp_adc_cal raw_to_voltage API).
 *
 * We reuse the upstream class declaration verbatim (the
 * <Battery/BatteryService.h> header still wins the include
 * search). All we need is link-time definitions for its methods,
 * routed to hal/battery from the C HAL — which already runs a
 * background sampler, exposes mV / percentage, and handles eFuse-
 * cal'd voltage with curve-fitting cal.
 *
 * Notes on the unused private members (`measureMicros`,
 * `measureSum`, `measureCounter`, `calChars`, `calibOffset`): the
 * upstream design implements its own averaging window — we don't,
 * because hal/battery does it for us. The members stay declared
 * (we can't change the header) but go unread.
 */

#include <Battery/BatteryService.h>

extern "C" {
#include "hal/battery.h"
}

BatteryService Battery;

void BatteryService::begin()
{
    /* hal/battery is initialised earlier in the C HAL bring-up
     * (battery_init() runs in app_main before Arduino layer
     * init), so this is a no-op. */
}

void BatteryService::loop(uint /*micros*/)
{
    /* hal/battery owns a 1Hz background sampler task — we don't
     * need to poll on every LoopManager tick. No-op. */
}

uint8_t BatteryService::getPercentage() const
{
    int p = battery_percent();
    if (p < 0)   p = 0;
    if (p > 100) p = 100;
    return (uint8_t)p;
}

uint16_t BatteryService::getVoltage() const
{
    /* battery_voltage returns volts as a float; the upstream
     * interface speaks millivolts. */
    float v = battery_voltage();
    if (v < 0.0f) v = 0.0f;
    return (uint16_t)(v * 1000.0f);
}

uint8_t BatteryService::getLevel() const
{
    /* Quantise percentage into the 0..5 bar levels that the
     * Chatter status bar uses. Thresholds match the legacy
     * BatteryElement icon set. */
    const uint8_t pct = getPercentage();
    if (pct < 5)   return 0;
    if (pct < 25)  return 1;
    if (pct < 50)  return 2;
    if (pct < 75)  return 3;
    if (pct < 95)  return 4;
    return 5;
}

int16_t BatteryService::getVoltOffset() const
{
    /* Calibration offset from the upstream design's TL431 reference
     * routine — irrelevant on MP2.4 since the schematic doesn't
     * wire the TL431 CALIB_EN net. eFuse curve-fitting cal in
     * hal/battery covers absolute accuracy. */
    return 0;
}

/* Private helpers — the class header lists them as static / non-
 * virtual, so they need definitions too. */

uint16_t BatteryService::mapReading(uint16_t reading)
{
    /* No-op pass-through. The upstream version remapped raw ADC
     * counts to millivolts using HW-revision-specific magic
     * numbers; on MP2.4 hal/battery does the eFuse cal so we
     * never see raw ADC counts here. */
    return reading;
}

void BatteryService::calibrate()
{
    /* No-op — see getVoltOffset note above. */
}
