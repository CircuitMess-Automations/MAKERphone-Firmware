/*
 * mp24/main/hal/power.c — power button polling + debounce.
 */

#include "hal/power.h"
#include "hal/pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include <stdatomic.h>

static const char *TAG = "POWER";

/* Polarity: assumed line goes LOW when the button is pressed (the
 * usual switch-to-GND + pull-up pattern). Flip this if hardware
 * observation shows the opposite. */
#define POWER_BUTTON_PRESSED_LEVEL  0

/* 50 Hz polling with N consecutive matches required to debounce. */
#define POLL_INTERVAL_MS            20
#define DEBOUNCE_CONSECUTIVE        3   /* ~60 ms */

static atomic_bool      s_pressed      = false;
static atomic_uint_fast32_t s_hold_ms  = 0;
static atomic_uint_fast32_t s_count    = 0;
static bool             s_inited       = false;

/* ----------------------------------------------------------------- */

static void poll_task(void *arg)
{
    (void)arg;
    int    streak  = 0;
    int    last_raw = 1 - POWER_BUTTON_PRESSED_LEVEL;   /* assume idle at boot */
    bool   pressed = false;
    uint32_t hold  = 0;

    while (1) {
        int raw = gpio_get_level(PIN_PWR_BUTTON);
        bool raw_pressed = (raw == POWER_BUTTON_PRESSED_LEVEL);

        if (raw == last_raw) {
            if (streak < DEBOUNCE_CONSECUTIVE) streak++;
        } else {
            streak = 1;
            last_raw = raw;
        }

        /* Only commit a new debounced state after DEBOUNCE_CONSECUTIVE
         * matching reads at the polling rate. */
        if (streak >= DEBOUNCE_CONSECUTIVE && raw_pressed != pressed) {
            pressed = raw_pressed;
            atomic_store(&s_pressed, pressed);
            if (pressed) {
                hold = 0;
                uint32_t c = (uint32_t)atomic_fetch_add(&s_count, 1) + 1;
                ESP_LOGI(TAG, "PRESS  (#%lu)", (unsigned long) c);
            } else {
                ESP_LOGI(TAG, "RELEASE  (held %lu ms)", (unsigned long) hold);
                hold = 0;
                atomic_store(&s_hold_ms, 0);
            }
        }

        if (pressed) {
            hold += POLL_INTERVAL_MS;
            atomic_store(&s_hold_ms, hold);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

/* ----------------------------------------------------------------- */
/* Public API                                                        */
/* ----------------------------------------------------------------- */

esp_err_t power_init(void)
{
    if (s_inited) return ESP_OK;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_PWR_BUTTON,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t r = gpio_config(&io);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config for PWR_BUTTON failed: %d", r);
        return r;
    }

    /* Deliberately leave PIN_PWR_OFF in its default state — see header.
     * NOT calling gpio_config on it keeps it as input/high-Z so we
     * can't accidentally kill our own supply. */

    if (xTaskCreate(poll_task, "power_btn", 2560, NULL,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "poll task spawn failed");
        return ESP_FAIL;
    }

    s_inited = true;
    ESP_LOGI(TAG, "polling GPIO%d at %d Hz, debounce %d ticks",
             PIN_PWR_BUTTON, 1000 / POLL_INTERVAL_MS,
             DEBOUNCE_CONSECUTIVE);
    return ESP_OK;
}

bool power_button_pressed(void)
{
    return atomic_load(&s_pressed);
}

uint32_t power_button_held_ms(void)
{
    return (uint32_t) atomic_load(&s_hold_ms);
}

uint32_t power_button_press_count(void)
{
    return (uint32_t) atomic_load(&s_count);
}
