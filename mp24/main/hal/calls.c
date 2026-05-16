/*
 * mp24/main/hal/calls.c — Quectel voice-call control plane.
 *
 * URCs we care about (registered as prefix subscriptions):
 *   RING                        inbound call alerting; CLIP follows
 *   +CLIP: "<num>",<type>...    caller number for the current RING
 *   CONNECT                     outbound dial picked up (data-mode)
 *   NO CARRIER                  remote hung up
 *   BUSY                        remote line busy
 *   NO ANSWER                   remote didn't pick up within timeout
 *   +CLCC: <id>,<dir>,<stat>... call list update from active query
 *
 * State machine:
 *   IDLE → DIALING (ATD sent OK) → RINGING_OUT (no specific URC for
 *          this on Quectel — synthetic from time-since-dial) → ACTIVE
 *          (on CONNECT) → TERMINATED (on NO CARRIER) → IDLE
 *
 *   IDLE → RINGING_IN (on RING + CLIP) → ACTIVE (on calls_answer)
 *        → TERMINATED → IDLE
 *
 * URCs run on the modem RX task and must not block. We push events
 * into a queue and let calls_event_task() process them; that task
 * fires the user callback and applies state transitions. */

#include "hal/calls.h"
#include "hal/modem.h"
#include "hal/audio_i2s2.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "CALLS";

static _Atomic int  s_state           = CALL_IDLE;
static char         s_remote[24]      = "";
static _Atomic uint32_t s_outbound    = 0;
static _Atomic uint32_t s_inbound     = 0;

static calls_event_cb_t s_user_cb     = NULL;
static void            *s_user_arg    = NULL;
static bool             s_inited      = false;

/* Pump queue: each entry is a small enum + payload string. */
typedef enum {
    PUMP_RING,
    PUMP_CLIP,
    PUMP_CONNECT,
    PUMP_HANGUP,
} pump_kind_t;

typedef struct {
    pump_kind_t kind;
    char        payload[24];   /* number for CLIP, empty otherwise */
} pump_msg_t;

static QueueHandle_t s_pump_q = NULL;

/* ----------------------------------------------------------------- */

const char *call_state_name(call_state_t s)
{
    switch (s) {
        case CALL_IDLE:        return "IDLE";
        case CALL_DIALING:     return "DIALING";
        case CALL_RINGING_OUT: return "RINGING_OUT";
        case CALL_RINGING_IN:  return "RINGING_IN";
        case CALL_ACTIVE:      return "ACTIVE";
        case CALL_TERMINATED:  return "TERMINATED";
    }
    return "?";
}

static void set_state(call_state_t s)
{
    int prev = atomic_exchange(&s_state, (int) s);
    if (prev != (int) s) {
        ESP_LOGI(TAG, "%s -> %s",
                 call_state_name((call_state_t) prev),
                 call_state_name(s));
    }
}

static void fire_user(call_event_t evt)
{
    if (!s_user_cb) return;
    call_event_payload_t p = {
        .event         = evt,
        .state         = (call_state_t) atomic_load(&s_state),
        .remote_number = s_remote,
    };
    s_user_cb(&p, s_user_arg);
}

/* ----------------------------------------------------------------- */
/* URC subscribers — fire from modem RX task, must be quick + non-
 * blocking. They push to s_pump_q for processing. */

static void on_ring(const char *line)
{
    (void) line;
    pump_msg_t m = { .kind = PUMP_RING, .payload = "" };
    xQueueSend(s_pump_q, &m, 0);
}

static void on_clip(const char *line)
{
    /* "+CLIP: \"+385912345678\",145,..." — pull the first quoted
     * token out as the caller number. */
    pump_msg_t m = { .kind = PUMP_CLIP, .payload = "" };
    const char *q1 = strchr(line, '"');
    if (q1) {
        const char *q2 = strchr(q1 + 1, '"');
        if (q2) {
            size_t l = q2 - q1 - 1;
            if (l >= sizeof(m.payload)) l = sizeof(m.payload) - 1;
            memcpy(m.payload, q1 + 1, l);
            m.payload[l] = '\0';
        }
    }
    xQueueSend(s_pump_q, &m, 0);
}

static void on_connect(const char *line)
{
    (void) line;
    pump_msg_t m = { .kind = PUMP_CONNECT, .payload = "" };
    xQueueSend(s_pump_q, &m, 0);
}

static void on_hangup(const char *line)
{
    (void) line;
    /* Reaches us for NO CARRIER / BUSY / NO ANSWER. */
    pump_msg_t m = { .kind = PUMP_HANGUP, .payload = "" };
    xQueueSend(s_pump_q, &m, 0);
}

/* ----------------------------------------------------------------- */

