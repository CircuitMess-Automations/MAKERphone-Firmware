/*
 * mp24/main/hal/xl9555.h — generic XL9555 16-bit I²C GPIO expander.
 *
 * MP2.4 has three of these:
 *   U5 @ 0x20 — numpad (1-9, *, #, 0) + A + B + (USB_DETECT)
 *   U9 @ 0x21 — joystick (5-way) + C + D + VOL UP/DN
 *   U3 @ ?    — on the UMAX bus (Session 8+ when UMAX is brought up)
 *
 * All keypad buttons are wired active-LOW (closed switch pulls the
 * expander pin to GND). Every port is configured as input at boot;
 * the few non-button bits (e.g. USB_DETECT on U5 P0.4) are also read
 * as inputs — they just don't decode to a button in dashboard.c's
 * k_buttons[] table.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Configure both ports of the XL9555 at `addr` as inputs. Idempotent. */
esp_err_t xl9555_init(uint8_t addr);

/* Read the 16-bit input register from `addr`. Low byte = port 0,
 * high byte = port 1. Returns 0xFFFF on read failure (matches the
 * idle "no buttons pressed" value so failures degrade safely). */
uint16_t xl9555_read_inputs(uint8_t addr);
