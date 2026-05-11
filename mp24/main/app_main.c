/*
 * mp24/main/app_main.c
 *
 * S-MP04 — wires the input HAL to the live dashboard.
 *
 * Boot sequence:
 *   1. USB CDC banner.
 *   2. Display + boot screen (S-MP02 deliverable).
 *   3. I²C bus + AW9523B + XL9555s + interrupt-driven keypad
 *      input dispatcher (S-MP03 + S-MP04).
 *   4. A small local listener fires on every press / release —
 *      bumps a counter, captures the event for on-screen display,
 *      and logs via USB CDC.
 *   5. Forever loop: blink the LEDs at ~2.5 Hz and redraw the live
 *      dashboard at ~5 fps.
 */

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_system.h"

#include "hal/display.h"
#include "hal/i2c_bus.h"
#include "hal/pins.h"
#include "hal/aw9523b.h"
#include "hal/xl9555.h"
#include "hal/buttons.h"
#include "hal/input_keypad.h"

static const char *TAG = "MP24";

/* ----------------------------------------------------------------- */
/* Listener that captures every event for the on-screen dashboard.   */
/* ----------------------------------------------------------------- */

static atomic_uint_fast32_t s_press_count   = 0;
static atomic_uint_fast32_t s_release_count = 0;

/* Last-event snapshot. We pack [pressed:1 | btn_id:7] into a byte
 * with the top bit reserved as a "valid" flag (so bit 7 set = an
 * event has occurred; bit 6 = pressed). 0 = uninitialised. */
#define EVT_VALID   (1u << 7)
#define EVT_PRESSED (1u << 6)
static atomic_uint_fast32_t s_last_event_packed = 0;

static void on_button_event(btn_id_t btn, bool pressed, void *ctx)
{
    (void)ctx;
    if (pressed) atomic_fetch_add(&s_press_count, 1);
    else         atomic_fetch_add(&s_release_count, 1);

    uint32_t packed = EVT_VALID | ((uint32_t)btn & 0x3F)
                                | (pressed ? EVT_PRESSED : 0);
    atomic_store(&s_last_event_packed, packed);

    ESP_LOGI(TAG, "%s %s",
             pressed ? "PRESS  " : "release", btn_name(btn));
}

static input_listener_t s_listener = {
    .cb  = on_button_event,
    .ctx = NULL,
};

/* ----------------------------------------------------------------- */
/* USB CDC boot banner                                               */
/* ----------------------------------------------------------------- */

static void print_banner(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, " MAKERphone v2.4 firmware — S-MP04 input");
    ESP_LOGI(TAG, " IDF=%s  chip=%s rev%d  cores=%d",
             esp_get_idf_version(),
             (info.model == CHIP_ESP32S3) ? "ESP32-S3" : "?",
             info.revision, info.cores);
    ESP_LOGI(TAG, " free heap = %lu B   PSRAM target = 2 MB octal",
             (unsigned long) esp_get_free_heap_size());
    ESP_LOGI(TAG, "==========================================");
}

/* ----------------------------------------------------------------- */
/* Dashboard — static layout drawn once, value strips redrawn live   */
/* ----------------------------------------------------------------- */

static uint16_t MP_BG     = 0;
static uint16_t MP_ACCENT = 0;
static uint16_t MP_TEXT   = 0;
static uint16_t MP_DIM    = 0;
static uint16_t MP_HILITE = 0;

static void draw_boot_screen(void)
{
    display_fill(MP_BG);

    display_fill_rect(0, 0, TFT_WIDTH, 14, MP_ACCENT);
    display_str(4, 4, "MAKERphone v2.4", MP_BG, MP_ACCENT);

    display_str(4, 22, "S-MP04  input HAL OK",  MP_TEXT, MP_BG);
    display_str(4, 34, "23 buttons, IRQ-driven", MP_DIM,  MP_BG);

    /* Chroma stripe stays — visual confidence in every boot. */
    const int bar_y = 50;
    const int bar_h = 6;
    const int bar_w = TFT_WIDTH / 4;
    display_fill_rect(0 * bar_w, bar_y, bar_w, bar_h, COLOR_RED);
    display_fill_rect(1 * bar_w, bar_y, bar_w, bar_h, COLOR_GREEN);
    display_fill_rect(2 * bar_w, bar_y, bar_w, bar_h, COLOR_BLUE);
    display_fill_rect(3 * bar_w, bar_y, bar_w, bar_h, COLOR_WHITE);

    /* Labels for live values. */
    display_str(4, 64,  "Last :",        MP_DIM, MP_BG);
    display_str(4, 76,  "Events:",       MP_DIM, MP_BG);
    display_str(4, 90,  "U5 raw:",       MP_DIM, MP_BG);
    display_str(4, 102, "U9 raw:",       MP_DIM, MP_BG);
    display_str(4, 116, "Press any key", MP_DIM, MP_BG);
}

