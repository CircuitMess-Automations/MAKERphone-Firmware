/*
 * mp24/main/hal/sms.c — text-mode SMS over the modem AT framework.
 *
 * Send path:
 *   sms_send(number, body)
 *      → modem_at_send_data("+CMGS=\"<number>\"", '>', body, ...)
 *
 * Receive path:
 *   modem RX task gets a "+CMTI: \"SM\",N" URC
 *      → s_cmti_cb posts N onto s_cmti_q
 *      → s_fetch_task reads the queue, runs AT+CMGR=N, parses the
 *        +CMGR response into number + body, fires user callback,
 *        then AT+CMGD=N to free the SIM slot.
 *
 * The worker-task indirection matters: URC callbacks fire on the
 * modem RX task and must NOT call modem_at_send (which would
 * deadlock waiting on s_at_mutex held by ourselves). The fetch
 * task is on a separate context.
 */

#include "hal/sms.h"
#include "hal/modem.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "SMS";

static sms_received_cb_t s_user_cb       = NULL;
static void             *s_user_arg      = NULL;

static QueueHandle_t s_cmti_q            = NULL;   /* int (msg index) */
static _Atomic uint32_t s_sent_ok        = 0;
static _Atomic uint32_t s_sent_fail      = 0;
static _Atomic uint32_t s_received       = 0;
static bool s_inited                     = false;

/* ----------------------------------------------------------------- */

static void cmti_urc_cb(const char *line)
{
    /* Format: +CMTI: "SM",N  — N is the storage slot index. */
    const char *comma = strchr(line, ',');
    if (!comma) return;
    int idx = atoi(comma + 1);
    if (idx <= 0) return;

    if (s_cmti_q) {
        /* Non-blocking — if the queue is full the message will be
         * dropped, but the modem holds it in SIM storage so a future
         * boot can re-fetch via AT+CMGL. */
        xQueueSend(s_cmti_q, &idx, 0);
    }
}

/* Pull a +CMGR response apart. Format:
 *
 *   +CMGR: "REC UNREAD","+385912345678",,"2026/05/16,10:30:42+08"
 *   Hello world
 *
 * The header line carries the sender; the next line is the body.
 * resp_buf passed in is the joined "\n"-separated response from
 * modem_at_send. */
static bool parse_cmgr(const char *resp,
                       char *number, size_t number_sz,
                       char *body,   size_t body_sz)
{
    if (!resp || !number || !body) return false;
    number[0] = body[0] = '\0';

    /* Find the +CMGR: line */
    const char *line = strstr(resp, "+CMGR:");
    if (!line) return false;

    /* Walk past the first quoted token ("REC UNREAD") and pick up
     * the second one (the number). Quectel always emits the same
     * order: status,sender,?,timestamp. */
    const char *p = line;
    int quote_pos = 0;        /* count of '"' chars seen */
    while (*p && quote_pos < 2) {
        if (*p == '"') quote_pos++;
        p++;
    }
    if (quote_pos < 2) return false;
    /* p now points just past the SECOND '"' — start of sender. */
    const char *q = strchr(p, '"');
    if (!q) return false;
    size_t nlen = q - p;
    if (nlen >= number_sz) nlen = number_sz - 1;
    memcpy(number, p, nlen);
    number[nlen] = '\0';

    /* Body sits on the line after the header. */
    const char *body_start = strchr(line, '\n');
    if (!body_start) return false;
    body_start++;
    size_t blen = strlen(body_start);
    /* Trim trailing whitespace. */
    while (blen > 0 && (body_start[blen-1] == '\r' ||
                        body_start[blen-1] == '\n' ||
                        body_start[blen-1] == ' ')) blen--;
    if (blen >= body_sz) blen = body_sz - 1;
    memcpy(body, body_start, blen);
    body[blen] = '\0';
    return true;
}

static void fetch_task(void *arg)
{
    (void) arg;
    int idx;
    while (xQueueReceive(s_cmti_q, &idx, portMAX_DELAY) == pdTRUE) {
        /* Drop CMGR queries here even if the modem rejects them; we
         * still want to clear the slot so storage doesn't fill up. */
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "+CMGR=%d", idx);

        char resp[512];
        esp_err_t r = modem_at_send(cmd, resp, sizeof(resp), 3000);
        if (r == ESP_OK) {
            char number[SMS_NUMBER_MAX_LEN];
            char body  [SMS_BODY_MAX_LEN];
            if (parse_cmgr(resp, number, sizeof(number),
                                 body,   sizeof(body))) {
                atomic_fetch_add(&s_received, 1);
                ESP_LOGI(TAG, "RX from %s: \"%s\"", number, body);
                if (s_user_cb) s_user_cb(number, body, s_user_arg);
            } else {
                ESP_LOGW(TAG, "CMGR parse failed for slot %d", idx);
            }
        } else {
            ESP_LOGW(TAG, "CMGR=%d failed: %s", idx, esp_err_to_name(r));
        }

        /* Free the SIM storage slot regardless of parse success. */
        char del_cmd[24];
        snprintf(del_cmd, sizeof(del_cmd), "+CMGD=%d", idx);
        modem_at_send(del_cmd, NULL, 0, 3000);
    }
}

