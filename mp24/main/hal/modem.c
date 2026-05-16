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

static modem_urc_cb_t s_urc_cb      = NULL;

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

    /* Idle states: PWR_KEY low (no pulse), RESET_N high (out of reset). */
    gpio_set_level(PIN_MODEM_PWR_KEY, 0);
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

static void modem_rx_task(void *arg)
{
    (void) arg;
    char line[MODEM_LINE_MAX];
    size_t line_len = 0;

    for (;;) {
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
                if (s_urc_cb) s_urc_cb(line);
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
    gpio_set_level(PIN_MODEM_PWR_KEY, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(PIN_MODEM_PWR_KEY, 0);
}

static void modem_sm_task(void *arg)
{
    (void) arg;

    /* Let the modem stabilise from reset deassertion. */
    vTaskDelay(pdMS_TO_TICKS(200));

    set_state(MODEM_POWERING_ON);

    /* Quectel EG912U-GL power-on pulse: PWR_KEY high for ≥500 ms.
     * Datasheet recommends 1000 ms for robust start-up against
     * supply ramp jitter. */
    pulse_pwr_key(1000);

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

void modem_set_urc_cb(modem_urc_cb_t cb)
{
    s_urc_cb = cb;
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