/* Erase a fixed strip first so changing-width values don't leave
 * artefacts from the previous frame. */
static void redraw_value(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    display_fill_rect(x, y, 110, 7, bg);
    display_str(x, y, s, fg, bg);
}

static void update_dashboard(void)
{
    char buf[32];

    /* "Last :" line */
    uint32_t evt = atomic_load(&s_last_event_packed);
    if (evt & EVT_VALID) {
        btn_id_t btn  = (btn_id_t)(evt & 0x3F);
        bool   was_p  = (evt & EVT_PRESSED) != 0;
        snprintf(buf, sizeof(buf), "%s %s",
                 was_p ? "PRESS" : "REL  ", btn_name(btn));
        redraw_value(46, 64, buf, was_p ? MP_HILITE : MP_TEXT, MP_BG);
    }

    /* "Events: N press / N rel" */
    uint32_t p = (uint32_t) atomic_load(&s_press_count);
    uint32_t r = (uint32_t) atomic_load(&s_release_count);
    snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)p, (unsigned long)r);
    redraw_value(58, 76, buf, MP_TEXT, MP_BG);

    /* Raw 16-bit cached state per chip. */
    snprintf(buf, sizeof(buf), "0x%04X", input_keypad_raw(0));
    redraw_value(58, 90, buf, MP_TEXT, MP_BG);
    snprintf(buf, sizeof(buf), "0x%04X", input_keypad_raw(1));
    redraw_value(58, 102, buf, MP_TEXT, MP_BG);
}

/* ----------------------------------------------------------------- */

void app_main(void)
{
    print_banner();

    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed — continuing headless");
    } else {
        MP_BG     = display_rgb( 20,  12,  36);
        MP_ACCENT = display_rgb(255, 140,  30);
        MP_TEXT   = display_rgb(255, 220, 180);
        MP_DIM    = display_rgb(170, 140, 200);
        MP_HILITE = display_rgb(122, 232, 255);
        draw_boot_screen();
    }

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "I²C init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "bus probe: U5=%s U9=%s AW9523B=%s",
             i2c_bus_probe(I2C_ADDR_XL9555_U5) ? "OK" : "MISSING",
             i2c_bus_probe(I2C_ADDR_XL9555_U9) ? "OK" : "MISSING",
             i2c_bus_probe(I2C_ADDR_AW9523B)   ? "OK" : "MISSING");

    if (aw9523b_init() != ESP_OK) {
        ESP_LOGE(TAG, "AW9523B init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (xl9555_init(I2C_ADDR_XL9555_U5) != ESP_OK) {
        ESP_LOGW(TAG, "XL9555 U5 init failed (continuing)");
    }
    if (xl9555_init(I2C_ADDR_XL9555_U9) != ESP_OK) {
        ESP_LOGW(TAG, "XL9555 U9 init failed (continuing)");
    }

    /* Register the listener BEFORE input_keypad_init() so any boot-time
     * presses (technician sitting on the keypad while flashing, etc.)
     * still get dispatched cleanly. */
    input_keypad_add_listener(&s_listener);

    if (input_keypad_init() != ESP_OK) {
        ESP_LOGW(TAG, "Keypad input HAL not started (continuing)");
    }

    ESP_LOGI(TAG, "Entering live dashboard loop.");
    bool led_on = false;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        led_on = !led_on;
        aw9523b_set_leds(led_on);
        update_dashboard();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(200));   /* 5 fps */
    }
}
