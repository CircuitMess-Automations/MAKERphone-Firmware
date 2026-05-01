#include "PhoneDateTimeScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneBrightnessScreen.cpp / PhoneSoundScreen.cpp /
// PhoneSettingsScreen.cpp). Cyan for the caption + idle field digits
// (informational), sunset orange for the focused field (so the user
// always knows which value the +/- keys will adjust), warm cream for
// the separators ("-" / ":"), dim purple for the hint caption, and
// MP_ACCENT used as the weekday tag color so the day-of-week reads
// as a secondary accent instead of competing with the active field.
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (focused field, weekday)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan (caption + idle fields)
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (separators)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple (hint)

// Layout pixel offsets, hand-tuned for the 160x128 viewport so every
// row sits cleanly between the status bar (y=0..10) and the soft-key
// bar (y=118..128).
static constexpr lv_coord_t kCaptionY  = 12;
static constexpr lv_coord_t kDateRowY  = 26;
static constexpr lv_coord_t kWeekdayY  = 44;
static constexpr lv_coord_t kTimeRowY  = 58;
static constexpr lv_coord_t kHintY     = 82;

// Flex-row geometry. The rows host a handful of labels each; each row
// is sized just slightly wider than the digits + separators it
// contains so flex centring keeps the whole row visually centred on
// the 160 px display.
static constexpr lv_coord_t kDateRowW  = 110;
static constexpr lv_coord_t kDateRowH  = 16;
static constexpr lv_coord_t kTimeRowW  = 60;
static constexpr lv_coord_t kTimeRowH  = 16;

PhoneDateTimeScreen::PhoneDateTimeScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  dateRow(nullptr),
		  timeRow(nullptr),
		  weekdayLabel(nullptr),
		  hintLabel(nullptr),
		  yearLabel(nullptr),
		  dateSep1(nullptr),
		  monthLabel(nullptr),
		  dateSep2(nullptr),
		  dayLabel(nullptr),
		  hourLabel(nullptr),
		  timeColon(nullptr),
		  minuteLabel(nullptr),
		  year(2026), month(1), day(1), hour(0), minute(0),
		  initialEpoch(0),
		  cursor(0) {

	// Snapshot the wall clock as PhoneClock currently sees it. The
	// editor walks the user through Year/Month/Day/Hour/Min over a copy
	// of these fields and only commits on SAVE - BACK reverts to this
	// snapshot.
	uint8_t  sec = 0;
	uint8_t  wd  = 0;
	initialEpoch = PhoneClock::now(year, month, day, hour, minute, sec, wd);

	// Defensive: PhoneClock::now() may return a year past our supported
	// editor range if a future caller pushes the wall clock that far.
	// Snap into [YearMin..YearMax] before we lay anything out so the
	// editor never opens on a year it cannot decrement to.
	if(year < YearMin) year = YearMin;
	if(year > YearMax) year = YearMax;
	clampDayToMonth();

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneBrightnessScreen / PhoneSoundScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the digit rows overlay
	// it cleanly. Every other Phase-J screen uses Synthwave here too.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Standard signal | clock | battery bar so the user keeps the
	// usual phone chrome while editing.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildDateRow();
	buildWeekday();
	buildTimeRow();
	buildHint();

	// Bottom: SAVE on the left, BACK on the right - matches the
	// Sony-Ericsson convention for option screens (commit / discard).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("SAVE");
	softKeys->setRight("BACK");

	// Initial paint: render every value label + colour the focused one.
	refreshDisplay();
}

PhoneDateTimeScreen::~PhoneDateTimeScreen() {
	// All children (wallpaper, statusBar, softKeys, labels, rows) are
	// parented to obj - LVGL frees them recursively when the screen's
	// obj is destroyed by the LVScreen base destructor.
}

void PhoneDateTimeScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneDateTimeScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders --------------------------------------------------------

