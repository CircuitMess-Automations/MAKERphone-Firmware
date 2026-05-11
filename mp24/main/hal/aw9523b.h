/*
 * mp24/main/hal/aw9523b.h — AW9523B (U12 @ 0x5B) I²C GPIO expander.
 *
 * Two responsibilities on this PCB:
 *   1. Drive 8 SMD LEDs (D4-D11) wired to P0_2..P0_6 and P1_5..P1_7,
 *      all active-LOW.
 *   2. Drive the MAX98357A amp's SD_MODE pin on P1_1 — must be HIGH
 *      for any audio to be produced.
 *
 * Init sequence is the canonical one from /mnt/project/test_i2c.c
 * lines 121-135 (SWRST → CTL → OUT0 → CFG0 → OUT1 → CFG1). Order is
 * non-negotiable per the hw_test session notes.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Initialise the chip. Requires i2c_bus_init() to have been called.
 * On success: SD_MODE is asserted HIGH (amp on), all LEDs are OFF. */
esp_err_t aw9523b_init(void);

/* Turn all 8 SMD LEDs on or off, preserving SD_MODE HIGH. */
void aw9523b_set_leds(bool on);