static void pump_task(void *arg)
{
    (void) arg;
    pump_msg_t m;
    while (xQueueReceive(s_pump_q, &m, portMAX_DELAY) == pdTRUE) {
        switch (m.kind) {

            case PUMP_RING:
                /* Inbound call alerting. Don't fire INCOMING until we
                 * have the CLIP number too — but if CLIP never comes
                 * (no-CLIP carrier), we still want to ring. Compromise:
                 * set state now; the CLIP handler refines the number. */
                if (atomic_load(&s_state) == CALL_IDLE) {
                    s_remote[0] = '\0';
                    set_state(CALL_RINGING_IN);
                    atomic_fetch_add(&s_inbound, 1);
                    fire_user(CALL_EVT_STATE_CHANGED);
                }
                break;

            case PUMP_CLIP:
                strncpy(s_remote, m.payload, sizeof(s_remote) - 1);
                s_remote[sizeof(s_remote) - 1] = '\0';
                fire_user(CALL_EVT_INCOMING);
                break;

            case PUMP_CONNECT:
                set_state(CALL_ACTIVE);
                /* Bring the modem-side audio bus online. Best-effort:
                 * if audio_i2s2 isn't initialised (e.g. modem never
                 * came up so init was skipped) this just returns
                 * ESP_ERR_INVALID_STATE and we carry on call-state-
                 * wise. */
                audio_i2s2_start();
                fire_user(CALL_EVT_CONNECTED);
                break;

            case PUMP_HANGUP:
                /* Stop reading from the modem's PCM bus before the
                 * modem itself tears down — avoids the RX driver
                 * hanging on clocks that suddenly stop. */
                audio_i2s2_stop();
                set_state(CALL_TERMINATED);
                fire_user(CALL_EVT_HANGUP);
                /* Brief settle so back-to-back events don't race on
                 * the same TERMINATED visibility. */
                vTaskDelay(pdMS_TO_TICKS(50));
                set_state(CALL_IDLE);
                fire_user(CALL_EVT_STATE_CHANGED);
                break;
        }
    }
}

/* ----------------------------------------------------------------- */

esp_err_t calls_init(uint32_t ready_wait_ms)
{
    if (s_inited) return ESP_OK;

    const TickType_t poll = pdMS_TO_TICKS(200);
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ready_wait_ms);
    while (modem_state() != MODEM_READY) {
        if (modem_state() == MODEM_FAILED) return ESP_ERR_INVALID_STATE;
        if (xTaskGetTickCount() > deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(poll);
    }

    /* Enable calling-line presentation so RING is followed by +CLIP. */
    if (modem_at_send("+CLIP=1", NULL, 0, 1500) != ESP_OK) {
        ESP_LOGW(TAG, "CLIP=1 failed (continuing — RING without caller ID)");
    }

    s_pump_q = xQueueCreate(8, sizeof(pump_msg_t));
    if (!s_pump_q) return ESP_ERR_NO_MEM;

    modem_subscribe_urc("RING",       on_ring);
    modem_subscribe_urc("+CLIP",      on_clip);
    modem_subscribe_urc("CONNECT",    on_connect);
    modem_subscribe_urc("NO CARRIER", on_hangup);
    modem_subscribe_urc("BUSY",       on_hangup);
    modem_subscribe_urc("NO ANSWER",  on_hangup);

    BaseType_t ok = xTaskCreate(pump_task, "calls_pump",
                                4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    if (ok != pdPASS) return ESP_FAIL;

    s_inited = true;
    ESP_LOGI(TAG, "init OK — CLIP on, RING/CONNECT/NO_CARRIER routed");
    return ESP_OK;
}

void calls_set_event_callback(calls_event_cb_t cb, void *user)
{
    s_user_cb  = cb;
    s_user_arg = user;
}

esp_err_t calls_dial(const char *number)
{
    if (!s_inited)          return ESP_ERR_INVALID_STATE;
    if (!number || !*number) return ESP_ERR_INVALID_ARG;
    if (atomic_load(&s_state) != CALL_IDLE) return ESP_ERR_INVALID_STATE;

    /* Snapshot the dialled number as the "remote" for later events. */
    strncpy(s_remote, number, sizeof(s_remote) - 1);
    s_remote[sizeof(s_remote) - 1] = '\0';

    char cmd[40];
    snprintf(cmd, sizeof(cmd), "D%s;", number);   /* ATD<number>; */

    set_state(CALL_DIALING);
    fire_user(CALL_EVT_STATE_CHANGED);

    /* ATD can take a couple of seconds before OK comes back; longer
     * waits would actually block on remote ring. We use 8 s — enough
     * for the modem-side accept, well before remote phone alerts. */
    esp_err_t r = modem_at_send(cmd, NULL, 0, 8000);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "ATD%s failed: %s", number, esp_err_to_name(r));
        set_state(CALL_IDLE);
        fire_user(CALL_EVT_STATE_CHANGED);
        return r;
    }
    /* OK from ATD means "dial accepted, dialling now". State stays
     * DIALING until a CONNECT URC arrives (-> ACTIVE) or one of the
     * hangup URCs (-> TERMINATED -> IDLE). Synthetic RINGING_OUT is
     * deferred; user code can infer it from elapsed time in DIALING. */
    atomic_fetch_add(&s_outbound, 1);
    return ESP_OK;
}

esp_err_t calls_answer(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (atomic_load(&s_state) != CALL_RINGING_IN) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t r = modem_at_send("A", NULL, 0, 3000);
    if (r == ESP_OK) {
        /* CONNECT URC may or may not arrive depending on modem config;
         * pre-emptively set ACTIVE so UI is responsive. */
        set_state(CALL_ACTIVE);
        audio_i2s2_start();
        fire_user(CALL_EVT_CONNECTED);
    }
    return r;
}

esp_err_t calls_hangup(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (atomic_load(&s_state) == CALL_IDLE) return ESP_OK;

    audio_i2s2_stop();
    esp_err_t r = modem_at_send("H", NULL, 0, 3000);
    if (r == ESP_OK) {
        set_state(CALL_TERMINATED);
        fire_user(CALL_EVT_HANGUP);
        vTaskDelay(pdMS_TO_TICKS(50));
        set_state(CALL_IDLE);
        fire_user(CALL_EVT_STATE_CHANGED);
    }
    return r;
}

call_state_t calls_state(void)
{
    return (call_state_t) atomic_load(&s_state);
}

const char *calls_remote_number(void)  { return s_remote; }
uint32_t calls_outbound_count(void)    { return atomic_load(&s_outbound); }
uint32_t calls_inbound_count(void)     { return atomic_load(&s_inbound); }