/* ----------------------------------------------------------------- */

esp_err_t sms_init(uint32_t ready_wait_ms)
{
    if (s_inited) return ESP_OK;

    /* Spin-wait for the modem to come up. We don't have a condvar in
     * modem.h so poll every 200 ms. */
    const TickType_t poll = pdMS_TO_TICKS(200);
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ready_wait_ms);
    while (modem_state() != MODEM_READY) {
        if (modem_state() == MODEM_FAILED) {
            ESP_LOGW(TAG, "modem FAILED — SMS unavailable");
            return ESP_ERR_INVALID_STATE;
        }
        if (xTaskGetTickCount() > deadline) {
            ESP_LOGW(TAG, "modem not READY within %lu ms — SMS init aborted",
                     (unsigned long) ready_wait_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(poll);
    }

    /* Text mode (vs PDU). */
    if (modem_at_send("+CMGF=1", NULL, 0, 1500) != ESP_OK) {
        ESP_LOGE(TAG, "CMGF=1 (text mode) failed");
        return ESP_FAIL;
    }
    /* GSM-7 charset for sender + body. UCS-2 unicode handled in
     * a later session if/when Unicode becomes a real ask. */
    if (modem_at_send("+CSCS=\"GSM\"", NULL, 0, 1500) != ESP_OK) {
        ESP_LOGW(TAG, "CSCS=\"GSM\" failed (continuing)");
    }
    /* New-message indicator: 2 → forward URCs while idle; 1 → buffer
     * URCs during data calls; 0 → discard new SMS notifications.
     * Quectel default is 2,1,0,0,0 which is exactly what we want. */
    if (modem_at_send("+CNMI=2,1,0,0,0", NULL, 0, 1500) != ESP_OK) {
        ESP_LOGW(TAG, "CNMI=2,1,0,0,0 failed (continuing)");
    }

    s_cmti_q = xQueueCreate(8, sizeof(int));
    if (!s_cmti_q) {
        ESP_LOGE(TAG, "CMTI queue alloc failed");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t r = modem_subscribe_urc("+CMTI", cmti_urc_cb);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "URC subscribe failed: %s", esp_err_to_name(r));
        return r;
    }
    BaseType_t ok = xTaskCreate(fetch_task, "sms_fetch",
                                4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "fetch task spawn failed");
        return ESP_FAIL;
    }

    s_inited = true;
    ESP_LOGI(TAG, "init OK — text mode, GSM-7, +CMTI routed");
    return ESP_OK;
}

void sms_set_received_callback(sms_received_cb_t cb, void *user)
{
    s_user_cb  = cb;
    s_user_arg = user;
}

esp_err_t sms_send(const char *number, const char *text)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!number || !text)            return ESP_ERR_INVALID_ARG;
    if (number[0] == '\0')           return ESP_ERR_INVALID_ARG;
    if (strlen(number) > SMS_NUMBER_MAX_LEN - 1) return ESP_ERR_INVALID_ARG;

    /* Truncate body to GSM-7 single-SMS limit. */
    size_t tlen = strlen(text);
    if (tlen > 160) tlen = 160;

    char cmd[32 + SMS_NUMBER_MAX_LEN];
    snprintf(cmd, sizeof(cmd), "+CMGS=\"%s\"", number);

    char resp[128];
    esp_err_t r = modem_at_send_data(
        cmd,
        '>',
        text, tlen,
        resp, sizeof(resp),
        2000,    /* prompt timeout — modem replies '>' in ~100 ms typically */
        60000    /* response timeout — network ack up to 60 s on EDGE */
    );

    if (r == ESP_OK) {
        atomic_fetch_add(&s_sent_ok, 1);
        ESP_LOGI(TAG, "TX to %s OK (%zu bytes): %s", number, tlen, resp);
    } else {
        atomic_fetch_add(&s_sent_fail, 1);
        ESP_LOGW(TAG, "TX to %s failed: %s", number, esp_err_to_name(r));
    }
    return r;
}

uint32_t sms_sent_ok_count(void)   { return atomic_load(&s_sent_ok); }
uint32_t sms_sent_fail_count(void) { return atomic_load(&s_sent_fail); }
uint32_t sms_received_count(void)  { return atomic_load(&s_received); }
