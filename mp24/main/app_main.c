/*
 * mp24/main/app_main.c
 *
 * S-MP01 — ESP-IDF skeleton + CircuitOS shim placeholder.
 *
 * Boots the MAKERphone v2.4 hardware as far as: USB CDC console up,
 * I²C bus initialised, AW9523B configured with the canonical init
 * sequence from the standalone hw_test project (lifted from
 * test_i2c.c lines 121-135), then blinks the 8 on-board SMD LEDs
 * forever so a flashed board produces a visible "alive" indicator.
 *
 * This file is intentionally monolithic — Session 3 will refactor
 * I²C and the AW9523B driver into a `hal/` sub-directory once the
 * second consumer (XL9555 expanders) shows up.
 *
 * Lifted constants are tagged "[hw_test:test_i2c.c]" so the
 * provenance is grep-able when we revisit.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

static const char *TAG = "MP24";

/* ----------------------------------------------------------------- */
/* Pin & address constants — from /mnt/project/pin_config.h          */
/* ----------------------------------------------------------------- */
#define PIN_I2C0_SDA          47
#define PIN_I2C0_SCL          48
#define I2C0_FREQ_HZ          100000
#define I2C0_PORT             I2C_NUM_0
#define I2C0_TIMEOUT_MS       100

#define I2C_ADDR_AW9523B      0x5B   /* U12: AD0=AD1=VCC */

/* AW9523B register map (datasheet rev 1.0) */
#define AW9523B_REG_IN0       0x00
#define AW9523B_REG_IN1       0x01
#define AW9523B_REG_OUT0      0x02
#define AW9523B_REG_OUT1      0x03
#define AW9523B_REG_CFG0      0x04   /* 1 = input, 0 = output */
#define AW9523B_REG_CFG1      0x05
#define AW9523B_REG_ID        0x10   /* returns 0x23 */
#define AW9523B_REG_CTL       0x11   /* bit4: 0 = P0 push-pull, 1 = open-drain */
#define AW9523B_REG_SWRST     0x7F   /* write 0x00 to soft-reset */

/* AW9523B output masks — [hw_test:test_i2c.c:219-222]
 * OUT0 0x83 (10000011): P0_2..P0_6 LOW = LED_3, LED_6, LED_4, LED_7, LED_5 on
 * OUT1 0x1F (00011111): P1_5,P1_6,P1_7 LOW = LED_8, LED_2, LED_1 on
 *                       P1_1 must stay HIGH to keep the amp enabled (SD_MODE)
 * Bit math: 0x1F has bits 0..4 set (HIGH); bits 5,6,7 clear (LOW = LEDs on).
 * P1_0 (TFT_BL_DRAIN) is configured as input — hardware backlight, leave alone.
 */
#define AW9523B_LEDS_ON_OUT0  0x83
#define AW9523B_LEDS_ON_OUT1  0x1F
#define AW9523B_LEDS_OFF_OUT0 0xFF
/* When LEDs off, P1_1 (SD_MODE) MUST still be HIGH to keep the amp alive.
 * 0xFF achieves that — every P1 output bit is HIGH. */
#define AW9523B_LEDS_OFF_OUT1 0xFF

/* ----------------------------------------------------------------- */
/* Minimal I²C helpers — modelled on [hw_test:test_i2c.c:49-59]      */
/* ----------------------------------------------------------------- */

static esp_err_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(I2C0_PORT, addr, buf, 2,
                                      pdMS_TO_TICKS(I2C0_TIMEOUT_MS));
}

static esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_write_read_device(I2C0_PORT, addr, &reg, 1, out, len,
                                        pdMS_TO_TICKS(I2C0_TIMEOUT_MS));
}

static esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = PIN_I2C0_SDA,
        .scl_io_num       = PIN_I2C0_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C0_FREQ_HZ,
    };
    esp_err_t r = i2c_param_config(I2C0_PORT, &conf);
    if (r != ESP_OK) return r;
    r = i2c_driver_install(I2C0_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (r != ESP_OK) return r;
    ESP_LOGI(TAG, "[I2C0] init: SDA=GPIO%d SCL=GPIO%d @ %d kHz",
             PIN_I2C0_SDA, PIN_I2C0_SCL, I2C0_FREQ_HZ / 1000);
    return ESP_OK;
}

