/*
 * mp24/main/hal/i2c_bus.c — main I²C bus.
 *
 * Lifted from /mnt/project/test_i2c.c with the i2c_param_config +
 * i2c_driver_install boilerplate, made idempotent so callers can use
 * i2c_bus_init() as a "ensure it's up" guard without tracking state.
 */

#include "hal/i2c_bus.h"
#include "hal/pins.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2C0";
static bool s_inited = false;

esp_err_t i2c_bus_init(void)
{
    if (s_inited) return ESP_OK;

    /* Some ESP32-S3 boot-ROM straps leave I/O pins in non-GPIO modes.
     * Reset both lines to a clean state before the I²C peripheral
     * takes them. */
    gpio_reset_pin(PIN_I2C0_SDA);
    gpio_reset_pin(PIN_I2C0_SCL);

    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = PIN_I2C0_SDA,
        .scl_io_num       = PIN_I2C0_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C0_FREQ_HZ,
    };
    esp_err_t r = i2c_param_config(I2C0_PORT, &conf);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "param_config failed: %d", r);
        return r;
    }

    r = i2c_driver_install(I2C0_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "driver_install failed: %d", r);
        return r;
    }

    s_inited = true;
    ESP_LOGI(TAG, "ready: SDA=GPIO%d SCL=GPIO%d @ %d kHz",
             PIN_I2C0_SDA, PIN_I2C0_SCL, I2C0_FREQ_HZ / 1000);
    return ESP_OK;
}

esp_err_t i2c_bus_write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(I2C0_PORT, addr, buf, 2,
                                      pdMS_TO_TICKS(I2C0_TIMEOUT_MS));
}

esp_err_t i2c_bus_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_write_read_device(I2C0_PORT, addr, &reg, 1, out, len,
                                        pdMS_TO_TICKS(I2C0_TIMEOUT_MS));
}

bool i2c_bus_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t r = i2c_master_cmd_begin(I2C0_PORT, cmd,
                                       pdMS_TO_TICKS(I2C0_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return (r == ESP_OK);
}
