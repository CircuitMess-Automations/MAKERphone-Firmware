#include "PhoneClock.h"

namespace {

// Thu 2026-01-01 00:00 expressed in our internal "epoch" - the
// leap-year-free calendar PhoneClockFace already uses, which counts
// every year as exactly 365 days. (2026 - 1970) * 365 * 86400 =
// 1766016000. Note this is NOT the real Unix epoch (which is
// 1767225600 thanks to 14 intervening leap days); we never round-trip
// through real epoch math, so the simpler synthetic anchor is what
// keeps buildEpoch / now / nowEpoch internally consistent.
constexpr uint32_t kAnchorEpoch = 1766016000UL;

// 0=SUN .. 6=SAT, matches PhoneClockFace::updateDate. Thu 2026-01-01
// is weekday 4 under that table.
constexpr uint8_t kAnchorWeekday = 4;

// User-set epoch (or kAnchorEpoch if the user has not set anything yet).
uint32_t setEpoch_  = kAnchorEpoch;
// millis() value captured at the time setEpoch_ was last written.
uint32_t setRefMs_  = 0;

// Days-in-month under the leap-year-free calendar (mirrors
// PhoneClockFace::updateDate so display arithmetic stays consistent).
constexpr uint8_t kMonthDays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

const char* kDowNames[7]  = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
const char* kMonthNames[12] = {
	"JAN","FEB","MAR","APR","MAY","JUN",
	"JUL","AUG","SEP","OCT","NOV","DEC"
};

} // namespace

namespace PhoneClock {

uint32_t nowEpoch() {
	// Compute elapsed wall seconds since the last setEpoch() write. We
	// use a 32-bit difference so millis() rollover (every ~49 days) is
	// handled correctly via unsigned wrap-around.
	uint32_t deltaMs = static_cast<uint32_t>(millis()) - setRefMs_;
	return setEpoch_ + (deltaMs / 1000UL);
}

void setEpoch(uint32_t epoch) {
	setEpoch_ = epoch;
	setRefMs_ = static_cast<uint32_t>(millis());
}

uint8_t daysInMonth(uint16_t /*year*/, uint8_t month) {
	if(month < 1 || month > 12) return kMonthDays[0];
	return kMonthDays[month - 1];
}

const char* weekdayName(uint8_t weekday) {
	if(weekday > 6) return "";
	return kDowNames[weekday];
}

const char* monthName(uint8_t month) {
	if(month < 1 || month > 12) return "";
	return kMonthNames[month - 1];
}

uint32_t now(uint16_t& year, uint8_t& month, uint8_t& day,
			 uint8_t& hour, uint8_t& minute, uint8_t& second,
			 uint8_t& weekday) {
	const uint32_t epoch = nowEpoch();

	// Time-of-day extraction from the seconds-of-day remainder.
	uint32_t timeOfDay = epoch % 86400UL;
	hour    = static_cast<uint8_t>(timeOfDay / 3600UL);
	uint32_t minRem = timeOfDay % 3600UL;
	minute  = static_cast<uint8_t>(minRem / 60UL);
	second  = static_cast<uint8_t>(minRem % 60UL);

	// Calendar walk from the anchor (Thu 2026-01-01). days = whole days
	// elapsed since the anchor. Negative deltas (epoch < kAnchorEpoch)
	// are clamped to zero so a "before-anchor" wall time still renders
	// the anchor date instead of underflowing into garbage.
	uint32_t days;
	if(epoch < kAnchorEpoch) {
		days = 0;
	} else {
		days = (epoch - kAnchorEpoch) / 86400UL;
	}

	weekday = static_cast<uint8_t>((kAnchorWeekday + days) % 7);

	uint16_t y     = 2026;
	uint32_t ordin = days;
	while(true) {
		const uint16_t yearLen = 365; // ignore leap years for cosmetic display
		if(ordin < yearLen) break;
		ordin -= yearLen;
		y++;
		if(y > 2099) {
			// Past our supported range. Pin to 2099-12-31 so the picker
			// has somewhere meaningful to land.
			y     = 2099;
			ordin = 364;
			break;
		}
	}

	uint8_t m = 0;
	while(m < 12 && ordin >= kMonthDays[m]) {
		ordin -= kMonthDays[m];
		m++;
	}
	if(m > 11) m = 11;

	year  = y;
	month = static_cast<uint8_t>(m + 1);    // 1-based for callers
	day   = static_cast<uint8_t>(ordin + 1); // 1-based for callers

	return epoch;
}

uint32_t buildEpoch(uint16_t year, uint8_t month, uint8_t day,
					uint8_t hour, uint8_t minute, uint8_t second) {
	// Clamp every field to its valid range. Belt-and-braces against
	// callers passing partially-edited UI state (e.g. day=31 while the
	// user still has the month at February).
	if(year < 2020) year = 2020;
	if(year > 2099) year = 2099;
	if(month < 1)   month = 1;
	if(month > 12)  month = 12;
	const uint8_t maxDay = kMonthDays[month - 1];
	if(day < 1)        day = 1;
	if(day > maxDay)   day = maxDay;
	if(hour > 23)      hour = 23;
	if(minute > 59)    minute = 59;
	if(second > 59)    second = 59;

	// Whole years past 1970 (leap-year-free, so each year is exactly
	// 365 days). Keeps the math the same as PhoneClockFace.
	uint32_t days = static_cast<uint32_t>(year - 1970) * 365UL;

	// Add days for completed months in `year`.
	for(uint8_t i = 0; i + 1 < month; ++i) {
		days += kMonthDays[i];
	}

	// Day-of-month is 1-based -> 0-based offset into the month.
	days += static_cast<uint32_t>(day - 1);

	const uint32_t timeOfDay =
			static_cast<uint32_t>(hour)   * 3600UL +
			static_cast<uint32_t>(minute) *   60UL +
			static_cast<uint32_t>(second);

	return days * 86400UL + timeOfDay;
}

} // namespace PhoneClock
