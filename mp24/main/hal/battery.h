/*
 * mp24/main/hal/battery.h — battery voltage monitor + SOC estimation.
 *
 * Hardware path: VBAT → 1:1 resistor divider → ADC1_CH2 (GPIO 3).
 * VBAT_DIVIDER_RATIO = 2.0 means the ADC sees half of VBAT, so the
 * raw mV reading is multiplied by 2.0 to recover the true battery
 * voltage.
 *
 * Calibration: ESP-IDF's curve-fitting scheme using the chip's
 * factory-burned eFuse coefficients. Accurate to ~±20 mV. The
 * schematic exposes a CALIB_EN net for TL431-based reference
 * calibration that would tighten this further, but tracing the
 * schematic shows CALIB_EN is only ever an `input` to sub-sheets —
 * nothing drives it on the v2.4 PCB. Documented as a future-work
 * item; eFuse cal is more than good enough for SOC estimation.
 *
 * Sampling runs in a dedicated background task at 1 Hz. Each sample
 * is an average of 8 consecutive ADC reads to suppress noise.
 * Callers read cached values; never blocks on ADC.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Initialise ADC unit + channel, install curve-fitting calibration,
 * spawn the 1 Hz sampling task. Idempotent. */
esp_err_t battery_init(void);

/* Latest cached battery voltage in volts. Returns 0.0 before the
 * first sample completes (typically <100 ms after battery_init). */
float battery_voltage(void);

/* Linear-interpolated SOC estimate (0..100). Approximates a Li-Po
 * discharge curve:
 *   4.20V → 100%   (fully charged)
 *   4.00V →  80%
 *   3.85V →  50%
 *   3.70V →  25%
 *   3.50V →  10%
 *   3.30V →   0%   (cutoff)
 * Clamps to [0, 100]. Returns 0 before the first sample. */
int battery_percent(void);

/* True once the first ADC sample has been taken. Useful for UI code
 * that wants to suppress "0 V / 0%" before init completes. */
bool battery_ready(void);
