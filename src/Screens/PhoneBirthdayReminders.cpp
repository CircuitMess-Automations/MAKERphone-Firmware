#include "PhoneBirthdayReminders.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"
#include "../Storage/Storage.h"
#include "../Storage/PhoneContacts.h"

// MAKERphone retro palette — kept identical to every other Phone*
// widget so the birthday-reminders screen slots in beside
// PhoneCalculator (S60), PhoneAlarmClock (S124), PhoneTimers (S125),
// PhoneCurrencyConverter (S126), PhoneUnitConverter (S127),
// PhoneWorldClock (S128), PhoneVirtualPet (S129), PhoneMagic8Ball
// (S130), PhoneDiceRoller (S131), PhoneCoinFlip (S132),
// PhoneFortuneCookie (S133) and PhoneFlashlight (S134) without a
// visual seam. Same inline-#define convention every other Phone*
// screen .cpp uses.
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption / days-cell
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream (regular row)
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple (sub-caption / hint)
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (today!)

// ---------- geometry --------------------------------------------------
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar
//   y=12..18   "BIRTHDAYS"      caption (pixelbasic7, cyan)
//   y=22..28   "UPCOMING"       sub-caption (pixelbasic7, dim)
//   y=34..104  list rows: up to MaxRows (5) at RowHeight (14) px each
//              (5 * 14 = 70 px, fits in the 70 px strip)
//   y=118..128 PhoneSoftKeyBar
//
// All coordinates centralised here so a future skin only needs to
// tweak these constants.

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kSubCaptionY   = 22;
static constexpr lv_coord_t kListTopY      = 34;
static constexpr lv_coord_t kListLeftX     = 4;
static constexpr lv_coord_t kListWidth     = 152;

// "Empty list" multi-line hint geometry. The pixelbasic7 font is ~7 px
// tall so four lines comfortably fit between the caption strip and
// the soft-key bar.
static constexpr lv_coord_t kEmptyY        = 50;
static constexpr lv_coord_t kEmptyW        = 152;

