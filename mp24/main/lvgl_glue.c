/*
 * mp24/main/lvgl_glue.c — LVGL 8 ↔ MP2.4 hardware glue.
 *
 * Note (S-MP16 history): an earlier revision of this file targeted
 * LVGL 9.5 — lv_display_create + lv_indev_create + lv_tick_set_cb.
 * It worked on hardware. We then started porting the legacy 506-
 * file phone firmware, which is written against LVGL 8 (different
 * indev / event APIs, different layout + style internals). Rather
 * than patch every screen .cpp for 9-style APIs, the call was to
 * downgrade our glue to LVGL 8.4 and keep the upstream code intact.
 *
 * Same end-to-end shape as the 9-flavoured version: a flush
 * callback that pushes pixels via hal/display, a 1 kHz tick via
 * esp_timer, a keypad-style indev that polls hal/input_keypad,
 * and a FreeRTOS task driving lv_timer_handler.
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

/* ----- tick source ------------------------------------------------- */

/* LVGL 8 needs lv_tick_inc(ms) called periodically. The cleanest way
 * to do that under ESP-IDF is a 1 ms esp_timer — fires from the
 * esp_timer task (not an ISR), which makes the lv_tick_inc call
 * cheap and safe. */
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static esp_timer_handle_t s_tick_timer;

/* ----- flush callback --------------------------------------------- */

static void lvgl_flush_cb(lv_disp_drv_t *drv,
                          const lv_area_t *area,
                          lv_color_t *color_p)
{
    int x = area->x1;
    int y = area->y1;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    /* With LV_COLOR_DEPTH=16 (LVGL 8 default) and LV_COLOR_16_SWAP=0,
     * lv_color_t has the same memory layout as uint16_t in host
     * endian — display_blit_rect does the big-endian swap to ST7735
     * wire format per row. */
    display_blit_rect(x, y, w, h, (const uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

/* ----- buffers ---------------------------------------------------- */

/* 20-row partial buffer = 160 × 20 × 2 B = 6.4 KB each.
 * Comfortably in internal SRAM; LVGL renders into one while the
 * other is flushing (double-buffer) for tear-free updates. */
#define LVGL_BUF_ROWS  20
#define LVGL_BUF_PIXELS (160 * LVGL_BUF_ROWS)
static lv_color_t            s_buf1[LVGL_BUF_PIXELS];
static lv_color_t            s_buf2[LVGL_BUF_PIXELS];
static lv_disp_draw_buf_t    s_draw_buf;
static lv_disp_drv_t         s_disp_drv;
static lv_disp_t            *s_disp;

/* ----- input (keypad) -------------------------------------------- */

static lv_indev_drv_t  s_indev_drv;
static lv_indev_t     *s_indev   = NULL;
static lv_group_t     *s_group   = NULL;
/* Last key we reported to LVGL. When no button is currently pressed
 * the read callback reports this key with RELEASED state so LVGL
 * sees a clean press→release for the most-recently-active key. */
static uint32_t        s_last_key = 0;

/* Map our hardware button enum (hal/buttons.h) to LVGL key codes.
 *
 * LVGL group navigation semantics (same in 8 and 9): lv_group_t
 * responds to LV_KEY_NEXT / PREV; UP / DOWN / LEFT / RIGHT go to
 * the focused widget. So vertical joystick motion has to map to
 * NEXT / PREV for menu nav to work. */
static uint32_t btn_to_lv_key(btn_id_t btn)
{
    switch (btn) {
        case BTN_JOY_UP:    return LV_KEY_PREV;
        case BTN_JOY_DOWN:  return LV_KEY_NEXT;
        case BTN_JOY_LEFT:  return LV_KEY_LEFT;
        case BTN_JOY_RIGHT: return LV_KEY_RIGHT;
        case BTN_JOY_CLICK: return LV_KEY_ENTER;
        case BTN_FACE_C:    return LV_KEY_ESC;
        default:            return 0;
    }
}

/* LVGL 8 keypad read callback. Signature takes lv_indev_drv_t* not
 * lv_indev_t* — the 8→9 rename. */
static void lvgl_keypad_read_cb(lv_indev_drv_t *drv,
                                lv_indev_data_t *data)
{
    (void)drv;

    static const btn_id_t prio[] = {
        BTN_JOY_CLICK, BTN_FACE_C,
        BTN_JOY_UP, BTN_JOY_DOWN, BTN_JOY_LEFT, BTN_JOY_RIGHT,
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
    data->key   = s_last_key;
    data->state = LV_INDEV_STATE_RELEASED;
}

/* ----- public API ------------------------------------------------- */

esp_err_t lvgl_glue_init(void)
{
    static bool inited = false;
    if (inited) return ESP_OK;

    lv_init();

    /* Tick: 1 ms periodic esp_timer drives lv_tick_inc. Created
     * + started here, never stopped. */
    const esp_timer_create_args_t targs = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_err_t err = esp_timer_create(&targs, &s_tick_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }
    esp_timer_start_periodic(s_tick_timer, 1000 /* 1 ms */);

    /* Display: draw_buf + drv pattern. */
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, LVGL_BUF_PIXELS);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = 160;
    s_disp_drv.ver_res  = 128;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp = lv_disp_drv_register(&s_disp_drv);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "lv_disp_drv_register returned NULL");
        return ESP_FAIL;
    }

    /* Input: keypad indev + default group. */
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_KEYPAD;
    s_indev_drv.read_cb = lvgl_keypad_read_cb;
    s_indev = lv_indev_drv_register(&s_indev_drv);
    if (s_indev == NULL) {
        ESP_LOGE(TAG, "lv_indev_drv_register returned NULL");
        return ESP_FAIL;
    }

    s_group = lv_group_create();
    if (s_group == NULL) {
        ESP_LOGE(TAG, "lv_group_create returned NULL");
        return ESP_FAIL;
    }
    lv_group_set_default(s_group);
    lv_indev_set_group(s_indev, s_group);

    ESP_LOGI(TAG, "LVGL 8 ready (160x128, RGB565, %d-row partial buf, "
                  "keypad indev wired)",
             LVGL_BUF_ROWS);
    inited = true;
    return ESP_OK;
}

lv_group_t *lvgl_glue_get_group(void)
{
    return s_group;
}

lv_indev_t *lvgl_glue_get_indev(void)
{
    return s_indev;
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
