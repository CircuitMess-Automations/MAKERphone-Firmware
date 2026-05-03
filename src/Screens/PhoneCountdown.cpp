#include "PhoneCountdown.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <nvs.h>
#include <esp_log.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneT9Input.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

// MAKERphone retro palette — kept identical to every other Phone*
// widget so the countdown screen slots in beside PhoneTodo (S136),
// PhoneHabits (S137), PhonePomodoro (S138), PhoneMoodLog (S139),
// PhoneScratchpad (S140), PhoneExpenses (S141) and the rest of the
// Phase-Q family without a visual seam.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (today / focused field)
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption / future
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple (dividers)
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream (regular row)
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple (past / hints)

// ---------- list-view geometry ------------------------------------------
//
// 160 x 128 layout. Vertical budget for List view:
//   y =  0..  9   PhoneStatusBar (10 px)
//   y = 12        caption "COUNTDOWN N/M" (pixelbasic7)
//   y = 22        top divider rule (1 px)
//   y = 26        row 0 (14 px row stride)
//   y = 40        row 1
//   y = 54        row 2
//   y = 68        row 3
//   y = 82        row 4
//   y = 98        bottom divider rule (1 px)
//   y = 118..127  PhoneSoftKeyBar (10 px)

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kTopDividerY  = 22;
static constexpr lv_coord_t kRowsTopY     = 26;
static constexpr lv_coord_t kBotDividerY  = 98;
static constexpr lv_coord_t kRowLeftX     = 4;
static constexpr lv_coord_t kRowWidth     = 152;
static constexpr lv_coord_t kEmptyHintY   = 38;
static constexpr lv_coord_t kEmptyHintW   = 152;

// ---------- edit-view geometry ------------------------------------------
//
//   y = 0..9      PhoneStatusBar (10 px)
//   y = 12        caption "NEW EVENT" / "EDIT EVENT"
//   y = 24        PhoneT9Input (Width x (Height + HelpHeight) = 156 x 30)
//   y = 60        date row "2026 - 12 - 25"
//   y = 80        hint strip
//   y = 118..127  PhoneSoftKeyBar
static constexpr lv_coord_t kEditCaptionY = 12;
static constexpr lv_coord_t kEditT9Y      = 24;
static constexpr lv_coord_t kEditDateY    = 60;
static constexpr lv_coord_t kEditHintY    = 80;
static constexpr lv_coord_t kEditHintW    = 152;

// ---------- leap-year-free calendar -------------------------------------

