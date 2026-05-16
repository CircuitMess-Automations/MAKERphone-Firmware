/*
 * mp24/main/hal/sms.h — SMS over the GSM modem AT framework.
 *
 * Layers on top of hal/modem.h. Once the modem is in READY state,
 * sms_init() configures text-mode SMS (AT+CMGF=1), GSM-7 charset
 * (AT+CSCS="GSM"), routes inbound-message notifications via the
 * +CMTI URC, and subscribes a handler that fetches the message body
 * with AT+CMGR and forwards it to the user callback.
 *
 * Outbound sends use modem_at_send_data() to navigate the Quectel
 * AT+CMGS interactive prompt: command line, wait for '>', payload,
 * Ctrl-Z, wait for +CMGS terminator. The default 60 s response
 * timeout covers a network-side ack on a slow EDGE link.
 *
 * Numbers are E.164 with a leading '+' (e.g. "+385912345678").
 * Body text is plain ASCII / GSM-7 (no Unicode — that needs UCS-2
 * mode which lives in a follow-up session). Max body 160 chars per
 * SMS; longer messages get truncated.
 *
 * Lightweight counters (sent OK, send failures, received) are
 * exposed for the dashboard.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define SMS_NUMBER_MAX_LEN   24    /* +<country><number><NUL>, generous */
#define SMS_BODY_MAX_LEN    161    /* GSM-7 max 160 + NUL */

/* Callback fired when a complete SMS arrives. The arguments live
 * only for the duration of the call — copy them if you need them
 * past return. The callback runs on a dedicated worker task so it
 * may block on AT commands or storage, but should still avoid
 * indefinite blocking. */
typedef void (*sms_received_cb_t)(const char *number,
                                  const char *body,
                                  void *user);

/* Configure text mode + URC routing. Blocks until the modem reaches
 * READY state OR until ready_wait_ms elapses. Returns ESP_OK once
 * the AT configuration commands all succeeded; ESP_ERR_TIMEOUT if
 * the modem never came up. */
esp_err_t sms_init(uint32_t ready_wait_ms);

/* Register (or replace) the inbound-message callback. NULL clears
 * the callback. The `user` pointer is opaque to the SMS layer. */
void sms_set_received_callback(sms_received_cb_t cb, void *user);

/* Send a text-mode SMS. number is E.164 ("+385..."); text is
 * 7-bit ASCII / GSM-7 alphabet, ≤160 chars (longer truncates).
 *
 * Synchronous. Blocks while the modem ACKs the message (typical
 * 2–10 s on a registered network; up to 60 s in poor coverage).
 * Returns ESP_OK on +CMGS / OK terminator, ESP_FAIL on +CMS ERROR,
 * ESP_ERR_TIMEOUT on no response, or ESP_ERR_INVALID_ARG on
 * malformed inputs. */
esp_err_t sms_send(const char *number, const char *text);

/* Cumulative counters since boot. Cheap atomic reads. */
uint32_t sms_sent_ok_count(void);
uint32_t sms_sent_fail_count(void);
uint32_t sms_received_count(void);
