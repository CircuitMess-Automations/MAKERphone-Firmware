/*
 * mp24/components/chatter_app/shim/ClockSourceBridge.cpp
 *
 * Bridges hal/clock_source (network-supplied UTC epoch + timezone
 * offset from the Quectel modem's AT+CCLK?) into PhoneClock (the
 * in-RAM wall-clock service that backs the status bar, the
 * Date&Time picker, PhoneAlarmService, PhoneVirtualPet, etc.).
 *
 * Why a separate file and not a direct call from hal/clock_source.c
 * into PhoneClock? Two reasons:
 *
 *   1. hal/clock_source lives in the `main` component, which is
 *      pure-C. PhoneClock is a C++ namespace defined in src/Services/
 *      compiled by the chatter_app component. Calling C++ from C
 *      requires a stable extern-"C" boundary, which is what this
 *      file provides.
 *
 *   2. PhoneClock uses a leap-year-free synthetic calendar where
 *      every year is exactly 365 days. Real Unix epoch seconds (as
 *      AT+CCLK? gives us, modulo timezone offset) advance through a
 *      real Gregorian calendar that includes 14 leap days between
 *      1970 and 2026. If we just dumped epoch_utc straight into
 *      PhoneClock::setEpoch() the displayed date would land 14
 *      days ahead of reality. The right thing is to break the
 *      modem epoch down into civil-time fields and rebuild them
 *      through PhoneClock::buildEpoch(), which lives in the same
 *      synthetic calendar as the rest of the firmware.
 *
 * S-MP24/2: this bridge ships compiled. The call site is
 * sms_boot_task in mp24/main/app_main.cpp, immediately after
 * clock_source_init() returns ESP_OK. On hardware where the modem
 * never reaches READY (no SIM, no battery, Q3 still open) the
 * caller skips this entirely; the firmware behaves exactly as
 * before (PhoneClock falls back to its synthetic 2026-01-01
 * anchor).
 *
 * The function is idempotent in practice: PhoneClock::setEpoch()
 * just overwrites the cached epoch+ref-millis pair, so calling
 * this twice from a hypothetical periodic re-query (S-MP24/3)
 * does no harm. We log a CLOCK: line on every successful apply so
 * boot.log shows the wall-clock transition the moment the modem
 * starts responding.
 */

#include <stdint.h>
#include <time.h>

#include "esp_log.h"

/* PhoneClock lives in src/Services/PhoneClock.h. PRIV_INCLUDE_DIRS
 * on this component already points at src/, so the relative path
 * Services/PhoneClock.h resolves. */
#include "Services/PhoneClock.h"

static const char *TAG = "CLOCK";

/* Called by sms_boot_task in app_main.cpp once both
 * clock_source_init() has returned ESP_OK and clock_source_have_time()
 * is true. epoch_utc is real Unix epoch seconds (UTC). tz_offset_sec
 * is the modem-reported timezone east of UTC in seconds.
 *
 * Implementation: convert to local-time-as-epoch (epoch_utc +
 * tz_offset_sec), then break down through gmtime_r() and feed the
 * civil-time fields through PhoneClock::buildEpoch() so the
 * result is in PhoneClock's synthetic calendar. */
extern "C" void clock_source_bridge_apply(uint32_t epoch_utc,
                                          int32_t  tz_offset_sec)
{
    if (epoch_utc == 0) {
        /* Shouldn't happen -- caller is supposed to gate on
         * clock_source_have_time() -- but be defensive. Leaving
         * PhoneClock on the 2026-01-01 anchor is the safe choice. */
        ESP_LOGW(TAG, "bridge: epoch_utc == 0, skipping setEpoch");
        return;
    }

    /* Local-time-as-epoch. The widening to int64_t catches the
     * pathological case where tz_offset_sec is, say, -14*3600 and
     * epoch_utc is small enough that adding it would underflow a
     * uint32_t. In practice epoch_utc from AT+CCLK? is around
     * 1.7e9 so this is just a defensive cast. */
    const int64_t local_epoch_64 = (int64_t) epoch_utc +
                                   (int64_t) tz_offset_sec;
    if (local_epoch_64 < 0 || local_epoch_64 > 0xFFFFFFFFLL) {
        ESP_LOGW(TAG, "bridge: local_epoch out of uint32_t range "
                      "(utc=%lu tz=%ld), skipping",
                 (unsigned long) epoch_utc,
                 (long) tz_offset_sec);
        return;
    }
    const time_t local_epoch = (time_t) local_epoch_64;

    /* gmtime_r treats its argument as UTC. We deliberately pass
     * local-time-as-epoch through gmtime_r so the broken-down
     * struct tm holds local civil-time fields. (We can't use
     * localtime_r because TZ has been forced to UTC0 system-wide
     * in hal/clock_source.c so mktime/gmtime stay in lockstep --
     * that's the same trick. We honour it here.) */
    struct tm tm_local = {};
    if (gmtime_r(&local_epoch, &tm_local) == NULL) {
        ESP_LOGW(TAG, "bridge: gmtime_r failed, skipping setEpoch");
        return;
    }

    /* PhoneClock::buildEpoch() clamps year to [2020..2099] and
     * every other field to its valid range, so partial garbage
     * from the modem doesn't crash anything -- it just lands on
     * a clamped date. Years are zero-based-from-1900 in struct
     * tm; convert. Months are 0..11 in struct tm; +1 for
     * PhoneClock's 1..12 contract. */
    const uint16_t year   = (uint16_t)(tm_local.tm_year + 1900);
    const uint8_t  month  = (uint8_t) (tm_local.tm_mon + 1);
    const uint8_t  day    = (uint8_t)  tm_local.tm_mday;
    const uint8_t  hour   = (uint8_t)  tm_local.tm_hour;
    const uint8_t  minute = (uint8_t)  tm_local.tm_min;
    const uint8_t  second = (uint8_t)  tm_local.tm_sec;

    const uint32_t synth_epoch = PhoneClock::buildEpoch(
        year, month, day, hour, minute, second);
    PhoneClock::setEpoch(synth_epoch);

    /* One-line summary in boot.log so the next fire's checkpoint
     * can confirm wall-clock acquisition just by grepping CLOCK:.
     * Timezone formatted as +/-HH:MM for readability. */
    const int tz_total_min = (int)(tz_offset_sec / 60);
    const int tz_h = tz_total_min / 60;
    const int tz_m = (tz_total_min < 0 ? -tz_total_min : tz_total_min) % 60;
    ESP_LOGI(TAG,
             "bridge: set PhoneClock to %04u-%02u-%02u %02u:%02u:%02u "
             "(UTC%+d:%02d, utc_epoch=%lu)",
             (unsigned) year, (unsigned) month, (unsigned) day,
             (unsigned) hour, (unsigned) minute, (unsigned) second,
             tz_h, tz_m,
             (unsigned long) epoch_utc);
}
