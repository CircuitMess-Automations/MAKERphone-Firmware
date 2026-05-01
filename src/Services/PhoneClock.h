#ifndef MAKERPHONE_PHONECLOCK_H
#define MAKERPHONE_PHONECLOCK_H

#include <Arduino.h>
#include <stdint.h>

/**
 * PhoneClock
 *
 * Tiny in-memory wall-clock service introduced in S54 to back the
 * Date & Time settings screen. Chatter has no real RTC, so the
 * existing PhoneStatusBar / PhoneClockFace render an uptime-based
 * synthetic clock keyed off millis(). PhoneDateTimeScreen needs a
 * place to stash a user-edited "now" without touching either widget,
 * so this header exposes a single source-of-truth for the wall clock
 * that:
 *
 *  - Remembers the user-set wall time as an epoch-second offset
 *    relative to the boot millis() reference. nowEpoch() then returns
 *    a stable, monotonically-increasing value the rest of the
 *    firmware can adopt incrementally (S55 About, future status-bar
 *    swap, future RTC-backed module).
 *
 *  - Defaults to the same "Thu 2026-01-01 00:00" anchor that
 *    PhoneClockFace::updateDate() already uses, so a clean boot opens
 *    the Date & Time picker on a recognisable date instead of 1970.
 *
 *  - Stays code-only / RAM-only. No SPIFFS, no NVS - the user's
 *    chosen wall time resets on reboot, exactly like a feature phone
 *    that has lost its coin cell. A future session can bind this to
 *    a persistent backing store without breaking the API.
 *
 * The implementation is small enough to live entirely in PhoneClock.cpp,
 * so this header just declares the namespaced free functions.
 */

namespace PhoneClock {

/**
 * Seconds since the unix epoch as currently presented to the user.
 *
 * Equal to:  setEpoch + (millis() - setRefMs) / 1000
 *
 * Where setEpoch defaults to the Thu 2026-01-01 00:00 anchor and
 * setRefMs defaults to 0, so a brand-new boot returns "anchor + uptime"
 * and feels like a clock that started ticking on 2026-01-01.
 */
uint32_t nowEpoch();

/**
 * Replace the wall clock with the supplied epoch second. Subsequent
 * nowEpoch() calls behave as if the device's wall clock was set to
 * `epoch` exactly now and has been ticking since. Re-calling resets
 * the millis() reference so micro-second drift between successive
 * setEpoch() calls does not accumulate.
 */
void setEpoch(uint32_t epoch);

/**
 * Convenience: split nowEpoch() into civil-time fields. month is
 * 1..12, day is 1..31, hour 0..23, minute 0..59, second 0..59,
 * weekday 0..6 (0 = Sunday, matching the DowNames table used by
 * PhoneClockFace and PhoneStatusBar).
 *
 * Returns the same epoch second nowEpoch() would have returned, so
 * callers that need both representations can grab them in one call.
 *
 * Note: ignores leap years for cosmetic display, matching the
 * existing convention in PhoneClockFace::updateDate(). Calendar
 * arithmetic past ~2099 is undefined; the picker clamps year to
 * [2020..2099].
 */
uint32_t now(uint16_t& year, uint8_t& month, uint8_t& day,
			 uint8_t& hour, uint8_t& minute, uint8_t& second,
			 uint8_t& weekday);

/**
 * Build an epoch second from civil-time fields (year/month/day/hour/
 * minute/second). Uses the same leap-year-free calendar as the rest
 * of PhoneClock so a round-trip through now()/buildEpoch() is exact.
 *
 * Inputs outside the valid civil-time ranges are clamped (month
 * 1..12, day 1..daysInMonth(year,month), hour 0..23, minute/second
 * 0..59). Year is clamped to [2020..2099] to keep the math cheap.
 */
uint32_t buildEpoch(uint16_t year, uint8_t month, uint8_t day,
					uint8_t hour, uint8_t minute, uint8_t second);

/**
 * Return the number of days in the supplied (year, month) pair under
 * the leap-year-free calendar. Public so the picker can clamp the
 * day field when the user changes month or year.
 *
 * month is 1..12. Returns 28..31. Out-of-range month is treated as 1.
 */
uint8_t daysInMonth(uint16_t year, uint8_t month);

/**
 * Three-letter all-caps day-of-week name for `weekday` (0=SUN..6=SAT).
 * Returns "" for out-of-range input. Stable pointer (string literal),
 * safe to store on the heap, etc.
 */
const char* weekdayName(uint8_t weekday);

/**
 * Three-letter all-caps month name for `month` (1..12). Returns "" for
 * out-of-range input. Stable pointer.
 */
const char* monthName(uint8_t month);

} // namespace PhoneClock

#endif // MAKERPHONE_PHONECLOCK_H
