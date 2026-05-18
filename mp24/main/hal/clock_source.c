/*
 * mp24/main/hal/clock_source.c -- AT+CCLK? based wall-clock source.
 *
 * Read-once-at-boot model. See hal/clock_source.h for the contract.
 *
 * AT+CCLK? response format (Quectel EG912U-GL AT Commands Manual,
 * §4.1):
 *
 *     +CCLK: "YY/MM/DD,HH:MM:SS<S>ZZ"
 *
 *   YY     2-digit year, offset from 2000 (so 26 == 2026)
 *   MM     month, 01..12
 *   DD     day-of-month, 01..31
 *   HH     hour, 00..23     (LOCAL time)
 *   MM     minute, 00..59
 *   SS     second, 00..59
 *   <S>    sign, '+' or '-'
 *   ZZ     two-digit unsigned magnitude in QUARTER-HOUR units of
 *          local-time offset from UTC (so +32 == UTC+8:00,
 *          -20 == UTC-5:00, +00 == UTC).
 *
 * Conversion: build a `struct tm` from the local-time fields (year
 * normalised to tm_year = year - 1900, month to tm_mon = month - 1,
 * everything else 1:1), feed it to mktime() with the process TZ
 * locked to UTC so mktime treats the input as UTC, then subtract
 * (zone * 15 * 60) to recover the actual UTC second.
 *
 * Rejected:
 *   - Year < 2024 or > 2069 (we ship in 2026 -- anything pre-2024
 *     is either a pre-NITZ default like "80/01/06,..." or garbled;
 *     2069 caps the 2-digit-year ambiguity at "the next 43 years")
 *   - mktime returning (time_t)-1
 *   - zone magnitude > 56 (== UTC+14:00, the max valid zone -- some
 *     modems return 96 or similar garbage before NITZ delivers)
 *
 * The parse + reject logic is paranoid because the device WILL ship
 * to users in regions where the network rolls out NITZ slowly, and
 * a wrong wall-clock is worse than the synthetic 2026-01-01 anchor.
 */

#include "hal/clock_source.h"
#include "hal/modem.h"

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "CLOCK";

/* All state behind atomics so concurrent reads from app code are
 * safe without a lock. clock_source_init runs exactly once on the
 * sms_boot_task; reads come from any task. */
static _Atomic uint32_t s_epoch_utc        = 0;
static _Atomic int32_t  s_tz_offset_seconds = 0;
static _Atomic bool     s_have_time        = false;
static _Atomic bool     s_inited           = false;

/* S-MP24/3: tick-count at the moment s_epoch_utc was last set
 * (either by clock_source_init() or by a successful
 * clock_source_refresh() apply). Used by refresh() to compute
 * the locally-extrapolated wall clock and compare against a
 * fresh +CCLK reply for the drift gate. xTaskGetTickCount()
 * is a uint32_t that wraps every (configTICK_RATE_HZ * 2^32)
 * seconds; for a 1 kHz tick that's ~49 days, well above our
 * 1-hour refresh cadence, so unsigned-subtraction modulo wrap
 * gives the correct elapsed delta. */
static _Atomic uint32_t s_set_at_ticks     = 0;

/* Wait for the modem to reach READY (or FAILED). Returns true if
 * READY before deadline, false on timeout / FAILED. */
