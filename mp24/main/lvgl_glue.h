/*
 * mp24/main/lvgl_glue.h — LVGL ↔ MP2.4 hardware glue.
 *
 * Provides:
 *   - lvgl_glue_init()  — boots LVGL, registers our flush callback,
 *     wires the esp_timer-based tick source, allocates the partial
 *     render buffer. Returns ESP_OK on success.
 *   - lvgl_glue_run()   — spawns the FreeRTOS task that runs
 *     lv_timer_handler() on a 10 ms cadence. Call AFTER you've
 *     created your initial widgets, because once the task starts,
 *     all LVGL API calls must be made from inside that task (LVGL
 *     is not thread-safe).
 *
 * Boot sequence the caller should follow:
 *
 *     display_init();      // hal/display SPI panel up first
 *     lvgl_glue_init();    // LVGL + display registered
 *     lv_obj_t *label = lv_label_create(lv_screen_active());
 *     lv_label_set_text(label, "MP2.4");
 *     lvgl_glue_run();     // starts the LVGL task
 *
 * The flush callback uses display_blit_rect from hal/display, which
 * runs polling SPI transfers row-by-row. No DMA yet — fine for the
 * 160×128 ST7735 at the byte rates we hit.
 *
 * Color format: LV_COLOR_FORMAT_RGB565 native (host-endian). The
 * byte-swap to ST7735 wire order happens inside display_blit_rect.
 */
#pragma once

#include "esp_err.h"

#include <lvgl.h>     /* needed for lv_group_t in the prototype below */

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise LVGL, register the display + flush callback + tick
 * source. Allocates a ~20-row partial buffer in internal SRAM. Safe
 * to call exactly once. After this returns, lv_screen_active() is
 * valid and you can build widgets on it. */
esp_err_t lvgl_glue_init(void);

/* The default group all newly-created widgets are added to. Returned
 * pointer is valid after lvgl_glue_init() succeeds. Use this if you
 * want to explicitly add an object to the keypad-navigation group,
 * though by default lv_obj_create() etc. add to it automatically
 * because we call lv_group_set_default() in init. */
lv_group_t *lvgl_glue_get_group(void);

/* Start the LVGL task. After this call, all LVGL API access has to
 * happen from inside the LVGL task — use lv_async_call() or similar
 * to schedule UI changes from other contexts. */
void lvgl_glue_run(void);

#ifdef __cplusplus
}
#endif
