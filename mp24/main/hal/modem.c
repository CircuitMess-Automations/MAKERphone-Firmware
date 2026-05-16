/*
 * mp24/main/hal/modem.c — Quectel EG912U-GL UART driver, AT
 * command framework, URC dispatcher, power-on state machine.
 *
 * Two background tasks:
 *   modem_rx   reads raw bytes from UART, splits on CRLF, classifies
 *              each line as URC vs AT response vs OK/ERROR terminator.
 *              Posts AT response lines to s_at_q. Calls URC cb.
 *   modem_sm   power-on state machine. Drives PWR_KEY, sends AT
 *              probes until a response arrives (or 30 s timeout),
 *              then sends ATE0 + AT+CGMM to identify the module.
 *
 * modem_at_send() is the only public way to talk to the modem. It
 * acquires s_at_mutex, writes the command, waits on s_at_q for
 * OK/ERROR, releases mutex. Caller from any non-ISR context.
 */

#include "hal/modem.h"
#include "hal/pins.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "MODEM";

/* ============================================================== */
/* State                                                          */
/* ============================================================== */

static _Atomic int  s_state         = MODEM_OFF;
static bool         s_inited        = false;
static char         s_model[32]     = "";

/* URC subscriptions. Subscribers are matched by prefix against each
 * arriving URC line; a subscriber with prefix="" sees every URC. The
 * array is small + static — registration is rare (during init) and
 * dispatch is hot (once per URC). */
#define MAX_URC_SUBS  8
typedef struct {
    char           prefix[24];
    modem_urc_cb_t cb;
} urc_sub_t;
static urc_sub_t s_urc_subs[MAX_URC_SUBS] = {0};
static int       s_urc_sub_count          = 0;

/* When non-zero, the RX task drops UART reads — used by
 * modem_at_send_data() during the interactive-prompt phase so the
 * sender can poll UART directly for a non-CRLF '>' character. */
static volatile bool s_rx_paused = false;

/* Mutual exclusion for the AT command channel. Only one task may be
 * inside modem_at_send() at a time. */
static SemaphoreHandle_t s_at_mutex = NULL;

/* Queue carrying response lines from the RX task to whichever task
 * is currently inside modem_at_send(). Each queue entry is a single
 * line (heap-allocated, NUL-terminated; receiver frees). */
static QueueHandle_t s_at_q         = NULL;

/* UART RX scratch buffer — 1 KB is plenty for a single response. */
#define MODEM_UART_RX_BUF   2048
#define MODEM_LINE_MAX       512

/* ============================================================== */
/* GPIO + UART setup                                              */
/* ============================================================== */

static esp_err_t init_gpios(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_MODEM_PWR_KEY) |
                        (1ULL << PIN_MODEM_RESET_N),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t r = gpio_config(&out_cfg);
    if (r != ESP_OK) return r;

    /* Idle states per Quectel EG912U hardware design:
     *   PWR_KEY high  — module sees pin un-asserted (not "pressed").
     *                   Active-low pin in VBAT power domain.
     *   RESET_N high  — out of reset (also active-low).
     * Holding PWR_KEY high here means the modem stays off until
     * pulse_pwr_key() pulls it low for the boot trigger. */
    gpio_set_level(PIN_MODEM_PWR_KEY, 1);
    gpio_set_level(PIN_MODEM_RESET_N, 1);
    return ESP_OK;
}

