/*
 * mp24/main/lvgl_glue.c — see header.
 */

#include "lvgl_glue.h"
#include "hal/display.h"

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

    ESP_LOGI(TAG, "LVGL ready (160x128, RGB565, %d-row partial buf)",
             LVGL_BUF_ROWS);
    inited = true;
    return ESP_OK;
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
