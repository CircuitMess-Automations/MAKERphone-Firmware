/*
 * mp24/main/hal/xl9555.c — XL9555 16-bit I²C GPIO expander.
 *
 * Driver patterns lifted from /mnt/project/test_i2c.c. Both ports are
 * configured as inputs at init time so the same driver instance can
 * be re-used across U5 / U9 / U3 (UMAX) without per-instance config.
 *
 * NOTE: this driver intentionally does NOT decode buttons. Decoding
 * (which bit maps to which button name) happens in the input layer
 * — see /mnt/project/dashboard.c k_buttons[] for the hardware-verified
 * map that Session S-MP04 will lift.
 */

#include "hal/xl9555.h"
#include "hal/i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "XL9555";

/* XL9555 register map (datasheet) */
#define REG_IN0     0x00
#define REG_IN1     0x01
#define REG_CFG0    0x06   /* 1 = input, 0 = output */
#define REG_CFG1    0x07

esp_err_t xl9555_init(uint8_t addr)
{
    esp_err_t r = ESP_OK;
    r |= i2c_bus_write_reg(addr, REG_CFG0, 0xFF);   /* port 0 all input */
    r |= i2c_bus_write_reg(addr, REG_CFG1, 0xFF);   /* port 1 all input */

    if (r != ESP_OK) {
        ESP_LOGE(TAG, "init @ 0x%02X failed", addr);
        return r;
    }

    /* Reading the input register clears any pending interrupt that
     * may have latched during power-up / probe. */
    (void) xl9555_read_inputs(addr);

    ESP_LOGI(TAG, "init OK @ 0x%02X (16 pins, all inputs)", addr);
    return ESP_OK;
}

uint16_t xl9555_read_inputs(uint8_t addr)
{
    uint8_t ports[2] = { 0xFF, 0xFF };
    if (i2c_bus_read_reg(addr, REG_IN0, ports, 2) != ESP_OK) {
        return 0xFFFF;   /* idle "no presses" value on failure */
    }
    return (uint16_t)ports[0] | ((uint16_t)ports[1] << 8);
}