/* ----------------------------------------------------------------- */
/* AW9523B init — canonical sequence [hw_test:test_i2c.c:121-135]    */
/* Order is non-negotiable: SWRST → CTL → OUT0 → CFG0 → OUT1 → CFG1. */
/* ----------------------------------------------------------------- */

static esp_err_t aw9523b_init(void)
{
    uint8_t id = 0;
    esp_err_t r = i2c_read_reg(I2C_ADDR_AW9523B, AW9523B_REG_ID, &id, 1);
    if (r != ESP_OK || id != 0x23) {
        ESP_LOGE(TAG, "[AW9523B] not found at 0x%02X (id=0x%02X, err=%d)",
                 I2C_ADDR_AW9523B, id, r);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[AW9523B] found @ 0x%02X (id=0x%02X)", I2C_ADDR_AW9523B, id);

    /* Soft-reset: register state may be undefined after power-up */
    i2c_write_reg(I2C_ADDR_AW9523B, AW9523B_REG_SWRST, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* CTL bit4 = 0 → P0 push-pull (driving LEDs from P0_2..P0_6) */
    i2c_write_reg(I2C_ADDR_AW9523B, AW9523B_REG_CTL, 0x00);

    /* OUT0 = 0xFF: all P0 outputs HIGH (LEDs are active-LOW → off) */
    i2c_write_reg(I2C_ADDR_AW9523B, AW9523B_REG_OUT0, AW9523B_LEDS_OFF_OUT0);

    /* CFG0 = 0x83 (10000011): P0_2..P0_6 as outputs, rest input */
    i2c_write_reg(I2C_ADDR_AW9523B, AW9523B_REG_CFG0, 0x83);

    /* OUT1 = 0xFF: P1 all HIGH (P1_1 = SD_MODE HIGH → MAX98357A enabled) */
    i2c_write_reg(I2C_ADDR_AW9523B, AW9523B_REG_OUT1, AW9523B_LEDS_OFF_OUT1);

    /* CFG1 = 0x1D (00011101): P1_7,P1_6,P1_5,P1_1 as outputs.
     * P1_0 (TFT_BL_DRAIN) stays input — backlight is hardware-on. */
    i2c_write_reg(I2C_ADDR_AW9523B, AW9523B_REG_CFG1, 0x1D);

    ESP_LOGI(TAG, "[AW9523B] init OK — 8 LEDs configured, SD_MODE asserted");
    return ESP_OK;
}

static void aw9523b_set_leds(bool on)
{
    i2c_write_reg(I2C_ADDR_AW9523B, AW9523B_REG_OUT0,
                  on ? AW9523B_LEDS_ON_OUT0 : AW9523B_LEDS_OFF_OUT0);
    i2c_write_reg(I2C_ADDR_AW9523B, AW9523B_REG_OUT1,
                  on ? AW9523B_LEDS_ON_OUT1 : AW9523B_LEDS_OFF_OUT1);
}

/* ----------------------------------------------------------------- */
/* Boot banner — useful when the user has the USB CDC console open   */
/* ----------------------------------------------------------------- */

static void print_banner(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, " MAKERphone v2.4 firmware — S-MP01 skeleton");
    ESP_LOGI(TAG, " IDF=%s  chip=%s rev%d  cores=%d",
             esp_get_idf_version(),
             (info.model == CHIP_ESP32S3) ? "ESP32-S3" : "?",
             info.revision, info.cores);
    ESP_LOGI(TAG, " free heap = %lu B   PSRAM target = 2 MB octal",
             (unsigned long) esp_get_free_heap_size());
    ESP_LOGI(TAG, "==========================================");
}

/* ----------------------------------------------------------------- */
/* app_main                                                          */
/* ----------------------------------------------------------------- */

void app_main(void)
{
    print_banner();

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "I²C init failed — halting in a tight loop");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (aw9523b_init() != ESP_OK) {
        ESP_LOGE(TAG, "AW9523B init failed — halting in a tight loop");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Blinking 8 SMD LEDs @ 2 Hz — power-cycle to exit.");
    bool on = false;
    for (;;) {
        on = !on;
        aw9523b_set_leds(on);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