static bool wait_for_ready(uint32_t deadline_ms)
{
    const TickType_t deadline = xTaskGetTickCount() +
                                pdMS_TO_TICKS(deadline_ms);
    while (xTaskGetTickCount() < deadline) {
        modem_state_t st = modem_state();
        if (st == MODEM_READY) return true;
        if (st == MODEM_FAILED) {
            ESP_LOGW(TAG, "modem entered FAILED state -- giving up");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return false;
}

/* Parse a CCLK response line into year/month/day/hh/mm/ss + zone.
 * Returns true if every field landed inside its valid range. */
static bool parse_cclk(const char *resp,
                       int *year, int *mon, int *day,
                       int *hour, int *minute, int *second,
                       int *zone_quarter_hours)
{
    if (!resp) return false;

    /* The response may be multi-line: ECHO line, +CCLK line, OK.
     * Locate the +CCLK: line explicitly rather than assuming the
     * first token. */
    const char *p = strstr(resp, "+CCLK:");
    if (!p) return false;
    p += 6;  /* past "+CCLK:" */

    /* Skip whitespace + opening quote */
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') p++;

    int yy, mo, dd, hh, mm, ss, zz;
    char sign;
    /* sscanf with %2d for the time fields is robust against weird
     * spacing. The zone has a separate sign char so we scan it as
     * %c%2d, then negate as needed. */
    int n = sscanf(p, "%2d/%2d/%2d,%2d:%2d:%2d%c%2d",
                   &yy, &mo, &dd, &hh, &mm, &ss, &sign, &zz);
    if (n != 8) {
        ESP_LOGW(TAG, "CCLK parse: only %d fields scanned from \"%s\"", n, p);
        return false;
    }
    if (sign == '-') zz = -zz;
    else if (sign != '+') {
        ESP_LOGW(TAG, "CCLK parse: bogus zone sign '%c'", sign);
        return false;
    }

    /* Sanity bounds. yy is years since 2000. We reject 2-digit
     * years that resolve to before 2024 or after 2069 -- the
     * pre-NITZ default many modems emit is "80/01/06,..." which
     * resolves to 2080 with a naive +2000 (or 1980 with the GSM
     * spec's "+1900 if YY>=70 else +2000" rule; either way well
     * outside our 2024..2069 production window). */
    int full_year = 2000 + yy;
    if (full_year < 2024 || full_year > 2069) {
        ESP_LOGW(TAG, "CCLK parse: year %d outside [2024, 2069]", full_year);
        return false;
    }
    if (mo < 1 || mo > 12) return false;
    if (dd < 1 || dd > 31) return false;
    if (hh < 0 || hh > 23) return false;
    if (mm < 0 || mm > 59) return false;
    if (ss < 0 || ss > 59) return false;
    if (zz < -56 || zz > 56) {
        ESP_LOGW(TAG, "CCLK parse: zone %d outside valid +/-14h", zz);
        return false;
    }

    *year   = full_year;
    *mon    = mo;
    *day    = dd;
    *hour   = hh;
    *minute = mm;
    *second = ss;
    *zone_quarter_hours = zz;
    return true;
}

/* Convert parsed local-time fields + zone into UTC epoch seconds.
 *
 * We treat the local-time fields as UTC for mktime's purposes by
 * forcing TZ=UTC on entry (the call is idempotent and cheap; the
 * tzset() costs ~1 ms but only fires when the process TZ changes).
 * Then we subtract the zone offset to recover real UTC. */
static uint32_t local_fields_to_utc_epoch(int year, int mon, int day,
                                          int hour, int minute, int second,
                                          int zone_quarter_hours)
{
    /* Pin TZ=UTC so mktime is a pure y/m/d/h/m/s -> seconds
     * function. ESP-IDF's newlib already defaults to UTC when
     * setenv("TZ", ...) is never called, but make it explicit so
     * a future module that touches TZ doesn't silently corrupt
     * the modem time. */
    setenv("TZ", "UTC0", 1);
    tzset();

    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec  = second;
    tm.tm_isdst = 0;

    time_t t = mktime(&tm);
    if (t == (time_t)-1) {
        ESP_LOGW(TAG, "mktime failed on %04d-%02d-%02d %02d:%02d:%02d",
                 year, mon, day, hour, minute, second);
        return 0;
    }

    /* mktime returned the "as-if-UTC" seconds, but the input was
     * really local time. Subtract the zone offset to get true UTC. */
    int32_t offset_seconds = (int32_t)zone_quarter_hours * 15 * 60;
    time_t utc = t - offset_seconds;
    if (utc < 0) return 0;
    return (uint32_t) utc;
}

esp_err_t clock_source_init(uint32_t ready_wait_ms)
{
    bool already = atomic_exchange(&s_inited, true);
    if (already) {
        /* A second call from a different task -- return the cached
         * state. */
        return atomic_load(&s_have_time) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    if (!wait_for_ready(ready_wait_ms)) {
        ESP_LOGW(TAG, "modem not READY after %u ms -- skipping CCLK",
                 (unsigned) ready_wait_ms);
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "modem READY -- querying AT+CCLK?");

    char resp[96];
    esp_err_t r = modem_at_send("+CCLK?", resp, sizeof(resp), 3000);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "AT+CCLK? failed: %s", esp_err_to_name(r));
        return r;
    }

    int year, mon, day, hour, minute, second, zone_qh;
    if (!parse_cclk(resp, &year, &mon, &day, &hour, &minute, &second, &zone_qh)) {
        ESP_LOGW(TAG, "AT+CCLK? unparseable / out-of-range; resp=\"%s\"", resp);
        return ESP_FAIL;
    }

    uint32_t utc = local_fields_to_utc_epoch(year, mon, day, hour, minute,
                                             second, zone_qh);
    if (utc == 0) {
        return ESP_FAIL;
    }

    int32_t tz_seconds = zone_qh * 15 * 60;
    atomic_store(&s_epoch_utc, utc);
    atomic_store(&s_tz_offset_seconds, tz_seconds);
    atomic_store(&s_set_at_ticks, (uint32_t) xTaskGetTickCount());
    atomic_store(&s_have_time, true);

    ESP_LOGI(TAG,
             "CCLK: %04d-%02d-%02d %02d:%02d:%02d (zone %+d s) -> epoch %u UTC",
             year, mon, day, hour, minute, second, (int) tz_seconds,
             (unsigned) utc);
    return ESP_OK;
}

uint32_t clock_source_epoch_utc(void)
{
    return atomic_load(&s_epoch_utc);
}

bool clock_source_have_time(void)
{
    return atomic_load(&s_have_time);
}

int32_t clock_source_tz_offset_seconds(void)
{
    return atomic_load(&s_tz_offset_seconds);
}

/* S-MP24/3: shared between init and refresh. Sends AT+CCLK?,
 * parses the reply, validates, and converts to UTC epoch + TZ
 * offset. Returns true on success and fills *utc_out / *tz_out;
 * false on any modem / parse / validation failure (the caller
 * decides how to react). Does NOT update any cached state -- the
 * caller does that. */
static bool query_and_parse_cclk(uint32_t timeout_ms,
                                 uint32_t *utc_out,
                                 int32_t  *tz_out)
{
    char resp[96];
    esp_err_t r = modem_at_send("+CCLK?", resp, sizeof(resp), timeout_ms);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "AT+CCLK? failed: %s", esp_err_to_name(r));
        return false;
    }

    int year, mon, day, hour, minute, second, zone_qh;
    if (!parse_cclk(resp, &year, &mon, &day, &hour, &minute, &second,
                    &zone_qh)) {
        ESP_LOGW(TAG, "AT+CCLK? unparseable / out-of-range; resp=\"%s\"",
                 resp);
        return false;
    }

    uint32_t utc = local_fields_to_utc_epoch(year, mon, day, hour, minute,
                                             second, zone_qh);
    if (utc == 0) return false;

    if (utc_out) *utc_out = utc;
    if (tz_out)  *tz_out  = (int32_t) zone_qh * 15 * 60;
    return true;
}

