/*
 * mp24/main/hal/modem.h — GSM modem HAL (Quectel EG912U-GL).
 *
 * Hardware path:
 *   ESP32-S3 UART1 ↔ Quectel UART
 *     GPIO 17 (TX) → modem RX
 *     GPIO 18 (RX) ← modem TX
 *     115200 8N1, no flow control (Quectel default)
 *   GPIO 12  uGSM_PWR_KEY    active-high pulse, 1 s = power on, 700 ms = power off
 *   GPIO 15  uGSM_RESET_N    active-low (XTAL_32K_N pin used as IO_MUX GPIO)
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
 *   - URCs (Unsolicited Result Codes — RING, +CMTI, +CREG, etc.) are
 *     dispatched to a user callback registered via modem_set_urc_cb().
 *     S-MP08 only logs them; S-MP09 wires them into MessageService.
 *
 * If the modem doesn't respond (no battery, no SIM, wrong pinout,
 * missing module) the HAL transitions to FAILED, logs a clear error
 * and stays idle. Other firmware subsystems are unaffected.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
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

/* URC (Unsolicited Result Code) callback. Invoked once per URC line
 * from the modem RX task — short, non-blocking work only; offload
 * anything heavy to a separate task or queue. The line is NUL-
 * terminated and does NOT include the trailing CRLF. */
typedef void (*modem_urc_cb_t)(const char *line);
void modem_set_urc_cb(modem_urc_cb_t cb);
