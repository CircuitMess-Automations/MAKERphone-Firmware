/*
 * mp24/main/lvgl_glue.c — see header.
 */

#include "lvgl_glue.h"
#include "hal/display.h"
#include "hal/buttons.h"
#include "hal/input_keypad.h"

#include <lvgl.h>

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "LVGL";

/* ----- tick callback ---------------------------------------------- */

/* LVGL 9 supports a callback-driven tick — we hand it
 * esp_timer_get_time() converted to milliseconds. No periodic ISR
 * needed. */
static uint32_t lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* ----- flush callback --------------------------------------------- */

/* Each call pushes one rectangle of pixels to the panel. With
 * LV_DISPLAY_RENDER_MODE_PARTIAL the rectangle is at most the
 * partial-buffer size we configure below. */
static void lvgl_flush_cb(lv_display_t *disp,
                          const lv_area_t *area,
                          uint8_t *px_map)
{
    int x = area->x1;
    int y = area->y1;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    display_blit_rect(x, y, w, h, (const uint16_t *)px_map);
    lv_display_flush_ready(disp);
}

/* ----- buffers ---------------------------------------------------- */

/* 20-row partial buffer = 160 × 20 × 2 B = 6.4 KB. Comfortably in
 * internal SRAM; LVGL renders into this and we flush, repeat. */
#define LVGL_BUF_ROWS  20
#define LVGL_BUF_PIXELS (160 * LVGL_BUF_ROWS)
static uint16_t s_buf1[LVGL_BUF_PIXELS];
static uint16_t s_buf2[LVGL_BUF_PIXELS];

static lv_display_t *s_disp;

/* ----- input (keypad) -------------------------------------------- */

static lv_indev_t *s_indev   = NULL;
static lv_group_t *s_group   = NULL;
/* The last key we reported to LVGL. When no button is currently
 * pressed, the read callback reports this key with RELEASED state
 * so LVGL's internal state machine sees a clean press → release
 * pair for the most-recently-active key. */
static uint32_t    s_last_key = 0;

/* Map our hardware button enum (hal/buttons.h) to LVGL's standard
 * key codes. Anything not in this table is silently ignored — the
 * digit pad / star / hash become relevant only for the dialer
 * screen and we map them then.
 *
 * Button-naming reminder: hal/buttons.h's actual enum members use
 * the BTN_JOY_* and BTN_FACE_* prefixes; the bare BTN_LEFT /
 * BTN_BACK / BTN_A aliases above them are #defines layered on top
 * for the legacy Chatter app. We use the raw enum names here to
 * avoid surprises from the macro chain. */
static uint32_t btn_to_lv_key(btn_id_t btn)
{
    switch (btn) {
        case BTN_JOY_UP:    return LV_KEY_UP;
        case BTN_JOY_DOWN:  return LV_KEY_DOWN;
        case BTN_JOY_LEFT:  return LV_KEY_LEFT;
        case BTN_JOY_RIGHT: return LV_KEY_RIGHT;
        case BTN_JOY_CLICK: return LV_KEY_ENTER;
        /* Face C is the "back" button per hal/buttons.h's BTN_BACK
         * alias (#define BTN_BACK BTN_FACE_C). */
        case BTN_FACE_C:    return LV_KEY_ESC;
        case BTN_FACE_A:    return LV_KEY_NEXT;
        case BTN_FACE_B:    return LV_KEY_PREV;
        default:            return 0;
    }
}

/* LVGL keypad read callback. Called by lv_timer_handler at ~30 Hz
 * (configurable via lv_indev_set_long_press_time and friends; the
 * default rate is comfortably faster than any human can press a
 * physical button). Polls hal/input_keypad cached state — that
 * cache is maintained by the kp_input task off the XL9555 INT1/
 * INT2 ISRs, so it's always current.
 *
 * Priority order matters when multiple buttons are held at once:
 * confirm + back take precedence over navigation so a panic
 * release-and-confirm always works. Multi-button combos beyond
 * pairs are intentionally not modelled — LV_INDEV_TYPE_KEYPAD is
 * a single-key abstraction. */
static void lvgl_keypad_read_cb(lv_indev_t *indev,
                                lv_indev_data_t *data)
{
    (void)indev;

    static const btn_id_t prio[] = {
        BTN_JOY_CLICK, BTN_FACE_C,
        BTN_JOY_UP, BTN_JOY_DOWN, BTN_JOY_LEFT, BTN_JOY_RIGHT,
        BTN_FACE_A, BTN_FACE_B,
    };

    for (size_t i = 0; i < sizeof(prio) / sizeof(prio[0]); ++i) {
        if (input_keypad_is_pressed(prio[i])) {
            uint32_t k = btn_to_lv_key(prio[i]);
            if (k != 0) {
                s_last_key  = k;
                data->key   = k;
                data->state = LV_INDEV_STATE_PRESSED;
                return;
            }
        }
    }

    /* No button currently held — report the most-recently-active
     * key as RELEASED so LVGL's edge-detection sees a clean
     * transition. data->key must still carry the key so LVGL can
     * route the release to the right widget. */
    data->key   = s_last_key;
    data->state = LV_INDEV_STATE_RELEASED;
}

/* ----- public API ------------------------------------------------- */

esp_err_t lvgl_glue_init(void)
{
    static bool inited = false;
    if (inited) return ESP_OK;

    lv_init();
    lv_tick_set_cb(lvgl_tick_get_cb);

    s_disp = lv_display_create(160, 128);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "lv_display_create returned NULL");
        return ESP_FAIL;
    }
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_buffers(s_disp,
                           s_buf1, s_buf2,
                           sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* S-MP16d: keypad input device + default navigation group.
     * Creating the group here and making it default means every
     * subsequently-created focusable widget (lv_button, lv_list
     * items, lv_dropdown, etc.) auto-joins the navigation group
     * without the app needing to call lv_group_add_obj manually. */
    s_indev = lv_indev_create();
    if (s_indev == NULL) {
        ESP_LOGE(TAG, "lv_indev_create returned NULL");
        return ESP_FAIL;
    }
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_indev, lvgl_keypad_read_cb);

    s_group = lv_group_create();
    if (s_group == NULL) {
        ESP_LOGE(TAG, "lv_group_create returned NULL");
        return ESP_FAIL;
    }
    lv_group_set_default(s_group);
    lv_indev_set_group(s_indev, s_group);

    ESP_LOGI(TAG, "LVGL ready (160x128, RGB565, %d-row partial buf, "
                  "keypad indev wired)",
             LVGL_BUF_ROWS);
    inited = true;
    return ESP_OK;
}

lv_group_t *lvgl_glue_get_group(void)
{
    return s_group;
}

/* ----- task ------------------------------------------------------- */

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task starting on core %d", xPortGetCoreID());
    for (;;) {
        uint32_t next_ms = lv_timer_handler();
        if (next_ms > 100) next_ms = 100;
        if (next_ms < 5)   next_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}

void lvgl_glue_run(void)
{
    static bool spawned = false;
    if (spawned) return;
    spawned = true;
    /* 8 KB stack — LVGL widget callbacks recurse a fair bit; 4 KB is
     * the minimum quoted in LVGL docs, 8 KB gives margin. */
    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 5, NULL);
}
