/*
 * mp24/main/hal/input_keypad.c — XL9555 keypad event dispatcher.
 *
 * Bit map lifted verbatim from /mnt/project/dashboard.c (the
 * hardware-verified table — see MP24_PORT_PLAN.md §7 for why the
 * schematic-derived map in pin_config.h's header comments is wrong).
 *
 * Two threads of execution:
 *
 *   ISR (IRAM, never blocks):
 *     - on falling edge of GPIO_INT1 or INT2, sends a task
 *       notification to the dispatcher.
 *
 *   Dispatcher task (kp_input, ulTaskNotifyTake on 100 ms timeout):
 *     - re-reads whichever expander's INT line is low (clearing
 *       the INT). If we missed an edge (e.g. bounce), the 100 ms
 *       timeout polls both expanders periodically as a safety net.
 *     - XORs against last-known state; for each bit that flipped,
 *       finds the matching btn_id_t in k_btn_map and fires every
 *       registered listener's callback.
 */

#include "hal/input_keypad.h"
#include "hal/buttons.h"
#include "hal/i2c_bus.h"
#include "hal/pins.h"
#include "hal/xl9555.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"

#include <stdatomic.h>

static const char *TAG = "INPUT";

/* ----------------------------------------------------------------- */
/* Bit map — index by btn_id_t. Source of truth:                     */
/* /mnt/project/dashboard.c k_buttons[] (hardware-verified 2025-05).  */
/* expander: 0 = U5 @ 0x20, 1 = U9 @ 0x21                            */
/* port: 0 or 1 (low byte / high byte of the 16-bit input register)  */
/* bit: 0..7, active LOW (pressed = bit reads 0)                     */
/* ----------------------------------------------------------------- */
typedef struct {
    uint8_t     expander;
    uint8_t     port;
    uint8_t     bit;
    const char *name;
} btn_map_entry_t;

#define U5 0
#define U9 1

static const btn_map_entry_t k_btn_map[BTN_COUNT] = {
    [BTN_0]         = { U5, 0, 6, "0"   },
    [BTN_1]         = { U5, 1, 4, "1"   },
    [BTN_2]         = { U5, 0, 0, "2"   },
    [BTN_3]         = { U5, 0, 1, "3"   },
    [BTN_4]         = { U5, 1, 2, "4"   },
    [BTN_5]         = { U5, 0, 7, "5"   },
    [BTN_6]         = { U5, 0, 2, "6"   },
    [BTN_7]         = { U5, 1, 1, "7"   },
    [BTN_8]         = { U5, 0, 5, "8"   },
    [BTN_9]         = { U5, 0, 3, "9"   },
    [BTN_STAR]      = { U5, 1, 0, "*"   },
    [BTN_HASH]      = { U5, 0, 4, "#"   },
    [BTN_FACE_A]    = { U5, 1, 6, "A"   },
    [BTN_FACE_B]    = { U5, 1, 5, "B"   },
    [BTN_FACE_C]    = { U9, 0, 5, "C"   },
    [BTN_FACE_D]    = { U9, 0, 6, "D"   },
    [BTN_JOY_UP]    = { U9, 0, 0, "JUP" },
    [BTN_JOY_DOWN]  = { U9, 1, 3, "JDN" },
    [BTN_JOY_LEFT]  = { U9, 1, 4, "JLT" },
    [BTN_JOY_RIGHT] = { U9, 0, 2, "JRT" },
    [BTN_JOY_CLICK] = { U9, 0, 1, "JCK" },
    [BTN_VOL_UP]    = { U9, 0, 3, "VUP" },
    [BTN_VOL_DOWN]  = { U9, 0, 4, "VDN" },
};

const char *btn_name(btn_id_t b)
{
    if ((unsigned)b >= BTN_COUNT) return "?";
    return k_btn_map[b].name;
}

/* ----------------------------------------------------------------- */
/* State                                                             */
/* ----------------------------------------------------------------- */

/* Listener chain. Mutated only at startup (before init); read by
 * the dispatcher task. Atomic head pointer keeps the dispatcher
 * race-free if someone *did* try to register late. */
static input_listener_t *_Atomic s_listeners = NULL;

/* Latest cached state — exposed for input_keypad_is_pressed(). */
static atomic_uint_fast32_t s_u5_state = 0xFFFF;
static atomic_uint_fast32_t s_u9_state = 0xFFFF;

static TaskHandle_t s_dispatch_task = NULL;
static bool s_inited = false;

/* ----------------------------------------------------------------- */
/* ISR — must stay short. Just notifies the dispatcher.              */
/* ----------------------------------------------------------------- */

static void IRAM_ATTR gpio_isr(void *arg)
{
    (void)arg;
    BaseType_t need_yield = pdFALSE;
    if (s_dispatch_task) {
        vTaskNotifyGiveFromISR(s_dispatch_task, &need_yield);
    }
    portYIELD_FROM_ISR(need_yield);
}