void PhoneDateTimeScreen::buildCaption() {
	// "DATE & TIME" caption in pixelbasic7 cyan, just under the status
	// bar - same anchor pattern PhoneBrightnessScreen / PhoneSoundScreen
	// / PhoneWallpaperScreen all use, so the screen feels like a member
	// of the same family.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "DATE & TIME");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
}

void PhoneDateTimeScreen::buildDateRow() {
	// Transparent row container. Flex-row with 1 px column gap so the
	// five children read as "2026-01-15" without extra horizontal
	// spacing. Row anchored to the top-mid so it visually centres on
	// the 160 px viewport regardless of label widths.
	dateRow = lv_obj_create(obj);
	lv_obj_remove_style_all(dateRow);
	lv_obj_set_size(dateRow, kDateRowW, kDateRowH);
	lv_obj_set_align(dateRow, LV_ALIGN_TOP_MID);
	lv_obj_set_y(dateRow, kDateRowY);
	lv_obj_set_scrollbar_mode(dateRow, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(dateRow, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(dateRow, 0, 0);
	lv_obj_set_flex_flow(dateRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(dateRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(dateRow, 1, 0);

	// Year (4-char) field. Initial color cyan; refreshDisplay() will
	// flip the focused field to MP_ACCENT.
	yearLabel = lv_label_create(dateRow);
	lv_obj_set_style_text_font(yearLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(yearLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(yearLabel, "0000");

	// "-" separator. Warm cream so it reads as static punctuation
	// rather than competing for attention with the digit fields.
	dateSep1 = lv_label_create(dateRow);
	lv_obj_set_style_text_font(dateSep1, &pixelbasic16, 0);
	lv_obj_set_style_text_color(dateSep1, MP_TEXT, 0);
	lv_label_set_text(dateSep1, "-");

	// Month (2-char) field.
	monthLabel = lv_label_create(dateRow);
	lv_obj_set_style_text_font(monthLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(monthLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(monthLabel, "00");

	dateSep2 = lv_label_create(dateRow);
	lv_obj_set_style_text_font(dateSep2, &pixelbasic16, 0);
	lv_obj_set_style_text_color(dateSep2, MP_TEXT, 0);
	lv_label_set_text(dateSep2, "-");

	// Day (2-char) field.
	dayLabel = lv_label_create(dateRow);
	lv_obj_set_style_text_font(dayLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(dayLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(dayLabel, "00");
}

void PhoneDateTimeScreen::buildWeekday() {
	// Weekday tag below the date row. Sunset orange so it reads as a
	// secondary accent - the user can scrub the date and watch the
	// weekday update under their finger.
	weekdayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(weekdayLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(weekdayLabel, MP_ACCENT, 0);
	lv_label_set_text(weekdayLabel, "");
	lv_obj_set_align(weekdayLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(weekdayLabel, kWeekdayY);
}

void PhoneDateTimeScreen::buildTimeRow() {
	// Mirror of dateRow for the HH : MM field group.
	timeRow = lv_obj_create(obj);
	lv_obj_remove_style_all(timeRow);
	lv_obj_set_size(timeRow, kTimeRowW, kTimeRowH);
	lv_obj_set_align(timeRow, LV_ALIGN_TOP_MID);
	lv_obj_set_y(timeRow, kTimeRowY);
	lv_obj_set_scrollbar_mode(timeRow, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(timeRow, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(timeRow, 0, 0);
	lv_obj_set_flex_flow(timeRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(timeRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(timeRow, 1, 0);

	hourLabel = lv_label_create(timeRow);
	lv_obj_set_style_text_font(hourLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(hourLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hourLabel, "00");

	timeColon = lv_label_create(timeRow);
	lv_obj_set_style_text_font(timeColon, &pixelbasic16, 0);
	lv_obj_set_style_text_color(timeColon, MP_TEXT, 0);
	lv_label_set_text(timeColon, ":");

	minuteLabel = lv_label_create(timeRow);
	lv_obj_set_style_text_font(minuteLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(minuteLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(minuteLabel, "00");
}

void PhoneDateTimeScreen::buildHint() {
	// Two-action hint: which buttons step the cursor between fields,
	// which buttons step the focused field's value. Same dim-purple
	// tone as the hint labels in PhoneBrightnessScreen / PhoneSoundScreen
	// so the screens read as a family.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "L/R: field   2/8: value");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, kHintY);
}

// ----- live updates ----------------------------------------------------

void PhoneDateTimeScreen::refreshDisplay() {
	// Format every value label. snprintf into small stack buffers is
	// plenty - lv_label_set_text copies into its internal store, so
	// the buffer is free to go out of scope immediately.
	char buf[8];

	if(yearLabel) {
		snprintf(buf, sizeof(buf), "%04u", static_cast<unsigned>(year));
		lv_label_set_text(yearLabel, buf);
	}
	if(monthLabel) {
		snprintf(buf, sizeof(buf), "%02u", static_cast<unsigned>(month));
		lv_label_set_text(monthLabel, buf);
	}
	if(dayLabel) {
		snprintf(buf, sizeof(buf), "%02u", static_cast<unsigned>(day));
		lv_label_set_text(dayLabel, buf);
	}
	if(hourLabel) {
		snprintf(buf, sizeof(buf), "%02u", static_cast<unsigned>(hour));
		lv_label_set_text(hourLabel, buf);
	}
	if(minuteLabel) {
		snprintf(buf, sizeof(buf), "%02u", static_cast<unsigned>(minute));
		lv_label_set_text(minuteLabel, buf);
	}

	// Recolour every value label - sunset orange for the focused field,
	// cyan for everyone else. One style call per label is plenty cheap.
	struct FieldEntry {
		uint8_t   index;       // 0..FieldCount-1
		lv_obj_t* label;
	};
	FieldEntry entries[FieldCount] = {
		{ static_cast<uint8_t>(Field::Year),   yearLabel   },
		{ static_cast<uint8_t>(Field::Month),  monthLabel  },
		{ static_cast<uint8_t>(Field::Day),    dayLabel    },
		{ static_cast<uint8_t>(Field::Hour),   hourLabel   },
		{ static_cast<uint8_t>(Field::Minute), minuteLabel },
	};
	for(uint8_t i = 0; i < FieldCount; ++i) {
		if(entries[i].label == nullptr) continue;
		lv_obj_set_style_text_color(entries[i].label,
									(entries[i].index == cursor) ? MP_ACCENT : MP_HIGHLIGHT,
									0);
	}

	// Weekday tag follows the (year, month, day) trio. We synthesise a
	// matching epoch via PhoneClock::buildEpoch() and then read the
	// weekday back via PhoneClock::now() so the leap-year-free calendar
	// math lives in exactly one place.
	if(weekdayLabel) {
		// Recompute the weekday from a synthesised "midnight of the
		// edited date" epoch. Cast both sides of the diff to int32_t so
		// dates earlier than the 2026-01-01 anchor produce a negative
		// day count instead of an unsigned underflow. Fold the signed
		// day count back into [0..6] with the standard ((x % 7) + 7) % 7
		// trick, then add the anchor weekday (THU=4) to get the final
		// 0=SUN..6=SAT index.
		const uint32_t trial   = PhoneClock::buildEpoch(year, month, day, 0, 0, 0);
		const uint32_t kAnchor = PhoneClock::buildEpoch(2026, 1, 1, 0, 0, 0);
		const int32_t  diffSec = static_cast<int32_t>(trial) - static_cast<int32_t>(kAnchor);
		const int32_t  diffDay = diffSec / 86400;
		int32_t        wdSigned = (4 + diffDay) % 7;
		if(wdSigned < 0) wdSigned += 7;
		lv_label_set_text(weekdayLabel,
			PhoneClock::weekdayName(static_cast<uint8_t>(wdSigned)));
	}
}

void PhoneDateTimeScreen::clampDayToMonth() {
	const uint8_t maxDay = PhoneClock::daysInMonth(year, month);
	if(day < 1)        day = 1;
	if(day > maxDay)   day = maxDay;
}

void PhoneDateTimeScreen::moveCursorBy(int8_t delta) {
	if(FieldCount == 0) return;
	int16_t next = static_cast<int16_t>(cursor) + delta;
	while(next < 0)                                  next += FieldCount;
	while(next >= static_cast<int16_t>(FieldCount))  next -= FieldCount;
	cursor = static_cast<uint8_t>(next);
	refreshDisplay();
}

void PhoneDateTimeScreen::adjustValueBy(int8_t delta) {
	switch(static_cast<Field>(cursor)) {
		case Field::Year: {
			// Hard clamp at the editor edges. Wrapping a 4-digit year
			// would feel weird (1999 -> 2099) so we hold the user at
			// the edge instead.
			int32_t next = static_cast<int32_t>(year) + delta;
			if(next < static_cast<int32_t>(YearMin)) next = YearMin;
			if(next > static_cast<int32_t>(YearMax)) next = YearMax;
			year = static_cast<uint16_t>(next);
			clampDayToMonth();
			break;
		}
		case Field::Month: {
			// Wrap 1..12. Stepping past December rolls into January,
			// which matches Sony-Ericsson behaviour and keeps the
			// editor responsive when the user is far from the target
			// month.
			int8_t next = static_cast<int8_t>(month) + delta;
			while(next < 1)  next += 12;
			while(next > 12) next -= 12;
			month = static_cast<uint8_t>(next);
			clampDayToMonth();
			break;
		}
		case Field::Day: {
			const uint8_t maxDay = PhoneClock::daysInMonth(year, month);
			int8_t next = static_cast<int8_t>(day) + delta;
			while(next < 1)                              next += maxDay;
			while(next > static_cast<int8_t>(maxDay))    next -= maxDay;
			day = static_cast<uint8_t>(next);
			break;
		}
		case Field::Hour: {
			int8_t next = static_cast<int8_t>(hour) + delta;
			while(next < 0)  next += 24;
			while(next > 23) next -= 24;
			hour = static_cast<uint8_t>(next);
			break;
		}
		case Field::Minute: {
			int8_t next = static_cast<int8_t>(minute) + delta;
			while(next < 0)  next += 60;
			while(next > 59) next -= 60;
			minute = static_cast<uint8_t>(next);
			break;
		}
	}
	refreshDisplay();
}

void PhoneDateTimeScreen::saveAndExit() {
	// Commit the edited civil-time fields through the wall-clock service.
	// Seconds are pinned to 0 so the user-visible "minute boundary" lines
	// up with what they just selected - feature-phone time-set screens
	// have always behaved this way.
	const uint32_t epoch = PhoneClock::buildEpoch(year, month, day, hour, minute, 0);
	PhoneClock::setEpoch(epoch);
	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneDateTimeScreen::cancelAndExit() {
	// Revert to whatever PhoneClock had on entry. Re-setting the
	// epoch (instead of leaving it untouched) protects against any
	// future code that might grab a reference into PhoneClock state
	// during the lifetime of this screen.
	PhoneClock::setEpoch(initialEpoch);
	if(softKeys) softKeys->flashRight();
	pop();
}

void PhoneDateTimeScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			// Field cursor: previous field. BTN_LEFT and BTN_4 both
			// map to "previous" so the user can drive the screen with
			// either the d-pad or the numpad column.
			moveCursorBy(-1);
			break;

		case BTN_RIGHT:
		case BTN_6:
			moveCursorBy(+1);
			break;

		case BTN_2:
			// Value: increment focused field. BTN_2 sits at the top of
			// the numpad column so "up" -> "increase" feels natural.
			adjustValueBy(+1);
			break;

		case BTN_8:
			// Value: decrement focused field.
			adjustValueBy(-1);
			break;

		case BTN_ENTER:
			saveAndExit();
			break;

		case BTN_BACK:
			cancelAndExit();
			break;

		default:
			break;
	}
}
