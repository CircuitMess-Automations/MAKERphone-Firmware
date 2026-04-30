#include "PhoneClockFace.h"
#include "../Fonts/font.h"
#include <Loop/LoopManager.h>
#include <stdio.h>

// MAKERphone retro palette (kept in sync with PhoneStatusBar / PhoneSoftKeyBar).
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple background (transparent here)
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (date row)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (clock digits)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream (secondary)

PhoneClockFace::PhoneClockFace(lv_obj_t* parent) : LVObject(parent){
	clockLabel = nullptr;
	colonLabel = nullptr;
	dowLabel   = nullptr;
	monthLabel = nullptr;

	// Anchor to the area directly under the status bar regardless of parent layout.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, FaceWidth, FaceHeight);
	// 10 px below the status bar (which is 10 px tall + 1 px separator overlap).
	lv_obj_set_pos(obj, 0, 11);

	// Transparent face - this widget overlays whatever wallpaper / content the
	// host screen has. The host can paint a synthwave gradient behind it later
	// without any changes here.
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);

	buildLabels();

	updateClock();
	updateDate();
	updateColon();

	LoopManager::addListener(this);
}

PhoneClockFace::~PhoneClockFace(){
	LoopManager::removeListener(this);
}

// ----- builders -----

void PhoneClockFace::buildLabels(){
	// Big clock digits. The colon is rendered as its own label so we can blink
	// it independently without re-rendering the surrounding numerals each tick.
	// Layout: hours label right-aligned to the colon, colon centered, minutes
	// label left-aligned to the colon.

	colonLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(colonLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(colonLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(colonLabel, ":");
	lv_obj_set_align(colonLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(colonLabel, 0);

	clockLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(clockLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(clockLabel, MP_HIGHLIGHT, 0);
	// "HH MM" with a single space where the colon will sit; the colon label
	// is drawn on top of that gap. This keeps the digits aligned even if
	// the colon momentarily disappears during the blink.
	lv_label_set_text(clockLabel, "00 00");
	lv_obj_set_align(clockLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(clockLabel, 0);

	// Day-of-week + day row (orange accent so the date pops on dark bg).
	dowLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(dowLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(dowLabel, MP_ACCENT, 0);
	lv_label_set_text(dowLabel, "DAY 00");
	lv_obj_set_align(dowLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(dowLabel, 17);

	// Month + year row (warm cream, less prominent than the day-of-week row).
	monthLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(monthLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(monthLabel, MP_TEXT, 0);
	lv_label_set_text(monthLabel, "MON 0000");
	lv_obj_set_align(monthLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(monthLabel, 24);
}

// ----- updaters -----

void PhoneClockFace::updateClock(){
	// Same uptime-based clock as PhoneStatusBar so the two stay in sync.
	// When a real RTC is wired in, replace this body (and updateDate()).
	uint32_t secs = millis() / 1000UL;
	uint16_t totalMin = (secs / 60UL) % (24UL * 60UL);
	if(totalMin == lastMin) return;
	lastMin = totalMin;

	uint8_t hh = totalMin / 60;
	uint8_t mm = totalMin % 60;
	char buf[8];
	// Two digits, blank where the colon will overlay - keeps the digits
	// from shifting when the colon blinks off.
	snprintf(buf, sizeof(buf), "%02u %02u", (unsigned) hh, (unsigned) mm);
	lv_label_set_text(clockLabel, buf);
}

void PhoneClockFace::updateDate(){
	// No RTC: synthesise a stable fake date from boot uptime so the labels
	// always render something sensible. Once a real time source is wired in,
	// replace the body of this function.
	uint32_t secs   = millis() / 1000UL;
	uint32_t days   = secs / 86400UL;            // whole days since boot
	// Pretend the device booted on Mon 2026-01-01 - a stable anchor so the
	// day-of-week / month progression looks natural during demos.
	const uint16_t baseYear  = 2026;
	const uint8_t  baseDow   = 4; // 0=Sun .. 4=Thu (2026-01-01 is a Thursday)

	uint32_t dow = (baseDow + days) % 7;
	static const char* DowNames[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

	// Days-in-month table (non-leap; close enough for cosmetic display).
	static const uint8_t MonthDays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
	static const char*   MonthNames[12] = {
			"JAN","FEB","MAR","APR","MAY","JUN",
			"JUL","AUG","SEP","OCT","NOV","DEC"
	};

	uint16_t year   = baseYear;
	uint32_t ordin  = days; // ordinal day from base
	while(true){
		uint16_t yearLen = 365; // ignore leap years for cosmetic display
		if(ordin < yearLen) break;
		ordin -= yearLen;
		year++;
	}

	uint8_t month = 0;
	while(month < 12 && ordin >= MonthDays[month]){
		ordin -= MonthDays[month];
		month++;
	}
	if(month > 11) month = 11;

	uint32_t dayKey = ((uint32_t) year) * 512UL + (uint32_t) month * 32UL + (uint32_t) ordin;
	if(dayKey == lastDayKey) return;
	lastDayKey = dayKey;

	char buf1[12];
	char buf2[12];
	snprintf(buf1, sizeof(buf1), "%s %02u", DowNames[dow], (unsigned) (ordin + 1));
	snprintf(buf2, sizeof(buf2), "%s %04u", MonthNames[month], (unsigned) year);
	lv_label_set_text(dowLabel,   buf1);
	lv_label_set_text(monthLabel, buf2);
}

void PhoneClockFace::updateColon(){
	uint32_t now = millis();
	// 1 Hz blink with a 50% duty cycle - cosmetic only.
	if(now - lastBlinkMs < 500) return;
	lastBlinkMs = now;
	colonOn = !colonOn;
	lv_obj_set_style_text_opa(colonLabel, colonOn ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void PhoneClockFace::loop(uint micros){
	updateClock();
	updateDate();
	updateColon();
}

void PhoneClockFace::refresh(){
	lastMin    = 0xFFFF;
	lastDayKey = 0xFFFFFFFFu;
	updateClock();
	updateDate();
}