/* ----------------------------------------------------------------- */
/* Dispatch one expander's changed bits.                              */
/* ----------------------------------------------------------------- */

static void dispatch_changes(uint8_t chip,
                             uint16_t old_state, uint16_t new_state)
{
    if (old_state == new_state) return;

    /* Walk only the buttons that live on this chip. Cost: ~14 iters
     * for U5, ~9 for U9 — fine for a UI loop. */
    for (btn_id_t b = (btn_id_t)0; b < BTN_COUNT; b++) {
        const btn_map_entry_t *e = &k_btn_map[b];
        if (e->expander != chip) continue;

        uint8_t mask = (uint8_t)(1u << e->bit);
        uint8_t old_byte = (e->port == 0) ? (old_state & 0xFF) : (old_state >> 8);
        uint8_t new_byte = (e->port == 0) ? (new_state & 0xFF) : (new_state >> 8);

        if ((old_byte & mask) == (new_byte & mask)) continue;

        bool pressed = !(new_byte & mask);   /* active LOW */

        ESP_LOGD(TAG, "%s %s",
                 pressed ? "PRESS  " : "release", e->name);

        /* Fan-out to listeners. Snapshot the head atomically; the
         * chain itself is append-only once init has run. */
        input_listener_t *l = atomic_load(&s_listeners);
        while (l) {
            if (l->cb) l->cb(b, pressed, l->ctx);
            l = l->next;
        }
    }
}

/* ----------------------------------------------------------------- */
/* Dispatcher task                                                   */
/* ----------------------------------------------------------------- */

static void dispatch_task(void *arg)
{
    (void)arg;
    /* Prime cached state to "all released" before the first event. */
    uint16_t u5_state = xl9555_read_inputs(I2C_ADDR_XL9555_U5);
    uint16_t u9_state = xl9555_read_inputs(I2C_ADDR_XL9555_U9);
    atomic_store(&s_u5_state, u5_state);
    atomic_store(&s_u9_state, u9_state);

    for (;;) {
        /* Wait for an ISR ping. The 100 ms fallback polls both
         * expanders even if we somehow miss an edge (the XL9555 INT
         * doesn't latch — once both port registers match the
         * last-read values, INT goes HIGH whether or not we observed
         * the interim transition). */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        bool int1_low = (gpio_get_level(PIN_INT_U5) == 0);
        bool int2_low = (gpio_get_level(PIN_INT_U9) == 0);

        if (int1_low) {
            uint16_t v = xl9555_read_inputs(I2C_ADDR_XL9555_U5);
            dispatch_changes(U5, u5_state, v);
            u5_state = v;
            atomic_store(&s_u5_state, v);
        }
        if (int2_low) {
            uint16_t v = xl9555_read_inputs(I2C_ADDR_XL9555_U9);
            dispatch_changes(U9, u9_state, v);
            u9_state = v;
            atomic_store(&s_u9_state, v);
        }
    }
}

/* ----------------------------------------------------------------- */
/* Public API                                                        */
/* ----------------------------------------------------------------- */

void input_keypad_add_listener(input_listener_t *l)
{
    if (!l) return;
    /* Push onto the head of the chain atomically. */
    input_listener_t *head;
    do {
        head = atomic_load(&s_listeners);
        l->next = head;
    } while (!atomic_compare_exchange_weak(&s_listeners, &head, l));
}

esp_err_t input_keypad_init(void)
{
    if (s_inited) return ESP_OK;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_INT_U5) | (1ULL << PIN_INT_U9),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    esp_err_t r = gpio_config(&io);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %d", r);
        return r;
    }

    BaseType_t ok = xTaskCreate(dispatch_task, "kp_input", 4096, NULL,
                                tskIDLE_PRIORITY + 5, &s_dispatch_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "dispatch task spawn failed");
        return ESP_FAIL;
    }

    r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "isr_service_install failed: %d", r);
        return r;
    }
    r = gpio_isr_handler_add(PIN_INT_U5, gpio_isr, NULL);
    if (r != ESP_OK) return r;
    r = gpio_isr_handler_add(PIN_INT_U9, gpio_isr, NULL);
    if (r != ESP_OK) return r;

    s_inited = true;
    ESP_LOGI(TAG, "ready (INT1=GPIO%d INT2=GPIO%d, %d btns)",
             PIN_INT_U5, PIN_INT_U9, (int)BTN_COUNT);
    return ESP_OK;
}

bool input_keypad_is_pressed(btn_id_t b)
{
    if ((unsigned)b >= BTN_COUNT) return false;
    const btn_map_entry_t *e = &k_btn_map[b];
    uint16_t st = (uint16_t)atomic_load(
        e->expander == U5 ? &s_u5_state : &s_u9_state);
    uint8_t byte = (e->port == 0) ? (st & 0xFF) : (st >> 8);
    return !(byte & (1u << e->bit));   /* active LOW */
}

uint16_t input_keypad_raw(uint8_t chip)
{
    return (uint16_t) atomic_load(chip == U5 ? &s_u5_state : &s_u9_state);
}
