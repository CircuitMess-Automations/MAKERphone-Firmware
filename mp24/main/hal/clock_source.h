/*
 * mp24/main/hal/clock_source.h -- network-supplied wall clock from
 * the Quectel modem (AT+CCLK?).
 *
 * Sits on top of hal/modem.h. Once the modem is in READY state and
 * the cellular network has handed a NITZ time-zone update down,
 * AT+CCLK? returns the modem's local-time view as
 *
 *     +CCLK: "YY/MM/DD,HH:MM:SS+/-ZZ"
 *
 * where YY is the 2-digit year (offset from 2000), HH:MM:SS is the
 * local-time-of-day, and +/-ZZ is the time-zone offset in
 * quarter-hour units (so +32 == UTC+8:00, -20 == UTC-5:00).
 *
 * clock_source_init() blocks until either the modem reaches READY
 * and a valid CCLK response is parsed, OR ready_wait_ms elapses.
 * On success the parsed UTC epoch second is cached; callers query
 * it via clock_source_epoch_utc(). A zero return from epoch_utc()
 * means we never got a successful CCLK response (modem not ready,
 * SIM not registered, no NITZ from the network, parse failure).
 *
 * S-MP24/1: this module ships compiled and called from
 * sms_boot_task. On hardware where the modem never reaches READY
 * (no SIM, no battery, hardware regression) clock_source_init()
 * times out cleanly and the firmware behaves exactly as before --
 * PhoneClock falls back to its synthetic anchor.
 *
 * S-MP24/3: clock_source_refresh() re-queries AT+CCLK? and -- when
 * the result differs from the locally-extrapolated wall clock by
 * more than a caller-supplied threshold -- updates the cache. The
 * refresh path is driven from a low-priority FreeRTOS task in
 * app_main.cpp on a ~1-hour cadence so the displayed wall clock
 * stays bounded vs. NITZ updates from the network. The drift gate
 * means a user who edited the time in the Date&Time picker won't
 * have their edit silently overwritten an hour later by a
 * sub-minute network correction. (The picker writes through to
 * PhoneClock directly today, not back into this HAL, so without
 * the gate every refresh would clobber the user's value.)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wait up to ready_wait_ms for the modem to reach READY, then send
 * AT+CCLK?. On success the parsed UTC epoch second is cached and
 * ESP_OK is returned. ESP_ERR_TIMEOUT means the modem never came
 * up; ESP_FAIL means the modem responded but the CCLK reply could
 * not be parsed (typically because the network hasn't delivered
 * NITZ yet -- some modems then return "+CCLK: \"80/01/06,...\"" or
 * similar pre-NITZ defaults that we reject as bogus). Idempotent
 * within a single boot; later calls just return the cached state. */
esp_err_t clock_source_init(uint32_t ready_wait_ms);

/* Re-query AT+CCLK? and refresh the cached UTC epoch / TZ offset
 * if the new value differs from locally-extrapolated wall time by
 * more than drift_threshold_sec.
 *
 * Returns:
 *   ESP_OK                 a fresh +CCLK reply was parsed and the
 *                          delta vs. the extrapolated wall clock
 *                          exceeded drift_threshold_sec, so the
 *                          cache was updated. Callers should now
 *                          re-read clock_source_epoch_utc() /
 *                          _tz_offset_seconds() and propagate the
 *                          new values (e.g. through
 *                          clock_source_bridge_apply()).
 *   ESP_ERR_NOT_FOUND      the new value parsed cleanly but the
 *                          drift was within the threshold, so the
 *                          cache was left alone. Treat as "no-op,
 *                          success". This is the common refresh
 *                          outcome for hardware running on a
 *                          steady oscillator with periodic NITZ
 *                          delivery.
 *   ESP_ERR_INVALID_STATE  no successful clock_source_init has
 *                          ever happened (no anchor to drift-check
 *                          against). Caller should not have asked
 *                          for a refresh; gate on
 *                          clock_source_have_time() first.
 *   ESP_ERR_TIMEOUT        the modem is not in READY state (it
 *                          dropped back to BOOTING / FAILED, or
 *                          was never up). No AT command was sent.
 *   ESP_FAIL               the modem responded but the +CCLK
 *                          reply could not be parsed / fell
 *                          outside the validation ranges.
 *
 * Safe to call from any task: the underlying AT exchange is
 * serialised by the modem-layer mutex. Does not need to run on
 * the same task that called clock_source_init(). */
esp_err_t clock_source_refresh(uint32_t drift_threshold_sec);

/* Latest known UTC epoch second from the modem, or 0 if we haven't
 * successfully fetched one yet. Lock-free; safe from any context. */
uint32_t clock_source_epoch_utc(void);

/* True once a valid +CCLK reply has been parsed and cached. */
bool clock_source_have_time(void);

/* Time-zone offset reported by the modem in seconds east of UTC
 * (so UTC+1:00 -> +3600, UTC-5:00 -> -18000). Valid only when
 * clock_source_have_time() returns true; 0 otherwise. */
int32_t clock_source_tz_offset_seconds(void);

#ifdef __cplusplus
}
#endif