static esp_err_t init_uart(void)
{
    uart_config_t cfg = {
        .baud_rate  = MODEM_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t r = uart_driver_install(MODEM_UART_PORT,
                                      MODEM_UART_RX_BUF, 0, 0, NULL, 0);
    if (r != ESP_OK) return r;
    r = uart_param_config(MODEM_UART_PORT, &cfg);
    if (r != ESP_OK) return r;
    r = uart_set_pin(MODEM_UART_PORT,
                     PIN_MODEM_UART_TX, PIN_MODEM_UART_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return r;
}

/* ============================================================== */
/* RX task                                                        */
/* ============================================================== */

/* Classify a line into one of three buckets. Returns true if the
 * line is a URC (unsolicited). OK/ERROR/+CME ERROR aren't URCs;
 * they're response terminators. */
static bool is_urc(const char *line)
{
    /* Quectel URCs are prefixed with '+' or are one of these
     * standalone tokens. AT command echoes also start with '+',
     * but we disable echo via ATE0 so this is unambiguous. The
     * RING, NO CARRIER, etc. set covers voice-call URCs. */
    static const char *urc_keywords[] = {
        "RING", "NO CARRIER", "NO ANSWER", "BUSY", "CONNECT",
        "+CMTI", "+CMT:", "+CLIP", "+CRING", "+CREG", "+CGREG",
        "+CEREG", "+QIURC", "+QIND", "+CGEV", "+QSIMSTAT",
        NULL,
    };
    for (int i = 0; urc_keywords[i]; i++) {
        if (strncmp(line, urc_keywords[i], strlen(urc_keywords[i])) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_terminator(const char *line)
{
    return strcmp(line, "OK") == 0
        || strcmp(line, "ERROR") == 0
        || strncmp(line, "+CME ERROR", 10) == 0
        || strncmp(line, "+CMS ERROR", 10) == 0;
}

/* Heap-allocate a line copy and push it onto s_at_q. If the queue
 * is full, log + drop (better than blocking the RX path). */
static void enqueue_line(const char *line, size_t len)
{
    char *copy = (char *) malloc(len + 1);
    if (!copy) return;
    memcpy(copy, line, len);
    copy[len] = '\0';
    if (xQueueSend(s_at_q, &copy, 0) != pdTRUE) {
        ESP_LOGW(TAG, "AT queue full, dropping: %s", copy);
        free(copy);
    }
}

static void dispatch_urc(const char *line)
{
    for (int i = 0; i < s_urc_sub_count; i++) {
        const char *p = s_urc_subs[i].prefix;
        if (p[0] == '\0' || strncmp(line, p, strlen(p)) == 0) {
            s_urc_subs[i].cb(line);
        }
    }
}

static void modem_rx_task(void *arg)
{
    (void) arg;
    char line[MODEM_LINE_MAX];
    size_t line_len = 0;

    for (;;) {
        /* Pause window: modem_at_send_data() is doing its own UART
         * polling for the '>' prompt — yielding the bus avoids
         * race-eating the prompt byte. Resumes on its own. */
        if (s_rx_paused) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint8_t byte;
        int n = uart_read_bytes(MODEM_UART_PORT, &byte, 1,
                                pdMS_TO_TICKS(1000));
        if (n <= 0) continue;

        /* Split on LF — modem terminates each line with CRLF, we
         * strip both. Allow lines slightly over MAX by truncating. */
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (line_len == 0) continue;          /* blank line filler */
            line[line_len] = '\0';

            if (is_urc(line)) {
                ESP_LOGI(TAG, "URC: %s", line);
                dispatch_urc(line);
            } else {
                /* Either an intermediate response line, an echo, or
                 * a terminator. Push it all to the AT queue; the
                 * sender decides what to do with it. */
                enqueue_line(line, line_len);
            }
            line_len = 0;
        } else {
            if (line_len < MODEM_LINE_MAX - 1) {
                line[line_len++] = (char) byte;
            }
        }
    }
}

/* ============================================================== */
/* AT command send (synchronous)                                  */
/* ============================================================== */

esp_err_t modem_at_send(const char *cmd,
                        char *resp_buf, size_t buf_len,
                        uint32_t timeout_ms)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    modem_state_t st = (modem_state_t) atomic_load(&s_state);
    if (st != MODEM_READY && st != MODEM_BOOTING) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_at_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Drain any leftover response lines from a previous timed-out
     * call so we don't see stale data. */
    char *junk = NULL;
    while (xQueueReceive(s_at_q, &junk, 0) == pdTRUE) {
        free(junk);
    }

    /* Build "AT<cmd>\r\n" and send. Empty cmd is allowed — "AT\r\n"
     * is the canonical liveness probe. */
    char tx[MODEM_LINE_MAX];
    int tlen = snprintf(tx, sizeof(tx), "AT%s\r\n", cmd ? cmd : "");
    if (tlen <= 0 || tlen >= (int) sizeof(tx)) {
        xSemaphoreGive(s_at_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    uart_write_bytes(MODEM_UART_PORT, tx, tlen);

    /* Collect lines until we see OK/ERROR or run out of time. */
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    size_t resp_len = 0;
    if (resp_buf && buf_len > 0) resp_buf[0] = '\0';

    esp_err_t result = ESP_ERR_TIMEOUT;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) break;
        char *line = NULL;
        if (xQueueReceive(s_at_q, &line, deadline - now) != pdTRUE) {
            break;
        }

        /* Skip echo of our own command if echo wasn't disabled yet. */
        bool is_echo = (strncmp(line, "AT", 2) == 0 &&
                        strstr(line, cmd ? cmd : "") != NULL);

        if (is_terminator(line)) {
            if (strcmp(line, "OK") == 0) result = ESP_OK;
            else                          result = ESP_FAIL;
            free(line);
            break;
        }

        if (!is_echo && resp_buf && buf_len > 0) {
            size_t ll = strlen(line);
            if (resp_len + ll + 2 < buf_len) {
                if (resp_len > 0) resp_buf[resp_len++] = '\n';
                memcpy(resp_buf + resp_len, line, ll);
                resp_len += ll;
                resp_buf[resp_len] = '\0';
            }
        }
        free(line);
    }

    xSemaphoreGive(s_at_mutex);
    return result;
}

/* ============================================================== */
/* Power-on state machine                                         */
/* ============================================================== */

static void set_state(modem_state_t s)
{
    atomic_store(&s_state, (int) s);
    ESP_LOGI(TAG, "state -> %s", modem_state_name(s));
}

static void pulse_pwr_key(uint32_t ms)
{
    /* Active-low pulse: pull PWR_KEY to ground for `ms`, then
     * release back to its idle-high state. Quectel datasheet
     * (EG912U-GL Hardware Design V1.1, §3.4.1) requires ≥2 s low
     * for power-on; we pass 2500 ms from the caller for margin. */
    gpio_set_level(PIN_MODEM_PWR_KEY, 0);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(PIN_MODEM_PWR_KEY, 1);
}

static void modem_sm_task(void *arg)
{
    (void) arg;

    /* Let the modem stabilise from reset deassertion. */
    vTaskDelay(pdMS_TO_TICKS(200));

    set_state(MODEM_POWERING_ON);

    /* Quectel EG912U-GL power-on: PWR_KEY pulled low for ≥2 s.
     * (Datasheet V1.1 §3.4.1; the older revision required ≥500 ms
     * which is what an earlier comment here was citing. The 2 s
     * floor is the safe number for production silicon.) We use
     * 2500 ms for jitter margin against supply ramp. */
    pulse_pwr_key(2500);

    set_state(MODEM_BOOTING);

    /* Probe for AT response. Modem typically takes 8-15 s to come
     * up; we wait up to 30 s. Sending "AT" every 500 ms gives clear
     * boot-progress logging without flooding. */
    const TickType_t start = xTaskGetTickCount();
    const TickType_t cap   = start + pdMS_TO_TICKS(30000);
    bool got_response = false;
    while (xTaskGetTickCount() < cap) {
        /* Direct UART write — can't use modem_at_send yet because
         * the mutex would be held by ourselves and other tasks
         * might race in. Probe is fire-and-forget; we look at the
         * AT queue manually. */
        const char probe[] = "AT\r\n";
        uart_write_bytes(MODEM_UART_PORT, probe, sizeof(probe) - 1);

        TickType_t wait_until = xTaskGetTickCount() + pdMS_TO_TICKS(500);
        char *line = NULL;
        while (xTaskGetTickCount() < wait_until) {
            TickType_t left = wait_until - xTaskGetTickCount();
            if (xQueueReceive(s_at_q, &line, left) == pdTRUE) {
                bool ok = (strcmp(line, "OK") == 0);
                free(line);
                if (ok) { got_response = true; break; }
            }
        }
        if (got_response) break;

        ESP_LOGI(TAG, "boot probe... (%lu s elapsed)",
                 (unsigned long)((xTaskGetTickCount() - start) /
                                 configTICK_RATE_HZ));
    }

    if (!got_response) {
        ESP_LOGE(TAG, "no response in 30 s — modem absent or unpowered");
        set_state(MODEM_FAILED);
        vTaskDelete(NULL);
        return;
    }

    set_state(MODEM_READY);
    ESP_LOGI(TAG, "alive — disabling echo + querying identity");

    /* Now we can use modem_at_send. Disable echo so subsequent
     * responses don't include the command line. */
    modem_at_send("E0", NULL, 0, 1000);

    /* Query module name. Quectel returns one info line then OK. */
    char resp[64];
    if (modem_at_send("+CGMM", resp, sizeof(resp), 2000) == ESP_OK) {
        strncpy(s_model, resp, sizeof(s_model) - 1);
        ESP_LOGI(TAG, "model: %s", s_model);
    } else {
        ESP_LOGW(TAG, "AT+CGMM failed (continuing anyway)");
    }

    /* Sit idle from here — S-MP09 will spawn additional logic on top
     * (SIM unlock, network registration, SMS handling). */
    vTaskDelete(NULL);
}

/* ============================================================== */
/* Public API                                                     */
/* ============================================================== */

const char *modem_state_name(modem_state_t s)
{
    switch (s) {
    case MODEM_OFF:          return "OFF";
    case MODEM_POWERING_ON:  return "POWER";
    case MODEM_BOOTING:      return "BOOT";
    case MODEM_READY:        return "READY";
    case MODEM_FAILED:       return "FAIL";
    }
    return "?";
}

modem_state_t modem_state(void)
{
    return (modem_state_t) atomic_load(&s_state);
}

const char *modem_model(void)
{
    return s_model;
}

esp_err_t modem_subscribe_urc(const char *prefix, modem_urc_cb_t cb)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    if (s_urc_sub_count >= MAX_URC_SUBS) {
        ESP_LOGW(TAG, "URC sub table full, dropping registration for '%s'",
                 prefix ? prefix : "");
        return ESP_ERR_NO_MEM;
    }
    const char *p = prefix ? prefix : "";
    size_t pl = strlen(p);
    if (pl >= sizeof(s_urc_subs[0].prefix)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_urc_subs[s_urc_sub_count].prefix, p, pl + 1);
    s_urc_subs[s_urc_sub_count].cb = cb;
    s_urc_sub_count++;
    return ESP_OK;
}

void modem_set_urc_cb(modem_urc_cb_t cb)
{
    /* Backward-compat shim: a catch-all subscriber. */
    modem_subscribe_urc("", cb);
}

esp_err_t modem_at_send_data(const char *cmd,
                             char prompt_char,
                             const void *data, size_t data_len,
                             char *resp_buf, size_t buf_len,
                             uint32_t prompt_timeout_ms,
                             uint32_t response_timeout_ms)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if ((modem_state_t) atomic_load(&s_state) != MODEM_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!cmd || !data) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(s_at_mutex,
                       pdMS_TO_TICKS(prompt_timeout_ms +
                                     response_timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Phase 0: drain any stale lines. */
    char *junk = NULL;
    while (xQueueReceive(s_at_q, &junk, 0) == pdTRUE) free(junk);

    /* Phase 1: send the command. Don't pause RX yet — we want the
     * echo (if echo isn't disabled) to flow through the line-splitter
     * normally before the prompt arrives mid-stream. */
    char tx[MODEM_LINE_MAX];
    int tlen = snprintf(tx, sizeof(tx), "AT%s\r\n", cmd);
    if (tlen <= 0 || tlen >= (int) sizeof(tx)) {
        xSemaphoreGive(s_at_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    uart_write_bytes(MODEM_UART_PORT, tx, tlen);

    /* Phase 2: pause RX, poll UART directly for the prompt char. The
     * modem sends "\r\n> " (with trailing space) once it's ready for
     * the payload. We match just the prompt_char to be flexible. */
    s_rx_paused = true;
    /* Brief yield so the RX task notices the pause flag before we
     * start reading. */
    vTaskDelay(pdMS_TO_TICKS(20));

    TickType_t deadline = xTaskGetTickCount() +
                          pdMS_TO_TICKS(prompt_timeout_ms);
    bool got_prompt = false;
    while (xTaskGetTickCount() < deadline) {
        uint8_t b;
        int n = uart_read_bytes(MODEM_UART_PORT, &b, 1, pdMS_TO_TICKS(50));
        if (n == 1 && b == (uint8_t) prompt_char) {
            got_prompt = true;
            break;
        }
    }

    if (!got_prompt) {
        s_rx_paused = false;
        xSemaphoreGive(s_at_mutex);
        ESP_LOGW(TAG, "data-send: no '%c' prompt within %lu ms",
                 prompt_char, (unsigned long) prompt_timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    /* Phase 3: write payload + Ctrl-Z (0x1A) submit byte. */
    uart_write_bytes(MODEM_UART_PORT, data, data_len);
    uint8_t ctrl_z = 0x1A;
    uart_write_bytes(MODEM_UART_PORT, &ctrl_z, 1);

    /* Phase 4: resume RX and collect the terminal response. */
    s_rx_paused = false;

    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(response_timeout_ms);
    size_t resp_len = 0;
    if (resp_buf && buf_len > 0) resp_buf[0] = '\0';

    esp_err_t result = ESP_ERR_TIMEOUT;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) break;
        char *line = NULL;
        if (xQueueReceive(s_at_q, &line, deadline - now) != pdTRUE) break;

        if (is_terminator(line)) {
            result = (strcmp(line, "OK") == 0) ? ESP_OK : ESP_FAIL;
            free(line);
            break;
        }
        if (resp_buf && buf_len > 0) {
            size_t ll = strlen(line);
            if (resp_len + ll + 2 < buf_len) {
                if (resp_len > 0) resp_buf[resp_len++] = '\n';
                memcpy(resp_buf + resp_len, line, ll);
                resp_len += ll;
                resp_buf[resp_len] = '\0';
            }
        }
        free(line);
    }

    xSemaphoreGive(s_at_mutex);
    return result;
}

esp_err_t modem_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t r = init_gpios();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "gpio config failed: %s", esp_err_to_name(r));
        return r;
    }

    r = init_uart();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "uart init failed: %s", esp_err_to_name(r));
        return r;
    }

    s_at_mutex = xSemaphoreCreateMutex();
    s_at_q     = xQueueCreate(16, sizeof(char *));
    if (!s_at_mutex || !s_at_q) {
        ESP_LOGE(TAG, "queue/mutex alloc failed");
        return ESP_ERR_NO_MEM;
    }

    /* RX task first so we don't miss the first "OK" from the modem. */
    BaseType_t ok;
    ok = xTaskCreate(modem_rx_task, "modem_rx",
                     4096, NULL, tskIDLE_PRIORITY + 3, NULL);
    if (ok != pdPASS) return ESP_FAIL;
    ok = xTaskCreate(modem_sm_task, "modem_sm",
                     4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    if (ok != pdPASS) return ESP_FAIL;

    s_inited = true;
    return ESP_OK;
}
