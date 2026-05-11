/*
 * mp24/main/hal/aw9523b.c — AW9523B I²C expander.
 *
 * Lifted from app_main.c (S-MP01) which lifted from
 * /mnt/project/test_i2c.c lines 121-135. The init sequence order is
 * non-negotiable: SWRST → CTL → OUT0 → CFG0 → OUT1 → CFG1. Skipping
 * the soft-reset or re-ordering the CTL write causes the chip to
 * latch in open-drain mode and the LEDs never light up.
 */

#include "hal/aw9523b.h"
#include "hal/i2c_bus.h"
#include "hal/pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AW9523B";

/* AW9523B register map (datasheet rev 1.0) */
#define REG_IN0     0x00
#define REG_IN1     0x01
#define REG_OUT0    0x02
#define REG_OUT1    0x03
#define REG_CFG0    0x04   /* 1 = input, 0 = output */
#define REG_CFG1    0x05
#define REG_ID      0x10   /* returns 0x23 */
#define REG_CTL     0x11   /* bit4: 0 = P0 push-pull, 1 = open-drain */
#define REG_SWRST   0x7F   /* write 0x00 to soft-reset */

/* LED output masks — verified in /mnt/project/test_i2c.c:219-222.
 *
 * OUT0 = 0x83  →  10000011  →  P0_2..P0_6 LOW = LED_3, LED_6, LED_4,
 *                              LED_7, LED_5 ON.
 *                              P0_0, P0_1, P0_7 stay HIGH (no LED).
 * OUT1 = 0x1F  →  00011111  →  P1_5, P1_6, P1_7 LOW = LED_8, LED_2,
 *                              LED_1 ON.
 *                              P1_1 (SD_MODE) MUST stay HIGH; bit 1 of
 *                              0x1F is HIGH so we're safe.
 *
 * When LEDs are OFF, all output bits go HIGH (0xFF on both ports).
 * P1_1 (SD_MODE) HIGH on 0xFF keeps the amp enabled.
 */
#define LEDS_ON_OUT0    0x83
#define LEDS_ON_OUT1    0x1F
#define LEDS_OFF_OUT0   0xFF
#define LEDS_OFF_OUT1   0xFF

esp_err_t aw9523b_init(void)
{
    uint8_t id = 0;
    esp_err_t r = i2c_bus_read_reg(I2C_ADDR_AW9523B, REG_ID, &id, 1);
    if (r != ESP_OK || id != 0x23) {
        ESP_LOGE(TAG, "not found at 0x%02X (id=0x%02X, err=%d)",
                 I2C_ADDR_AW9523B, id, r);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "found @ 0x%02X (id=0x%02X)", I2C_ADDR_AW9523B, id);

    /* Soft-reset — power-up register state is undefined. */
    i2c_bus_write_reg(I2C_ADDR_AW9523B, REG_SWRST, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* CTL bit4 = 0 → P0 push-pull (the LED side wants to source current). */
    i2c_bus_write_reg(I2C_ADDR_AW9523B, REG_CTL, 0x00);

    /* OUT0 first, THEN CFG0: configure the latch before enabling outputs
     * so we don't accidentally pulse the LEDs ON at boot. */
    i2c_bus_write_reg(I2C_ADDR_AW9523B, REG_OUT0, LEDS_OFF_OUT0);
    i2c_bus_write_reg(I2C_ADDR_AW9523B, REG_CFG0, 0x83);   /* P0_2..P0_6 = output */

    /* P1: same order. 0xFF on OUT1 leaves P1_1 (SD_MODE) HIGH = amp on. */
    i2c_bus_write_reg(I2C_ADDR_AW9523B, REG_OUT1, LEDS_OFF_OUT1);
    /* CFG1 = 0x1D  →  00011101  →  P1_1, P1_5, P1_6, P1_7 = output.
     * P1_0 (TFT_BL_DRAIN) stays input — backlight is hardware-on. */
    i2c_bus_write_reg(I2C_ADDR_AW9523B, REG_CFG1, 0x1D);

    ESP_LOGI(TAG, "init OK — 8 LEDs configured, SD_MODE (P1_1) asserted");
    return ESP_OK;
}

void aw9523b_set_leds(bool on)
{
    i2c_bus_write_reg(I2C_ADDR_AW9523B, REG_OUT0,
                      on ? LEDS_ON_OUT0 : LEDS_OFF_OUT0);
    i2c_bus_write_reg(I2C_ADDR_AW9523B, REG_OUT1,
                      on ? LEDS_ON_OUT1 : LEDS_OFF_OUT1);
}
