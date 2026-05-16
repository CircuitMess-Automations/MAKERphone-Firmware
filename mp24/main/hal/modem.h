/*
 * mp24/main/hal/modem.h — GSM modem HAL (Quectel EG912U-GL).
 *
 * Hardware path:
 *   ESP32-S3 UART1 ↔ Quectel UART
 *     GPIO 17 (TX) → modem RX
 *     GPIO 18 (RX) ← modem TX
 *     115200 8N1, no flow control (Quectel default)
 *   GPIO 12  uGSM_PWR_KEY    active-high pulse, 1 s = power on, 700 ms = power off
 *   GPIO 16  uGSM_RESET_N    active-low (chip pad #22, XTAL_32K_N in IO_MUX)
 *   GPIO 11  uGSM_PSM_EXT_INT (deferred — power-saving wake interrupt)
 *
 * I²S2 (GPIO 13/14/21) is the voice-PCM path and lands in its own
 * session (S-MP10), not part of this layer.
 *
 * Architecture:
 *   - modem_init() configures GPIOs + UART driver and spawns ONE
 *     background task that owns the modem state machine. App_main
 *     never blocks on modem boot.
 *   - The state-machine task drives PWR_KEY, then polls "AT" until
 *     the modem responds (or 30 s timeout). On success it does ATE0
 *     + AT+CGMM to confirm the module identity, then sits in READY
 *     state servicing AT requests + URCs.
 *   - modem_at_send() is synchronous and thread-safe; it blocks the
 *     calling task on a queue while the RX task accumulates the
 *     response. Use it from any non-ISR context.
 *   - modem_at_send_data() handles interactive commands such as
 *     AT+CMGS that emit a '>' prompt mid-stream. The RX task is
 *     briefly paused so the sender can poll UART directly for the
 *     prompt before resuming normal line-based response collection.
 *   - URCs (Unsolicited Result Codes — RING, +CMTI, +CREG, etc.) are
 *     dispatched to prefix-matched subscribers registered via
 *     modem_subscribe_urc(). Multiple subscribers can coexist (SMS,
 *     voice, network state, etc.). modem_set_urc_cb() registers a
 *     catch-all subscriber that sees every URC line.
 *
 * If the modem doesn't respond (no battery, no SIM, wrong pinout,
 * missing module) the HAL transitions to FAILED, logs a clear error
 * and stays idle. Other firmware subsystems are unaffected.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    MODEM_OFF,            /* Power off; PWR_KEY idle, no UART traffic */
    MODEM_POWERING_ON,    /* PWR_KEY pulse active */
    MODEM_BOOTING,        /* Pulse done, waiting for first AT response */
    MODEM_READY,          /* AT confirmed, module identified */
    MODEM_FAILED,         /* Boot timeout or fatal error */
} modem_state_t;

/* Human-readable state name for logs and the dashboard. */
const char *modem_state_name(modem_state_t s);

/* Current state. Lock-free atomic read; safe from any context. */
modem_state_t modem_state(void);

/* The module identifier string returned by AT+CGMM during boot
 * (e.g. "EG912U-GL"). Empty string if not yet acquired. Pointer is
 * valid for the firmware's lifetime. */
const char *modem_model(void);

/* Set up UART1 + control GPIOs, spawn the state-machine task. The
 * task immediately begins the power-on sequence; this call returns
 * within a few ms. Idempotent. */
esp_err_t modem_init(void);

/* Send an AT command (without the leading "AT" or trailing CRLF —
 * pass e.g. "+CSQ" not "AT+CSQ\r\n") and synchronously wait for
 * the final "OK" / "ERROR" line.
 *
 *   resp_buf  optional, receives all intermediate response lines
 *             joined by '\n'. Pass NULL to discard. NUL-terminated.
 *   buf_len   sizeof(*resp_buf).
 *   timeout_ms  wall-clock cap; typical AT commands return in 50–
 *               300 ms but some (CGATT, COPS) can take 30 s+.
 *
 * Returns:
 *   ESP_OK           response ended with "OK"
 *   ESP_FAIL         response ended with "ERROR" or "+CME ERROR"
 *   ESP_ERR_TIMEOUT  no terminal line within timeout_ms
 *   ESP_ERR_INVALID_STATE  modem isn't in READY (or BOOTING+) state
 *
 * Thread-safe: serialised internally with a mutex so callers from
 * different tasks queue rather than collide on the UART. */
esp_err_t modem_at_send(const char *cmd,
                        char *resp_buf, size_t buf_len,
                        uint32_t timeout_ms);

/* Interactive AT command for prompt-based protocols (AT+CMGS, AT+QFUPL,
 * etc.). Steps:
 *   1. Sends "AT<cmd>\r\n" verbatim.
 *   2. Pauses RX task and polls UART directly for prompt_char (typ.
 *      '>'). Times out at prompt_timeout_ms.
 *   3. On prompt, writes `data` (`data_len` bytes) followed by the
 *      0x1A (Ctrl-Z) terminator.
 *   4. Resumes RX task, waits up to response_timeout_ms for the
 *      final OK / ERROR line.
 *
 * resp_buf/buf_len same semantics as modem_at_send. Returns ESP_OK
 * only on terminal OK; ESP_FAIL on ERROR/+CMS ERROR; ESP_ERR_TIMEOUT
 * if either phase times out. */
esp_err_t modem_at_send_data(const char *cmd,
                             char prompt_char,
                             const void *data, size_t data_len,
                             char *resp_buf, size_t buf_len,
                             uint32_t prompt_timeout_ms,
                             uint32_t response_timeout_ms);

/* URC (Unsolicited Result Code) callback. Invoked once per URC line
 * from the modem RX task — short, non-blocking work only; offload
 * anything heavy to a separate task or queue. The line is NUL-
 * terminated and does NOT include the trailing CRLF. */
typedef void (*modem_urc_cb_t)(const char *line);

/* Register a URC subscriber filtered by line prefix. The callback
 * fires for any URC line whose first chars match `prefix` (case
 * sensitive). Up to 8 subscribers; further registrations fail
 * silently with a warning log. Use prefix="" for a catch-all. */
esp_err_t modem_subscribe_urc(const char *prefix, modem_urc_cb_t cb);

/* Compatibility: register a catch-all URC subscriber. Equivalent to
 * modem_subscribe_urc("", cb). Existing callers stay working. */
void modem_set_urc_cb(modem_urc_cb_t cb);