esp_err_t clock_source_refresh(uint32_t drift_threshold_sec)
{
    /* Gate: no anchor means refresh has nothing to compare against
     * and -- by contract -- this function never establishes the
     * first cached value (clock_source_init owns that). The caller
     * should have checked clock_source_have_time() before asking. */
    if (!atomic_load(&s_have_time)) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Gate: only attempt the AT exchange when the modem is READY.
     * sms_boot_task may have reached READY hours ago and then the
     * modem could have crashed back to FAILED or restarted into
     * BOOTING; in either case sending an AT command now would just
     * time out the modem_at_send mutex for 3 s. */
    if (modem_state() != MODEM_READY) {
        return ESP_ERR_TIMEOUT;
    }

    uint32_t fresh_utc = 0;
    int32_t  fresh_tz  = 0;
    if (!query_and_parse_cclk(3000, &fresh_utc, &fresh_tz)) {
        return ESP_FAIL;
    }

    /* Compute the locally-extrapolated wall clock: cached_utc plus
     * the elapsed wall-clock seconds since the cache was last set.
     * xTaskGetTickCount is a uint32_t that wraps; unsigned subtraction
     * modulo 2^32 still gives the correct delta as long as the actual
     * elapsed time is below the wrap period (~49 days at 1 kHz). */
    const uint32_t cached_utc    = atomic_load(&s_epoch_utc);
    const uint32_t set_at_ticks  = atomic_load(&s_set_at_ticks);
    const uint32_t now_ticks     = (uint32_t) xTaskGetTickCount();
    const uint32_t elapsed_ticks = now_ticks - set_at_ticks;
    const uint32_t elapsed_secs  = elapsed_ticks / configTICK_RATE_HZ;

    const uint32_t extrapolated_utc = cached_utc + elapsed_secs;
    int64_t delta = (int64_t) fresh_utc - (int64_t) extrapolated_utc;
    int64_t abs_delta = delta < 0 ? -delta : delta;

    if (abs_delta <= (int64_t) drift_threshold_sec) {
        /* No-op: the network agrees with us within the threshold.
         * Log at DEBUG so production boot.logs don't fill up with
         * "refresh: drift 3 s, no update" lines every hour. */
        ESP_LOGD(TAG,
                 "refresh: drift %lld s within +/- %u s threshold, "
                 "no update",
                 (long long) delta,
                 (unsigned) drift_threshold_sec);
        return ESP_ERR_NOT_FOUND;
    }

    /* The drift exceeds the gate -- adopt the fresh value. Order
     * matches clock_source_init: data fields before the
     * have-time flag, so a concurrent reader either sees the old
     * snapshot or the new snapshot but never a torn mix. */
    atomic_store(&s_epoch_utc, fresh_utc);
    atomic_store(&s_tz_offset_seconds, fresh_tz);
    atomic_store(&s_set_at_ticks, now_ticks);

    ESP_LOGI(TAG,
             "refresh: drift %+lld s exceeded %u s gate -- "
             "epoch %u UTC (tz %+d s)",
             (long long) delta,
             (unsigned) drift_threshold_sec,
             (unsigned) fresh_utc,
             (int) fresh_tz);

    return ESP_OK;
}
