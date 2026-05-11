/*
 * mp24/main/app_main.c
 *
 * S-MP03 — refactor inline drivers into hal/, add XL9555 expanders,
 * wire INT1/INT2 interrupt lines.
 *
 * After display + I²C bring-up, this:
 *   - probes the bus and reports which devices answered
 *   - inits AW9523B (8 LEDs, SD_MODE → amp on)
 *   - inits both XL9555s (U5 + U9, all pins as inputs)
 *   - wires GPIO_INT1 (U5) and GPIO_INT2 (U9) as falling-edge ISRs
 *     that wake a small "drain" task. The task reads the expander's
 *     input register (which clears the interrupt) and updates the
 *     last-known input state for each chip.
 *   - drops into a forever loop that blinks the LEDs and redraws
 *     the live counters on screen.
 *
 * Session S-MP04 will replace the drain task with a proper input
 * dispatcher that emits buttonPressed/buttonReleased events using
 * the dashboard.c k_buttons[] map.
 */

#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "hal/display.h"
#include "hal/i2c_bus.h"
#include "hal/pins.h"
#include "hal/aw9523b.h"
#include "hal/xl9555.h"

static const char *TAG = "MP24";

/* ----------------------------------------------------------------- */
/* Interrupt counters + last-known expander state                    */
/* These live as atomics so the drain task and the display loop can  */
/* both touch them without explicit locking.                         */
/* ----------------------------------------------------------------- */
static atomic_uint_fast32_t s_int1_count = 0;   /* falling edges on U5 INT */
static atomic_uint_fast32_t s_int2_count = 0;   /* falling edges on U9 INT */
static atomic_uint_fast32_t s_u5_state   = 0xFFFF;   /* low byte=P0, high=P1 */
static atomic_uint_fast32_t s_u9_state   = 0xFFFF;

/* Drain task waits on this notification; ISRs xTaskNotifyFromISR it. */
static TaskHandle_t s_drain_task = NULL;

/* ISR — minimal. Just bumps a counter and pings the task. */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int which = (int)(intptr_t)arg;   /* 0 = INT1/U5, 1 = INT2/U9 */
    if (which == 0) atomic_fetch_add(&s_int1_count, 1);
    else            atomic_fetch_add(&s_int2_count, 1);

    BaseType_t need_yield = pdFALSE;
    if (s_drain_task) {
        vTaskNotifyGiveFromISR(s_drain_task, &need_yield);
    }
    portYIELD_FROM_ISR(need_yield);
}

/* User-space task — reads each expander whose INT line is asserted.
 * Reading clears the INT. Updates the cached state. */
static void drain_task(void *arg)
{
    (void)arg;
    /* Prime: read both once so the cached state matches "no buttons". */
    atomic_store(&s_u5_state, xl9555_read_inputs(I2C_ADDR_XL9555_U5));
    atomic_store(&s_u9_state, xl9555_read_inputs(I2C_ADDR_XL9555_U9));

    for (;;) {
        /* Wait for an ISR ping (or 100 ms timeout — periodic poll
         * as a safety net in case we ever miss an edge). */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        if (gpio_get_level(PIN_INT_U5) == 0) {
            uint16_t v = xl9555_read_inputs(I2C_ADDR_XL9555_U5);
            atomic_store(&s_u5_state, v);
        }
        if (gpio_get_level(PIN_INT_U9) == 0) {
            uint16_t v = xl9555_read_inputs(I2C_ADDR_XL9555_U9);
            atomic_store(&s_u9_state, v);
        }
    }
}

static esp_err_t setup_keypad_interrupts(void)
{
    /* Both INT lines: input + pull-up (XL9555 INT is open-drain) */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_INT_U5) | (1ULL << PIN_INT_U9),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    esp_err_t r = gpio_config(&io);
    if (r != ESP_OK) return r;

    /* Spawn the drain task BEFORE installing the ISR — otherwise an
     * ISR firing in the gap would notify a null TaskHandle. */
    BaseType_t ok = xTaskCreate(drain_task, "kp_drain", 4096, NULL,
                                tskIDLE_PRIORITY + 5, &s_drain_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "drain task spawn failed");
        return ESP_FAIL;
    }

    /* Install the per-pin ISR service (idempotent flag = 0). */
    r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) return r;
    r = gpio_isr_handler_add(PIN_INT_U5, gpio_isr_handler, (void*)(intptr_t)0);
    if (r != ESP_OK) return r;
    r = gpio_isr_handler_add(PIN_INT_U9, gpio_isr_handler, (void*)(intptr_t)1);
    if (r != ESP_OK) return r;

    ESP_LOGI(TAG, "keypad interrupts wired (INT1=GPIO%d INT2=GPIO%d)",
             PIN_INT_U5, PIN_INT_U9);
    return ESP_OK;
}

/* ----------------------------------------------------------------- */
/* USB CDC boot banner                                               */
/* ----------------------------------------------------------------- */

static void print_banner(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, " MAKERphone v2.4 firmware — S-MP03 I²C HAL");
    ESP_LOGI(TAG, " IDF=%s  chip=%s rev%d  cores=%d",
             esp_get_idf_version(),
             (info.model == CHIP_ESP32S3) ? "ESP32-S3" : "?",
             info.revision, info.cores);
    ESP_LOGI(TAG, " free heap = %lu B   PSRAM target = 2 MB octal",
             (unsigned long) esp_get_free_heap_size());
    ESP_LOGI(TAG, "==========================================");
}

