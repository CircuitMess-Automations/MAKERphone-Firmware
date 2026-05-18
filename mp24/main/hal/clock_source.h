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
 * Architecture note: this module is intentionally read-once at
 * boot. A future session can wire periodic re-queries + listener
 * fan-out for drift correction (S-MP24/2+), but the first cut is
 * single-shot so the rest of the firmware can incrementally adopt
 * a real wall-clock source without us paying for a polling task
 * we don't need yet.
 *
 * S-MP24/1: this module ships compiled and called from
 * sms_boot_task. On hardware where the modem never reaches READY
 * (no SIM, no battery, hardware regression) clock_source_init()
 * times out cleanly and the firmware behaves exactly as before --
 * PhoneClock falls back to its synthetic anchor.
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
