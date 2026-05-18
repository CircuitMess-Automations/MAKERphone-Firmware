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