/* ----------------------------------------------------------------- */
/* Static boot screen + dynamic update                               */
/* The labels are drawn once; only the value strips repaint per      */
/* frame so we don't flicker the whole screen.                       */
/* ----------------------------------------------------------------- */

static uint16_t MP_BG     = 0;
static uint16_t MP_ACCENT = 0;
static uint16_t MP_TEXT   = 0;
static uint16_t MP_DIM    = 0;

static void draw_boot_screen(void)
{
    display_fill(MP_BG);

    display_fill_rect(0, 0, TFT_WIDTH, 14, MP_ACCENT);
    display_str(4, 4, "MAKERphone v2.4", MP_BG, MP_ACCENT);

    display_str(4, 22, "S-MP03  i2c HAL OK",      MP_TEXT, MP_BG);
    display_str(4, 34, "U5 U9 AW9523B wired",     MP_DIM,  MP_BG);

    /* Chroma stripe stays — visual sanity check at every boot. */
    const int bar_y = 50;
    const int bar_h = 8;
    const int bar_w = TFT_WIDTH / 4;
    display_fill_rect(0 * bar_w, bar_y, bar_w, bar_h, COLOR_RED);
    display_fill_rect(1 * bar_w, bar_y, bar_w, bar_h, COLOR_GREEN);
    display_fill_rect(2 * bar_w, bar_y, bar_w, bar_h, COLOR_BLUE);
    display_fill_rect(3 * bar_w, bar_y, bar_w, bar_h, COLOR_WHITE);

    /* Labels for the live values updated each frame. */
    display_str(4, 66,  "INT1 U5 :",  MP_DIM, MP_BG);
    display_str(4, 80,  "INT2 U9 :",  MP_DIM, MP_BG);
    display_str(4, 96,  "U5 raw  :",  MP_DIM, MP_BG);
    display_str(4, 110, "U9 raw  :",  MP_DIM, MP_BG);
}

static void redraw_value(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    /* Erase a fixed strip so changing widths don't leave artefacts. */
    display_fill_rect(x, y, 90, 7, bg);
    display_str(x, y, s, fg, bg);
}

static void update_dashboard(void)
{
    char buf[24];
    uint32_t i1 = (uint32_t) atomic_load(&s_int1_count);
    uint32_t i2 = (uint32_t) atomic_load(&s_int2_count);
    uint16_t u5 = (uint16_t) atomic_load(&s_u5_state);
    uint16_t u9 = (uint16_t) atomic_load(&s_u9_state);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long) i1);
    redraw_value(66, 66,  buf, MP_TEXT, MP_BG);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long) i2);
    redraw_value(66, 80,  buf, MP_TEXT, MP_BG);

    snprintf(buf, sizeof(buf), "0x%04X", u5);
    redraw_value(66, 96,  buf, MP_TEXT, MP_BG);

    snprintf(buf, sizeof(buf), "0x%04X", u9);
    redraw_value(66, 110, buf, MP_TEXT, MP_BG);
}

/* ----------------------------------------------------------------- */

void app_main(void)
{
    print_banner();

    /* Bring up display first so any later step's failure is visible. */
    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed — continuing headless");
    } else {
        MP_BG     = display_rgb( 20,  12,  36);
        MP_ACCENT = display_rgb(255, 140,  30);
        MP_TEXT   = display_rgb(255, 220, 180);
        MP_DIM    = display_rgb(170, 140, 200);
        draw_boot_screen();
    }

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "I²C init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* Bus inventory — cheap (3 probes), good signal at bring-up. */
    ESP_LOGI(TAG, "bus probe: U5=%s U9=%s AW9523B=%s",
             i2c_bus_probe(I2C_ADDR_XL9555_U5) ? "OK" : "MISSING",
             i2c_bus_probe(I2C_ADDR_XL9555_U9) ? "OK" : "MISSING",
             i2c_bus_probe(I2C_ADDR_AW9523B)   ? "OK" : "MISSING");

    if (aw9523b_init() != ESP_OK) {
        ESP_LOGE(TAG, "AW9523B init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* XL9555 init is non-fatal — one expander gone, the rest of the
     * firmware still comes up so the user can read the status on
     * screen. */
    if (xl9555_init(I2C_ADDR_XL9555_U5) != ESP_OK) {
        ESP_LOGW(TAG, "XL9555 U5 init failed (continuing)");
    }
    if (xl9555_init(I2C_ADDR_XL9555_U9) != ESP_OK) {
        ESP_LOGW(TAG, "XL9555 U9 init failed (continuing)");
    }

    if (setup_keypad_interrupts() != ESP_OK) {
        ESP_LOGW(TAG, "Keypad interrupts not wired (continuing)");
    }

    ESP_LOGI(TAG, "Entering live dashboard loop.");
    bool led_on = false;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        led_on = !led_on;
        aw9523b_set_leds(led_on);
        update_dashboard();
        /* ~5 fps for the screen; LEDs visually blink at 2.5 Hz. */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(200));
    }
}