static constexpr uint8_t  kMonthDays[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static constexpr uint16_t kCumDays[13] = {
	0,   31,  59,  90, 120, 151, 181,
	212, 243, 273, 304, 334, 365,
};
static constexpr uint16_t kDaysInYear = 365;
static_assert(kCumDays[12] == kDaysInYear, "kCumDays must end at kDaysInYear");

// ---------- NVS persistence ---------------------------------------------

namespace {

constexpr const char* kNamespace = "mpcd";
constexpr const char* kBlobKey   = "e";

constexpr uint8_t kMagic0  = 'M';
constexpr uint8_t kMagic1  = 'P';
constexpr uint8_t kVersion = 1;

constexpr size_t kHeaderBytes      = 4;
constexpr size_t kEventHeaderBytes = 5;  // year(2) + month(1) + day(1) + nameLen(1)

nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneCountdown",
		         "nvs_open(%s) failed: %d -- countdown runs without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

uint8_t clampMonth(uint8_t m) {
	if(m < 1)  return 1;
	if(m > 12) return 12;
	return m;
}

uint8_t clampDay(uint8_t m, uint8_t d) {
	const uint8_t mc = clampMonth(m);
	if(d < 1) return 1;
	if(d > kMonthDays[mc - 1]) return kMonthDays[mc - 1];
	return d;
}

uint16_t clampYear(uint16_t y) {
	if(y < PhoneCountdown::YearMin) return PhoneCountdown::YearMin;
	if(y > PhoneCountdown::YearMax) return PhoneCountdown::YearMax;
	return y;
}

const char* kMonthNames[12] = {
	"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
	"JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

const char* monthShort(uint8_t m) {
	if(m < 1 || m > 12) return "???";
	return kMonthNames[m - 1];
}

} // namespace

// ---------- public statics ----------------------------------------------

int32_t PhoneCountdown::daysSinceEpoch(uint16_t year, uint8_t month, uint8_t day) {
	const uint16_t y = clampYear(year);
	const uint8_t  m = clampMonth(month);
	const uint8_t  d = clampDay(m, day);
	const int32_t years = static_cast<int32_t>(y) - static_cast<int32_t>(YearMin);
	return years * static_cast<int32_t>(kDaysInYear)
	     + static_cast<int32_t>(kCumDays[m - 1])
	     + static_cast<int32_t>(d) - 1;
}

int32_t PhoneCountdown::daysUntilDate(uint16_t todayYear, uint8_t todayMonth, uint8_t todayDay,
                                      uint16_t evtYear,   uint8_t evtMonth,   uint8_t evtDay) {
	const int32_t today = daysSinceEpoch(todayYear, todayMonth, todayDay);
	const int32_t evt   = daysSinceEpoch(evtYear,   evtMonth,   evtDay);
	return evt - today;
}

void PhoneCountdown::trimText(const char* in, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(in == nullptr) {
		out[0] = '\0';
		return;
	}
	while(*in != '\0' && isspace((unsigned char)*in)) ++in;
	const char* end = in + strlen(in);
	while(end > in && isspace((unsigned char)*(end - 1))) --end;
	const size_t srcLen  = (size_t)(end - in);
	const size_t copyLen = (srcLen < outLen - 1) ? srcLen : (outLen - 1);
	memcpy(out, in, copyLen);
	out[copyLen] = '\0';
}

// ---------- ctor / dtor -------------------------------------------------

PhoneCountdown::PhoneCountdown()
		: LVScreen() {

	for(uint8_t i = 0; i < MaxEvents; ++i) {
		events[i].name[0] = '\0';
		events[i].year    = YearMin;
		events[i].month   = 1;
		events[i].day     = 1;
	}
	eventCount = 0;

	for(uint8_t i = 0; i < MaxEvents; ++i) {
		sorted[i].slot      = 0;
		sorted[i].daysUntil = 0;
	}

	// Try to load persisted events. Failure leaves the list empty.
	load();

	// Full-screen blank canvas.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);
	softKeys  = new PhoneSoftKeyBar(obj);

	buildListView();

	setButtonHoldTime(BTN_BACK, BackHoldMs);

	enterList();
}

PhoneCountdown::~PhoneCountdown() {
	teardownEditView();
	// All other children are parented to obj and freed by the LVScreen
	// destructor.
}

// ---------- lifecycle ---------------------------------------------------

void PhoneCountdown::onStart() {
	Input::getInstance()->addListener(this);
	cacheToday();
	if(mode == Mode::List) {
		rebuildSorted();
		// Keep cursor in range — events may have been added or
		// removed between sessions.
		if(eventCount == 0) {
			cursor = 0;
		} else if(cursor >= eventCount) {
			cursor = (uint8_t)(eventCount - 1);
		}
		refreshCaption();
		refreshRows();
		refreshSoftKeys();
		refreshEmptyHint();
	}
}

void PhoneCountdown::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- read-only accessors -----------------------------------------

const char* PhoneCountdown::getEventName(uint8_t slot) const {
	if(slot >= eventCount) return "";
	return events[slot].name;
}

uint16_t PhoneCountdown::getEventYear(uint8_t slot) const {
	if(slot >= eventCount) return YearMin;
	return events[slot].year;
}

uint8_t PhoneCountdown::getEventMonth(uint8_t slot) const {
	if(slot >= eventCount) return 1;
	return events[slot].month;
}

uint8_t PhoneCountdown::getEventDay(uint8_t slot) const {
	if(slot >= eventCount) return 1;
	return events[slot].day;
}

// ---------- builders ----------------------------------------------------

void PhoneCountdown::buildListView() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "COUNTDOWN  0/8");

	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	for(uint8_t i = 0; i < VisibleRows; ++i) {
		lv_obj_t* row = lv_label_create(obj);
		lv_obj_set_style_text_font(row, &pixelbasic7, 0);
		lv_obj_set_style_text_color(row, MP_TEXT, 0);
		lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
		lv_obj_set_width(row, kRowWidth);
		lv_obj_set_pos(row, kRowLeftX, kRowsTopY + i * RowHeight);
		lv_label_set_text(row, "");
		rowLabels[i] = row;
	}

	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);

	emptyHint = lv_label_create(obj);
	lv_obj_set_style_text_font(emptyHint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(emptyHint, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(emptyHint, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(emptyHint, kEmptyHintW);
	lv_obj_set_style_text_align(emptyHint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(emptyHint, kRowLeftX, kEmptyHintY);
	lv_label_set_text(emptyHint,
		"NO COUNTDOWNS YET.\n"
		"PRESS \"NEW\" TO ADD\n"
		"AN EVENT TO COUNT DOWN.");
	lv_obj_add_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
}

void PhoneCountdown::buildEditView() {
	editCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(editCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(editCaption, MP_HIGHLIGHT, 0);
	lv_obj_set_align(editCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(editCaption, kEditCaptionY);
	lv_label_set_text(editCaption, "NEW EVENT");

	t9Input = new PhoneT9Input(obj, MaxNameLen);
	lv_obj_set_pos(t9Input->getLvObj(),
	               (160 - PhoneT9Input::Width) / 2,
	               kEditT9Y);
	t9Input->setPlaceholder("EVENT NAME");
	t9Input->setCase(PhoneT9Input::Case::Upper);

	editDateLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(editDateLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(editDateLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(editDateLabel, kEditHintW);
	lv_obj_set_pos(editDateLabel, kRowLeftX, kEditDateY);
	lv_obj_set_style_text_align(editDateLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(editDateLabel, "2026 - 01 - 01");

	editHint = lv_label_create(obj);
	lv_obj_set_style_text_font(editHint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(editHint, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(editHint, LV_LABEL_LONG_DOT);
	lv_obj_set_width(editHint, kEditHintW);
	lv_obj_set_pos(editHint, kRowLeftX, kEditHintY);
	lv_obj_set_style_text_align(editHint, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(editHint, "ENTER NAME, THEN NEXT");
}

void PhoneCountdown::teardownEditView() {
	if(t9Input != nullptr) {
		delete t9Input;
		t9Input = nullptr;
	}
	if(editCaption != nullptr) {
		lv_obj_del(editCaption);
		editCaption = nullptr;
	}
	if(editDateLabel != nullptr) {
		lv_obj_del(editDateLabel);
		editDateLabel = nullptr;
	}
	if(editHint != nullptr) {
		lv_obj_del(editHint);
		editHint = nullptr;
	}
}

// ---------- repainters --------------------------------------------------

void PhoneCountdown::refreshCaption() {
	if(captionLabel == nullptr) return;
	char buf[24];
	snprintf(buf, sizeof(buf), "COUNTDOWN  %u/%u",
	         (unsigned)eventCount,
	         (unsigned)MaxEvents);
	lv_label_set_text(captionLabel, buf);
}

void PhoneCountdown::refreshRows() {
	if(eventCount == 0) {
		for(uint8_t i = 0; i < VisibleRows; ++i) {
			if(rowLabels[i] == nullptr) continue;
			lv_label_set_text(rowLabels[i], "");
			lv_obj_add_flag(rowLabels[i], LV_OBJ_FLAG_HIDDEN);
		}
		return;
	}

	// Compute scroll origin so the cursor row stays in view.
	uint8_t scrollTop = 0;
	if(cursor >= VisibleRows) {
		scrollTop = (uint8_t)(cursor - (VisibleRows - 1));
	}
	const uint8_t maxScroll = (eventCount > VisibleRows)
	                          ? (uint8_t)(eventCount - VisibleRows) : 0;
	if(scrollTop > maxScroll) scrollTop = maxScroll;

	for(uint8_t r = 0; r < VisibleRows; ++r) {
		lv_obj_t* row = rowLabels[r];
		if(row == nullptr) continue;

		const uint8_t sortedIdx = (uint8_t)(scrollTop + r);
		if(sortedIdx >= eventCount) {
			lv_label_set_text(row, "");
			lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);

		const SortedRow& sr = sorted[sortedIdx];
		const Event&     e  = events[sr.slot];

		// Days column.
		char daysStr[16] = {0};
		if(sr.daysUntil == 0) {
			snprintf(daysStr, sizeof(daysStr), "TODAY!");
		} else if(sr.daysUntil == 1) {
			snprintf(daysStr, sizeof(daysStr), "TOMORROW");
		} else if(sr.daysUntil == -1) {
			snprintf(daysStr, sizeof(daysStr), "YESTERDAY");
		} else if(sr.daysUntil > 1) {
			snprintf(daysStr, sizeof(daysStr), "IN %ld D",
			         (long)sr.daysUntil);
		} else {
			// daysUntil <= -2
			snprintf(daysStr, sizeof(daysStr), "%ld D AGO",
			         (long)(-sr.daysUntil));
		}

		// Date column "DD MMM".
		const char* mn = monthShort(e.month);

		// Truncate name to 8 chars to leave room for days + date
		// columns. The full name is preserved in events[]; this is
		// just a render budget.
		char shortName[10] = {0};
		const size_t nameLen = strnlen(e.name, MaxNameLen);
		if(nameLen <= 8) {
			memcpy(shortName, e.name, nameLen);
			shortName[nameLen] = '\0';
		} else {
			memcpy(shortName, e.name, 7);
			shortName[7] = '.';
			shortName[8] = '\0';
		}

		const char cursorMark = (sortedIdx == cursor) ? '>' : ' ';

		char rowStr[64] = {0};
		snprintf(rowStr, sizeof(rowStr), "%c%-8s %-9s %02u %s",
		         cursorMark,
		         shortName,
		         daysStr,
		         (unsigned)e.day,
		         mn);

		// Defensive: strip stray newlines so a row stays a single
		// visual line.
		for(size_t j = 0; rowStr[j] != '\0'; ++j) {
			if(rowStr[j] == '\n' || rowStr[j] == '\r') rowStr[j] = ' ';
		}
		lv_label_set_text(row, rowStr);

		// Colour key:
		//   sr.daysUntil == 0  -> accent (today)
		//   sr.daysUntil  > 0  -> highlight cyan when cursor, cream otherwise
		//   sr.daysUntil  < 0  -> dim purple (past)
		lv_color_t color;
		if(sr.daysUntil == 0) {
			color = MP_ACCENT;
		} else if(sr.daysUntil < 0) {
			color = MP_LABEL_DIM;
		} else if(sortedIdx == cursor) {
			color = MP_HIGHLIGHT;
		} else {
			color = MP_TEXT;
		}
		lv_obj_set_style_text_color(row, color, 0);
	}
}

void PhoneCountdown::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	if(mode == Mode::List) {
		softKeys->setLeft("NEW");
		if(eventCount == 0) {
			softKeys->setRight("BACK");
		} else {
			softKeys->setRight("EDIT");
		}
		return;
	}
	// Edit mode.
	if(editStage == EditStage::Name) {
		softKeys->setLeft("NEXT");
		softKeys->setRight("BACK");
	} else {
		softKeys->setLeft("SAVE");
		softKeys->setRight("BACK");
	}
}

void PhoneCountdown::refreshEmptyHint() {
	if(emptyHint == nullptr) return;
	if(eventCount == 0 && mode == Mode::List) {
		lv_obj_clear_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneCountdown::refreshEditCaption() {
	if(editCaption == nullptr) return;
	lv_label_set_text(editCaption, editingNew ? "NEW EVENT" : "EDIT EVENT");
}

void PhoneCountdown::refreshEditDate() {
	if(editDateLabel == nullptr) return;
	char buf[32];
	// Brackets surround the focused field so the user has a clear
	// "this is the active value" cue even on a monochrome theme.
	const bool y = (dateField == DateField::Year);
	const bool m = (dateField == DateField::Month);
	const bool d = (dateField == DateField::Day);
	snprintf(buf, sizeof(buf),
	         "%c%04u%c - %c%02u%c - %c%02u%c",
	         y ? '[' : ' ', (unsigned)stagedYear,  y ? ']' : ' ',
	         m ? '[' : ' ', (unsigned)stagedMonth, m ? ']' : ' ',
	         d ? '[' : ' ', (unsigned)stagedDay,   d ? ']' : ' ');
	lv_label_set_text(editDateLabel, buf);

	// Tint the whole row in accent while we're focused on the date
	// stage; cyan when we're still in the Name stage so the user
	// reads the date as "preview only".
	lv_obj_set_style_text_color(editDateLabel,
		(editStage == EditStage::Date) ? MP_ACCENT : MP_HIGHLIGHT, 0);
}

void PhoneCountdown::refreshEditHint() {
	if(editHint == nullptr) return;
	if(editStage == EditStage::Name) {
		lv_label_set_text(editHint, "ENTER NAME, THEN NEXT");
	} else {
		lv_label_set_text(editHint, "L/R FIELD  2/8 +/-");
	}
}

// ---------- model helpers -----------------------------------------------

void PhoneCountdown::cacheToday() {
	uint16_t y; uint8_t m, d, hh, mm, ss, wd;
	PhoneClock::now(y, m, d, hh, mm, ss, wd);
	if(m < 1 || m > 12) m = 1;
	if(d < 1 || d > 31) d = 1;
	todayYear  = clampYear(y);
	todayMonth = m;
	todayDay   = d;
}

void PhoneCountdown::rebuildSorted() {
	// Snapshot every saved event as a sortable row.
	for(uint8_t i = 0; i < eventCount; ++i) {
		sorted[i].slot      = i;
		sorted[i].daysUntil = daysUntilDate(
			todayYear, todayMonth, todayDay,
			events[i].year, events[i].month, events[i].day);
	}

	// Insertion sort. Order:
	//   future-or-today (daysUntil >= 0): ascending by daysUntil.
	//   past            (daysUntil  < 0): descending by daysUntil
	//                                      (most recently past first).
	//   future before past as a class.
	//   ties break alphabetically by name (case-insensitive).
	for(uint8_t i = 1; i < eventCount; ++i) {
		const SortedRow key = sorted[i];
		int8_t j = (int8_t)i - 1;
		while(j >= 0) {
			const SortedRow& prev = sorted[j];

			bool greater = false;
			const bool prevPast = (prev.daysUntil < 0);
			const bool keyPast  = (key.daysUntil  < 0);

			if(prevPast != keyPast) {
				// Future class (daysUntil >= 0) sorts before past class.
				greater = prevPast;  // past should NOT come before future
			} else if(!prevPast) {
				// Both future-or-today: ascending daysUntil.
				if(prev.daysUntil > key.daysUntil) {
					greater = true;
				} else if(prev.daysUntil == key.daysUntil) {
					greater = strncmp(events[prev.slot].name,
					                     events[key.slot].name, MaxNameLen) > 0;
				}
			} else {
				// Both past: more-recent (closer to zero) first ->
				// daysUntil greater (less negative) goes earlier.
				if(prev.daysUntil < key.daysUntil) {
					greater = true;
				} else if(prev.daysUntil == key.daysUntil) {
					greater = strncmp(events[prev.slot].name,
					                     events[key.slot].name, MaxNameLen) > 0;
				}
			}

			if(!greater) break;
			sorted[j + 1] = sorted[j];
			--j;
		}
		sorted[j + 1] = key;
	}
}

void PhoneCountdown::clampStagedDay() {
	stagedYear  = clampYear(stagedYear);
	stagedMonth = clampMonth(stagedMonth);
	if(stagedDay < 1) stagedDay = 1;
	if(stagedDay > kMonthDays[stagedMonth - 1]) {
		stagedDay = kMonthDays[stagedMonth - 1];
	}
}

// ---------- mode / stage transitions ------------------------------------

void PhoneCountdown::enterList() {
	teardownEditView();

	mode      = Mode::List;
	editStage = EditStage::Name;

	cacheToday();
	rebuildSorted();

	if(eventCount == 0) {
		cursor = 0;
	} else if(cursor >= eventCount) {
		cursor = (uint8_t)(eventCount - 1);
	}

	if(captionLabel)  lv_obj_clear_flag(captionLabel,  LV_OBJ_FLAG_HIDDEN);
	if(topDivider)    lv_obj_clear_flag(topDivider,    LV_OBJ_FLAG_HIDDEN);
	if(bottomDivider) lv_obj_clear_flag(bottomDivider, LV_OBJ_FLAG_HIDDEN);
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		if(rowLabels[i]) lv_obj_clear_flag(rowLabels[i], LV_OBJ_FLAG_HIDDEN);
	}

	refreshCaption();
	refreshRows();
	refreshSoftKeys();
	refreshEmptyHint();
}

void PhoneCountdown::enterEdit(uint8_t slot, bool prefill, bool isNew) {
	if(slot >= MaxEvents) return;

	teardownEditView();

	editingSlot = slot;
	editingNew  = isNew;
	mode        = Mode::Edit;
	editStage   = EditStage::Name;
	dateField   = DateField::Year;

	cacheToday();

	if(isNew) {
		stagedYear  = todayYear;
		stagedMonth = todayMonth;
		stagedDay   = todayDay;
	} else if(slot < eventCount) {
		stagedYear  = events[slot].year;
		stagedMonth = events[slot].month;
		stagedDay   = events[slot].day;
	} else {
		stagedYear  = todayYear;
		stagedMonth = todayMonth;
		stagedDay   = todayDay;
	}
	clampStagedDay();

	buildEditView();

	// Hide list-only widgets behind the Edit overlay.
	if(captionLabel)  lv_obj_add_flag(captionLabel,  LV_OBJ_FLAG_HIDDEN);
	if(topDivider)    lv_obj_add_flag(topDivider,    LV_OBJ_FLAG_HIDDEN);
	if(bottomDivider) lv_obj_add_flag(bottomDivider, LV_OBJ_FLAG_HIDDEN);
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		if(rowLabels[i]) lv_obj_add_flag(rowLabels[i], LV_OBJ_FLAG_HIDDEN);
	}
	if(emptyHint) lv_obj_add_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);

	if(prefill && t9Input != nullptr && slot < eventCount && events[slot].name[0] != '\0') {
		t9Input->setText(String(events[slot].name));
	}

	refreshEditCaption();
	refreshEditDate();
	refreshEditHint();
	refreshSoftKeys();
}

void PhoneCountdown::enterEditDateStage() {
	editStage = EditStage::Date;
	dateField = DateField::Year;
	refreshEditDate();
	refreshEditHint();
	refreshSoftKeys();
}

void PhoneCountdown::enterEditNameStage() {
	editStage = EditStage::Name;
	refreshEditDate();
	refreshEditHint();
	refreshSoftKeys();
}

// ---------- list actions ------------------------------------------------

void PhoneCountdown::moveCursor(int8_t delta) {
	if(eventCount == 0 || delta == 0) return;
	int16_t next = (int16_t)cursor + (int16_t)delta;
	while(next < 0) next += eventCount;
	while(next >= (int16_t)eventCount) next -= eventCount;
	cursor = (uint8_t)next;
	refreshRows();
	refreshSoftKeys();
}

void PhoneCountdown::onNewPressed() {
	if(softKeys) softKeys->flashLeft();
	if(eventCount >= MaxEvents) {
		// Soft cap — silently no-op. The brief flashLeft above is the
		// only feedback so the user sees "I pressed a key" but no row
		// is overwritten.
		return;
	}
	enterEdit(eventCount, /*prefill=*/false, /*isNew=*/true);
}

void PhoneCountdown::onEditPressed() {
	if(softKeys) softKeys->flashRight();
	if(eventCount == 0) return;
	const uint8_t slot = sorted[cursor].slot;
	enterEdit(slot, /*prefill=*/true, /*isNew=*/false);
}

void PhoneCountdown::onDeletePressed() {
	if(eventCount == 0) return;
	const uint8_t slot = sorted[cursor].slot;
	if(slot >= eventCount) return;

	// Compact events[] downward.
	for(uint8_t i = slot; i + 1 < eventCount; ++i) {
		events[i] = events[i + 1];
	}
	if(eventCount > 0) --eventCount;
	if(eventCount == 0) {
		cursor = 0;
	} else {
		// Keep the cursor pointing at the same sorted index after the
		// deletion. rebuildSorted below renumbers slots.
		if(cursor >= eventCount) cursor = (uint8_t)(eventCount - 1);
	}

	save();
	rebuildSorted();
	refreshCaption();
	refreshRows();
	refreshSoftKeys();
	refreshEmptyHint();
}

// ---------- edit actions ------------------------------------------------

void PhoneCountdown::onNamePressed() {
	if(softKeys) softKeys->flashLeft();
	if(t9Input == nullptr) {
		enterList();
		return;
	}
	t9Input->commitPending();

	// Validate the name has *something* before advancing. If empty,
	// leave the user in the Name stage so they can correct it. The
	// hint already prompts them to enter a name first.
	String live = t9Input->getText();
	char raw[MaxNameLen + 1] = {0};
	const size_t copyLen = (live.length() < MaxNameLen)
	                       ? (size_t)live.length() : (size_t)MaxNameLen;
	memcpy(raw, live.c_str(), copyLen);
	raw[copyLen] = '\0';
	char trimmed[MaxNameLen + 1] = {0};
	trimText(raw, trimmed, sizeof(trimmed));
	if(trimmed[0] == '\0') {
		// Surface the empty-name issue with a hint flash but stay put.
		if(editHint) {
			lv_label_set_text(editHint, "ENTER A NAME FIRST");
		}
		return;
	}

	enterEditDateStage();
}

void PhoneCountdown::onSavePressed() {
	if(softKeys) softKeys->flashLeft();

	// Snapshot the staged name (already validated when we left the
	// Name stage, but re-validate just in case the user reset things).
	char raw[MaxNameLen + 1] = {0};
	if(t9Input != nullptr) {
		t9Input->commitPending();
		String live = t9Input->getText();
		const size_t copyLen = (live.length() < MaxNameLen)
		                       ? (size_t)live.length() : (size_t)MaxNameLen;
		memcpy(raw, live.c_str(), copyLen);
		raw[copyLen] = '\0';
	}
	char trimmed[MaxNameLen + 1] = {0};
	trimText(raw, trimmed, sizeof(trimmed));
	if(trimmed[0] == '\0') {
		// Empty name — bounce back to Name stage so the user can fix
		// it. Same conservative behaviour PhoneTodo's edit-empty path
		// uses.
		enterEditNameStage();
		if(editHint) {
			lv_label_set_text(editHint, "ENTER A NAME FIRST");
		}
		return;
	}
	clampStagedDay();

	if(editingNew) {
		if(editingSlot >= MaxEvents) {
			// Defensive: should never happen because onNewPressed
			// caps to MaxEvents, but tolerate a future caller path.
			enterList();
			return;
		}
		const uint8_t target = editingSlot;
		Event& e = events[target];
		strncpy(e.name, trimmed, MaxNameLen);
		e.name[MaxNameLen] = '\0';
		e.year  = stagedYear;
		e.month = stagedMonth;
		e.day   = stagedDay;
		if(target >= eventCount) {
			eventCount = (uint8_t)(target + 1);
		}
	} else if(editingSlot < eventCount) {
		Event& e = events[editingSlot];
		strncpy(e.name, trimmed, MaxNameLen);
		e.name[MaxNameLen] = '\0';
		e.year  = stagedYear;
		e.month = stagedMonth;
		e.day   = stagedDay;
	}

	save();

	// Land the user on the saved row. Find it in the new sorted order
	// so the cursor highlights what they just saved.
	rebuildSorted();
	for(uint8_t i = 0; i < eventCount; ++i) {
		if(sorted[i].slot == editingSlot) {
			cursor = i;
			break;
		}
	}

	enterList();
}

void PhoneCountdown::onBackPressed() {
	if(softKeys) softKeys->flashRight();
	if(editStage == EditStage::Date) {
		enterEditNameStage();
		return;
	}
	enterList();
}

void PhoneCountdown::onDateAdjust(int8_t delta) {
	if(editStage != EditStage::Date) return;
	if(delta == 0) return;

	switch(dateField) {
		case DateField::Year: {
			int32_t y = (int32_t)stagedYear + delta;
			if(y < (int32_t)YearMin) y = YearMin;
			if(y > (int32_t)YearMax) y = YearMax;
			stagedYear = (uint16_t)y;
			break;
		}
		case DateField::Month: {
			int32_t m = (int32_t)stagedMonth + delta;
			while(m < 1)  m += 12;
			while(m > 12) m -= 12;
			stagedMonth = (uint8_t)m;
			break;
		}
		case DateField::Day: {
			const uint8_t maxDay = kMonthDays[clampMonth(stagedMonth) - 1];
			int32_t d = (int32_t)stagedDay + delta;
			while(d < 1)              d += maxDay;
			while(d > (int32_t)maxDay) d -= maxDay;
			stagedDay = (uint8_t)d;
			break;
		}
	}
	clampStagedDay();
	refreshEditDate();
}

void PhoneCountdown::onDateFieldShift(int8_t delta) {
	if(editStage != EditStage::Date) return;
	int8_t f = (int8_t)dateField + delta;
	while(f < 0) f += 3;
	while(f > 2) f -= 3;
	dateField = (DateField)f;
	refreshEditDate();
}

// ---------- persistence -------------------------------------------------

void PhoneCountdown::load() {
	if(!ensureOpen()) return;

	// Header read.
	uint8_t header[kHeaderBytes] = {0};
	size_t  headerSize = sizeof(header);
	auto err = nvs_get_blob(s_handle, kBlobKey, header, &headerSize);
	if(err != ESP_OK)             return;
	if(headerSize < kHeaderBytes) return;
	if(header[0] != kMagic0)      return;
	if(header[1] != kMagic1)      return;
	if(header[2] != kVersion)     return;

	uint8_t storedCount = header[3];
	if(storedCount > MaxEvents) storedCount = MaxEvents;

	if(storedCount == 0) {
		eventCount = 0;
		return;
	}

	size_t blobSize = 0;
	err = nvs_get_blob(s_handle, kBlobKey, nullptr, &blobSize);
	if(err != ESP_OK || blobSize == 0) return;
	const size_t kMaxBlob = kHeaderBytes
	                      + (size_t)MaxEvents * (kEventHeaderBytes + (size_t)MaxNameLen);
	if(blobSize > kMaxBlob) return;

	uint8_t buf[kHeaderBytes
	            + (size_t)MaxEvents * (kEventHeaderBytes + (size_t)MaxNameLen)] = {0};
	size_t  readLen = blobSize;
	err = nvs_get_blob(s_handle, kBlobKey, buf, &readLen);
	if(err != ESP_OK) return;
	if(readLen < kHeaderBytes) return;

	size_t off = kHeaderBytes;
	uint8_t loaded = 0;
	for(uint8_t i = 0; i < storedCount; ++i) {
		if(off + kEventHeaderBytes > readLen) break;
		const uint16_t y      = (uint16_t)buf[off]
		                      | ((uint16_t)buf[off + 1] << 8);
		const uint8_t  m      = buf[off + 2];
		const uint8_t  d      = buf[off + 3];
		const uint8_t  nameLen = buf[off + 4];
		off += kEventHeaderBytes;
		if(nameLen > MaxNameLen)        break;
		if(off + nameLen > readLen)     break;

		Event& e = events[loaded];
		memcpy(e.name, &buf[off], nameLen);
		e.name[nameLen] = '\0';
		e.year  = clampYear(y);
		e.month = clampMonth(m);
		e.day   = clampDay(e.month, d);
		off += nameLen;
		++loaded;
	}
	eventCount = loaded;
}

void PhoneCountdown::save() {
	if(!ensureOpen()) return;

	uint8_t buf[kHeaderBytes
	            + (size_t)MaxEvents * (kEventHeaderBytes + (size_t)MaxNameLen)] = {0};
	size_t off = 0;

	buf[off++] = kMagic0;
	buf[off++] = kMagic1;
	buf[off++] = kVersion;
	buf[off++] = (uint8_t)eventCount;

	for(uint8_t i = 0; i < eventCount; ++i) {
		const Event& e = events[i];
		const size_t nameLen = strnlen(e.name, MaxNameLen);
		buf[off++] = (uint8_t)(e.year & 0xFF);
		buf[off++] = (uint8_t)((e.year >> 8) & 0xFF);
		buf[off++] = e.month;
		buf[off++] = e.day;
		buf[off++] = (uint8_t)nameLen;
		memcpy(&buf[off], e.name, nameLen);
		off += nameLen;
	}

	auto err = nvs_set_blob(s_handle, kBlobKey, buf, off);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneCountdown", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneCountdown", "nvs_commit failed: %d", (int)err);
	}
}

// ---------- input -------------------------------------------------------

void PhoneCountdown::buttonPressed(uint i) {
	if(mode == Mode::List) {
		switch(i) {
			case BTN_2:
			case BTN_L:
				moveCursor(-1);
				break;
			case BTN_8:
			case BTN_R:
				moveCursor(+1);
				break;
			case BTN_LEFT:
				onNewPressed();
				break;
			case BTN_RIGHT:
			case BTN_5:
				if(eventCount == 0) {
					// "BACK" softkey when empty — pop the screen.
					if(i == BTN_RIGHT) {
						if(softKeys) softKeys->flashRight();
						pop();
					}
				} else {
					onEditPressed();
				}
				break;
			case BTN_ENTER:
				if(eventCount > 0) onEditPressed();
				break;
			case BTN_3:
				onDeletePressed();
				break;
			default:
				break;
		}
		return;
	}

	// Edit mode.
	if(editStage == EditStage::Name) {
		switch(i) {
			case BTN_0: if(t9Input) t9Input->keyPress('0'); break;
			case BTN_1: if(t9Input) t9Input->keyPress('1'); break;
			case BTN_2: if(t9Input) t9Input->keyPress('2'); break;
			case BTN_3: if(t9Input) t9Input->keyPress('3'); break;
			case BTN_4: if(t9Input) t9Input->keyPress('4'); break;
			case BTN_5: if(t9Input) t9Input->keyPress('5'); break;
			case BTN_6: if(t9Input) t9Input->keyPress('6'); break;
			case BTN_7: if(t9Input) t9Input->keyPress('7'); break;
			case BTN_8: if(t9Input) t9Input->keyPress('8'); break;
			case BTN_9: if(t9Input) t9Input->keyPress('9'); break;
			case BTN_L:
				if(t9Input) t9Input->keyPress('*');
				break;
			case BTN_R:
				if(t9Input) t9Input->keyPress('#');
				break;
			case BTN_ENTER:
				if(t9Input) t9Input->commitPending();
				break;
			case BTN_LEFT:
				onNamePressed();
				break;
			case BTN_RIGHT:
				backLongFired = false;
				break;
			default:
				break;
		}
		return;
	}

	// Edit / Date stage.
	switch(i) {
		case BTN_2:
			onDateAdjust(+1);
			break;
		case BTN_8:
			onDateAdjust(-1);
			break;
		case BTN_L:
			onDateFieldShift(-1);
			break;
		case BTN_R:
		case BTN_ENTER:
			onDateFieldShift(+1);
			break;
		case BTN_LEFT:
			onSavePressed();
			break;
		case BTN_RIGHT:
			backLongFired = false;
			break;
		default:
			break;
	}
}

void PhoneCountdown::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		backLongFired = true;
		if(softKeys) softKeys->flashRight();
		pop();
	}
}

void PhoneCountdown::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		if(mode == Mode::Edit) {
			onBackPressed();
		} else {
			if(softKeys) softKeys->flashRight();
			pop();
		}
		return;
	}

	if(i == BTN_RIGHT && mode == Mode::Edit) {
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		onBackPressed();
		return;
	}
}