// ---------- leap-year-free calendar -----------------------------------
//
// Mirrors PhoneClock's internal calendar so the days-until math
// matches the wall clock the rest of the firmware shows. Feb is 28
// days even in leap years — the screen documents this and the
// reminder simply rolls forward to Mar 1 in the rare Feb-29 case.
static constexpr uint8_t  kMonthDays[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static constexpr uint16_t kDaysInYear    = 365;

// Cumulative days-before-month: kCumDays[m] is the day-of-year of
// (month=m+1, day=1) minus 1. Hand-precomputed from kMonthDays so we
// avoid a C++14-style constexpr-loop helper (the build's CMake config
// targets C++11). The asserts in the .cpp keep the two tables in
// lock-step at compile time.
static constexpr uint16_t kCumDays[13] = {
	0,   31,  59,  90, 120, 151, 181,
	212, 243, 273, 304, 334, 365,
};
static_assert(kCumDays[12] == kDaysInYear,
	"kCumDays must end at kDaysInYear");

// ---------- public statics --------------------------------------------

uint16_t PhoneBirthdayReminders::dayOfYear(uint8_t month, uint8_t day) {
	if(month < 1)  month = 1;
	if(month > 12) month = 12;
	if(day   < 1)  day   = 1;
	if(day   > kMonthDays[month - 1]) day = kMonthDays[month - 1];

	// Day-of-year is 1-based; Jan 1 -> 1.
	return kCumDays[month - 1] + day;
}

uint16_t PhoneBirthdayReminders::daysUntil(uint8_t todayMonth, uint8_t todayDay,
                                           uint8_t birthMonth, uint8_t birthDay) {
	const uint16_t today = dayOfYear(todayMonth, todayDay);
	const uint16_t birth = dayOfYear(birthMonth, birthDay);
	if(birth >= today) {
		return birth - today;
	}
	// Birthday already passed this year — reach the next occurrence
	// by wrapping through Dec 31 -> Jan 1.
	return (kDaysInYear - today) + birth;
}

// ---------- ctor / dtor -----------------------------------------------

PhoneBirthdayReminders::PhoneBirthdayReminders()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  subCaptionLabel(nullptr),
		  emptyLabel(nullptr) {

	for(uint8_t i = 0; i < MaxRows; ++i) rowLabels[i] = nullptr;
	for(uint8_t i = 0; i < MaxEntries; ++i) {
		entries[i].uid = 0;
		entries[i].name[0] = 0;
		entries[i].month = 0;
		entries[i].day = 0;
		entries[i].daysUntil = 0;
	}

	// Full-screen container, no scrollbars, no padding — same blank
	// canvas pattern every other Phone* screen uses. Children below
	// are anchored manually to the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildHeader();
	buildRows();
	buildEmptyHint();

	// Bottom soft-key bar. The screen is read-only so only the right
	// (BACK) softkey is wired; the left side is intentionally empty.
	// A future session that adds inline editing can populate "EDIT"
	// here without touching anything else.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");
}

PhoneBirthdayReminders::~PhoneBirthdayReminders() {
	// Children (wallpaper, statusBar, softKeys, captionLabel,
	// subCaptionLabel, rowLabels, emptyLabel) are all parented to
	// `obj` - LVScreen's destructor tears the LVGL tree down for us.
}

// ---------- build helpers ---------------------------------------------

void PhoneBirthdayReminders::buildHeader() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, kListWidth);
	lv_obj_set_pos(captionLabel, kListLeftX, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "BIRTHDAYS");

	subCaptionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(subCaptionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(subCaptionLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(subCaptionLabel, kListWidth);
	lv_obj_set_pos(subCaptionLabel, kListLeftX, kSubCaptionY);
	lv_obj_set_style_text_align(subCaptionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(subCaptionLabel, "UPCOMING");
}

void PhoneBirthdayReminders::buildRows() {
	// MaxRows (5) labels, one per visible slot. Position is fixed —
	// only the text and color are rewritten on cursor / scroll change.
	// pixelbasic7 keeps the row legible at 14 px height with ~3 px of
	// halo above/below the glyphs.
	for(uint8_t i = 0; i < MaxRows; ++i) {
		lv_obj_t* row = lv_label_create(obj);
		lv_obj_set_style_text_font(row, &pixelbasic7, 0);
		lv_obj_set_style_text_color(row, MP_TEXT, 0);
		lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
		lv_obj_set_width(row, kListWidth);
		lv_obj_set_pos(row, kListLeftX, kListTopY + i * RowHeight);
		lv_label_set_text(row, "");
		lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
		rowLabels[i] = row;
	}
}

void PhoneBirthdayReminders::buildEmptyHint() {
	// Multi-line dim-purple hint shown when no contact has a birthday
	// set. Uses LV_LABEL_LONG_WRAP so the four explanatory sentences
	// flow naturally inside the 152 px width.
	emptyLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(emptyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(emptyLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(emptyLabel, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(emptyLabel, kEmptyW);
	lv_obj_set_pos(emptyLabel, kListLeftX, kEmptyY);
	lv_obj_set_style_text_align(emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(emptyLabel,
		"NO BIRTHDAYS YET.\n"
		"SET A BIRTHDAY ON A CONTACT\n"
		"TO SEE IT APPEAR HERE.");
	lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
}

// ---------- lifecycle -------------------------------------------------

void PhoneBirthdayReminders::onStart() {
	Input::getInstance()->addListener(this);
	rebuildList();
	cursor    = 0;
	scrollTop = 0;
	refresh();
}

void PhoneBirthdayReminders::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- list rebuild ----------------------------------------------

void PhoneBirthdayReminders::rebuildList() {
	entryCount = 0;

	// 1) Pull the date-of-today from PhoneClock so the days-until
	//    column matches whatever the wall clock currently shows.
	uint16_t y; uint8_t m, d, hh, mm, ss, wd;
	PhoneClock::now(y, m, d, hh, mm, ss, wd);
	if(m < 1 || m > 12) m = 1;
	if(d < 1 || d > 31) d = 1;
	const uint8_t todayMonth = m;
	const uint8_t todayDay   = d;

	// 2) Walk every PhoneContact record and capture the ones with a
	//    birthday set. The repo currently caps at 32 visible contacts
	//    so MaxEntries (32) covers the realistic ceiling — any
	//    additional entries are silently dropped (and a future session
	//    can grow MaxEntries if needed).
	const auto uids = Storage.PhoneContacts.all();
	for(UID_t uid : uids) {
		if(entryCount >= MaxEntries) break;
		uint8_t bm = 0, bd = 0;
		if(!PhoneContacts::birthdayOf(uid, &bm, &bd)) continue;

		Entry& e = entries[entryCount];
		e.uid       = uid;
		// Display name copy. PhoneContacts::displayNameOf returns into
		// a small static scratch buffer so we copy it out before the
		// next call clobbers it.
		const char* nm = PhoneContacts::displayNameOf(uid);
		strncpy(e.name, nm, sizeof(e.name) - 1);
		e.name[sizeof(e.name) - 1] = 0;
		e.month     = bm;
		e.day       = bd;
		e.daysUntil = daysUntil(todayMonth, todayDay, bm, bd);
		++entryCount;
	}

	// 3) Sort entries by daysUntil ascending; ties break alphabetically
	//    by name. A simple insertion sort is fine — entryCount is
	//    bounded at MaxEntries (32) so the worst case is ~500 compares.
	for(uint8_t i = 1; i < entryCount; ++i) {
		const Entry key = entries[i];
		int8_t j = static_cast<int8_t>(i) - 1;
		while(j >= 0) {
			bool greater = false;
			const Entry& prev = entries[j];
			if(prev.daysUntil > key.daysUntil) {
				greater = true;
			} else if(prev.daysUntil == key.daysUntil) {
				greater = (strncmp(prev.name, key.name, sizeof(prev.name)) > 0);
			}
			if(!greater) break;
			entries[j + 1] = entries[j];
			--j;
		}
		entries[j + 1] = key;
	}
}

// ---------- repaint ---------------------------------------------------

void PhoneBirthdayReminders::refresh() {
	// Empty-state: hide the rows, show the multi-line hint, and bail.
	if(entryCount == 0) {
		for(uint8_t i = 0; i < MaxRows; ++i) {
			if(rowLabels[i] == nullptr) continue;
			lv_obj_add_flag(rowLabels[i], LV_OBJ_FLAG_HIDDEN);
		}
		if(emptyLabel) lv_obj_clear_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
		if(subCaptionLabel) {
			lv_label_set_text(subCaptionLabel, "UPCOMING");
		}
		return;
	}

	if(emptyLabel) lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);

	// Scroll the visible window so the cursor is always on screen.
	updateScroll();

	// Render up to MaxRows entries starting at scrollTop.
	for(uint8_t i = 0; i < MaxRows; ++i) {
		lv_obj_t* row = rowLabels[i];
		if(row == nullptr) continue;

		const uint8_t idx = static_cast<uint8_t>(scrollTop + i);
		if(idx >= entryCount) {
			lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);

		const Entry& e = entries[idx];

		// Format the days-until column.
		//   0  -> "TODAY!"
		//   1  -> "TOMORROW"
		//   N  -> "IN N DAYS"
		char daysStr[16] = {0};
		if(e.daysUntil == 0) {
			snprintf(daysStr, sizeof(daysStr), "TODAY!");
		} else if(e.daysUntil == 1) {
			snprintf(daysStr, sizeof(daysStr), "TOMORROW");
		} else {
			snprintf(daysStr, sizeof(daysStr), "IN %u DAYS",
			         static_cast<unsigned>(e.daysUntil));
		}

		// Date column: "DD MMM" using PhoneClock::monthName for the
		// 3-letter all-caps month abbreviation that matches the rest
		// of the shell.
		const char* mn = PhoneClock::monthName(e.month);
		if(mn == nullptr || mn[0] == 0) mn = "???";

		// Compose: "> NAME           DAYS    DD MMM"
		// pixelbasic7 is monospace-ish at ~7 px per glyph, so a 152
		// px row holds ~21 chars. We truncate the name to 8 chars to
		// leave room for the days + date columns. The cursor mark
		// uses '>' for the focused row, ' ' otherwise.
		const char cursorMark = (idx == cursor) ? '>' : ' ';

		// Truncate name to 8 chars for layout safety. The full name
		// is still preserved in `e.name`; this is purely a render
		// budget. Append '.' if we truncated to hint at overflow.
		char shortName[10] = {0};
		const size_t nameLen = strlen(e.name);
		if(nameLen <= 8) {
			strncpy(shortName, e.name, sizeof(shortName) - 1);
		} else {
			strncpy(shortName, e.name, 7);
			shortName[7] = '.';
			shortName[8] = 0;
		}

		char rowStr[48] = {0};
		snprintf(rowStr, sizeof(rowStr), "%c %-8s %-9s %02u %s",
		         cursorMark,
		         shortName,
		         daysStr,
		         static_cast<unsigned>(e.day),
		         mn);
		lv_label_set_text(row, rowStr);

		// Today!s render in accent so they catch the eye; everything
		// else uses warm cream.
		lv_obj_set_style_text_color(row,
			(e.daysUntil == 0) ? MP_ACCENT : MP_TEXT, 0);
	}

	// Sub-caption switches between "UPCOMING" and "UPCOMING n/N" so
	// the user can tell when entries are below the fold. We keep the
	// short form when everything fits to avoid visual noise.
	if(subCaptionLabel) {
		if(entryCount <= MaxRows) {
			lv_label_set_text(subCaptionLabel, "UPCOMING");
		} else {
			char sub[32];
			snprintf(sub, sizeof(sub), "UPCOMING  %u/%u",
			         static_cast<unsigned>(cursor + 1),
			         static_cast<unsigned>(entryCount));
			lv_label_set_text(subCaptionLabel, sub);
		}
	}
}

void PhoneBirthdayReminders::updateScroll() {
	if(entryCount == 0) {
		scrollTop = 0;
		return;
	}
	if(cursor >= entryCount) cursor = entryCount - 1;

	if(cursor < scrollTop) {
		scrollTop = cursor;
	} else if(cursor >= scrollTop + MaxRows) {
		scrollTop = static_cast<uint8_t>(cursor - (MaxRows - 1));
	}

	// Clamp scrollTop so the last window never extends past the end
	// of the list (e.g. when entries got removed between paints).
	const uint8_t maxTop = (entryCount > MaxRows)
	                       ? static_cast<uint8_t>(entryCount - MaxRows)
	                       : 0;
	if(scrollTop > maxTop) scrollTop = maxTop;
}

// ---------- cursor motion ---------------------------------------------

void PhoneBirthdayReminders::cursorUp() {
	if(entryCount == 0) return;
	if(cursor == 0) {
		cursor = static_cast<uint8_t>(entryCount - 1);
	} else {
		--cursor;
	}
	refresh();
}

void PhoneBirthdayReminders::cursorDown() {
	if(entryCount == 0) return;
	cursor = static_cast<uint8_t>((cursor + 1) % entryCount);
	refresh();
}

// ---------- input -----------------------------------------------------

void PhoneBirthdayReminders::buttonPressed(uint i) {
	switch(i) {
		case BTN_2:
			cursorUp();
			break;
		case BTN_8:
			cursorDown();
			break;

		case BTN_R:
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		case BTN_LEFT:
		case BTN_RIGHT:
		case BTN_L:
		case BTN_ENTER:
		case BTN_0:
		case BTN_1:
		case BTN_3:
		case BTN_4:
		case BTN_5:
		case BTN_6:
		case BTN_7:
		case BTN_9:
			// Read-only screen — absorb every other key so they don't
			// fall through to anything else. A follow-up session that
			// adds an "EDIT" softkey will wire BTN_LEFT / BTN_L /
			// BTN_ENTER without touching the switch above.
			break;

		default:
			break;
	}
}

void PhoneBirthdayReminders::buttonReleased(uint i) {
	switch(i) {
		case BTN_BACK:
			if(!backLongFired) {
				pop();
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneBirthdayReminders::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK = short tap = exit. The flag suppresses
			// the matching short-press fire-back on release.
			backLongFired = true;
			pop();
			break;

		default:
			break;
	}
}
