/*
 * mp24/main/hal/i2c_bus.h — thin wrapper around the legacy ESP-IDF
 * `driver/i2c.h` master API.
 *
 * S-MP03 scope: single bus (main, I2C_NUM_0 @ GPIO 47/48 / 100 kHz).
 * The UMAX bus (I²C_NUM_1, GPIO 43/44) will be added when the UMAX
 * subsystem is brought up — out of scope for the current session.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Initialise I²C master on the main bus (idempotent: returns ESP_OK
 * if the driver is already installed). */
esp_err_t i2c_bus_init(void);

/* Single-byte register write: addr → [reg, val]. */
esp_err_t i2c_bus_write_reg(uint8_t addr, uint8_t reg, uint8_t val);

/* Read `len` bytes starting at `reg` on `addr`. */
esp_err_t i2c_bus_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len);

/* Bus probe: returns true if `addr` ACKs a zero-byte write. */
bool i2c_bus_probe(uint8_t addr);
