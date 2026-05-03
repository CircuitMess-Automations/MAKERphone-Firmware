#include "PhoneHabits.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <nvs.h>
#include <esp_log.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneT9Input.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

// MAKERphone retro palette — kept identical to every other Phone*
// widget so the habit tracker slots in beside PhoneTodo (S136),
// PhoneBirthdayReminders (S135), PhoneAlarmClock (S124),
// PhoneVirtualPet (S129) and the rest of the Phase-Q family without
// a visual seam. Same inline-#define convention every other Phone*
// screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan (today done)
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// ---------- geometry ------------------------------------------------------
//
// 160 x 128 layout. Vertical budget for List view:
//   y =  0..  9   PhoneStatusBar (10 px)
//   y = 12        caption "HABITS  N/M" (pixelbasic7, ~7 px tall)
//   y = 22        top divider rule (1 px)
//   y = 26        row 0 (14 px row stride)
//   y = 40        row 1
//   y = 54        row 2
//   y = 68        row 3
//   y = 82        row 4
//   y = 98        bottom divider rule (1 px)
//   y =118..127   PhoneSoftKeyBar (10 px)
//
// Within each 14 px row:
//   y = row+0..row+7   name label (left) + streak label (right)
//   y = row+3..row+7   7 heatmap cells (4 px wide, 1 px gap = 5 stride)
//   x =   4 ..  88     name label (84 px, ~16 chars truncated)
//   x =  92 .. 122     7 heatmap cells (4*7 + 6*1 = 34 px)
//   x = 128 .. 156     streak label (right-aligned, ~28 px)
//
// Five rows fit between the dividers without the heatmap colliding
// with the name. The 35 cells (5 rows x 7 cells) are pre-allocated
// at construction and only have their colour repainted on refresh.

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kTopDividerY  = 22;
static constexpr lv_coord_t kRowsTopY     = 26;
static constexpr lv_coord_t kRowStride    = 14;
static constexpr lv_coord_t kBotDividerY  = 98;
static constexpr lv_coord_t kRowLeftX     = 4;
static constexpr lv_coord_t kRowWidth     = 152;
static constexpr lv_coord_t kNameW        = 84;
static constexpr lv_coord_t kHeatmapX     = 92;
static constexpr lv_coord_t kHeatmapDY    = 3;   // offset within row (cell tops)
static constexpr lv_coord_t kCellW        = 4;
static constexpr lv_coord_t kCellH        = 5;
static constexpr lv_coord_t kCellStride   = 5;
static constexpr lv_coord_t kStreakX      = 128;
static constexpr lv_coord_t kStreakW      = 28;

// "Empty list" multi-line hint geometry. Sits roughly in the middle
// of the rows strip so the user reads it as occupying the whole list.
static constexpr lv_coord_t kEmptyHintY   = 44;
static constexpr lv_coord_t kEmptyHintW   = 152;

// Edit-view layout.
//   y = 12       "NEW HABIT" / "EDIT HABIT" caption (pixelbasic7)
//   y = 24       PhoneT9Input
//   y = 60       hint strip
//   y =118..127  PhoneSoftKeyBar
static constexpr lv_coord_t kEditCaptionY = 12;
static constexpr lv_coord_t kEditT9Y      = 24;
static constexpr lv_coord_t kEditHintY    = 60;
static constexpr lv_coord_t kEditHintW    = 152;

// ---------- NVS persistence ----------------------------------------------

namespace {

constexpr const char* kNamespace = "mphabits";
constexpr const char* kBlobKey   = "h";

constexpr uint8_t  kMagic0  = 'M';
constexpr uint8_t  kMagic1  = 'P';
constexpr uint8_t  kVersion = 1;

constexpr size_t   kHeaderBytes = 8;        // 4 byte tag + 4 byte lastSyncDay
constexpr size_t   kHabitFixed  = 1 + 4;    // name length + history word
constexpr uint32_t kHistoryMask = 0x0FFFFFFFu; // 28-bit useful range

// Single shared NVS handle, lazy-open. Mirrors PhoneTodo /
// PhoneVirtualPet so we never spam nvs_open() retries when the
// flash partition is unavailable.
nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneHabits",
		         "nvs_open(%s) failed: %d -- habits run without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

void writeU32LE(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)( v        & 0xFF);
	p[1] = (uint8_t)((v >>  8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
	p[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint32_t readU32LE(const uint8_t* p) {
	return  (uint32_t) p[0]
	     | ((uint32_t) p[1] <<  8)
	     | ((uint32_t) p[2] << 16)
	     | ((uint32_t) p[3] << 24);
}

lv_color_t cellColor(bool done, bool isToday) {
	if(done && isToday) return MP_HIGHLIGHT;
	if(done)            return MP_ACCENT;
	if(isToday)         return MP_LABEL_DIM;
	return MP_DIM;
}

} // namespace

// ---------- public statics -----------------------------------------------

void PhoneHabits::trimText(const char* in, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(in == nullptr) {
		out[0] = '\0';
		return;
	}

	while(*in != '\0' && isspace((unsigned char) *in)) ++in;

	const char* end = in + strlen(in);
	while(end > in && isspace((unsigned char) *(end - 1))) --end;

	const size_t srcLen  = (size_t) (end - in);
	const size_t copyLen = (srcLen < outLen - 1) ? srcLen : (outLen - 1);
	memcpy(out, in, copyLen);
	out[copyLen] = '\0';
}

uint8_t PhoneHabits::streakOfBits(uint32_t history) {
	history &= kHistoryMask;
	uint8_t s = 0;
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		if(history & (1u << i)) ++s;
		else                    break;
	}
	return s;
}

uint32_t PhoneHabits::todayIndex() {
	return PhoneClock::nowEpoch() / 86400u;
}

// ---------- ctor / dtor --------------------------------------------------

PhoneHabits::PhoneHabits()
		: LVScreen() {

	// Zero the slots so an early refresh sees stable empty state.
	for(uint8_t i = 0; i < MaxHabits; ++i) {
		habits[i].name[0] = '\0';
		habits[i].history = 0;
	}
	habitCount  = 0;
	lastSyncDay = todayIndex();

	// Try to load from NVS. If that fails we start with an empty
	// list — the screen runs RAM-only.
	load();

	// Always sync forward to today after load: a habit set up five
	// days ago shifts its history into the right historical slots
	// before the first refresh. Performed once at construction; the
	// list view runs on day-boundaries by re-running syncToToday()
	// from each user action.
	syncToToday();

	// Full-screen blank canvas, same pattern every Phone* screen uses.
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

PhoneHabits::~PhoneHabits() {
	stopTickFlash();
	// LVGL children parented to obj are freed by the LVScreen base
	// destructor. PhoneT9Input cancels its commit + caret timers on
	// destruction so a tear-down mid-edit can't leave a callback
	// pointing into freed memory.
}

void PhoneHabits::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneHabits::onStop() {
	stopTickFlash();
	Input::getInstance()->removeListener(this);
}

// ---------- builders -----------------------------------------------------

void PhoneHabits::buildListView() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "HABITS  0/5");

	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	for(uint8_t r = 0; r < MaxHabits; ++r) {
		const lv_coord_t rowY = kRowsTopY + r * kRowStride;

		lv_obj_t* name = lv_label_create(obj);
		lv_obj_set_style_text_font(name, &pixelbasic7, 0);
		lv_obj_set_style_text_color(name, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
		lv_obj_set_width(name, kNameW);
		lv_obj_set_pos(name, kRowLeftX, rowY);
		lv_label_set_text(name, "");
		nameLabels[r] = name;

		for(uint8_t c = 0; c < HeatmapDays; ++c) {
			lv_obj_t* cell = lv_obj_create(obj);
			lv_obj_remove_style_all(cell);
			lv_obj_set_size(cell, kCellW, kCellH);
			lv_obj_set_style_bg_color(cell, MP_DIM, 0);
			lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
			lv_obj_set_pos(cell,
			               kHeatmapX + c * kCellStride,
			               rowY + kHeatmapDY);
			heatmapCells[r][c] = cell;
		}

		lv_obj_t* streak = lv_label_create(obj);
		lv_obj_set_style_text_font(streak, &pixelbasic7, 0);
		lv_obj_set_style_text_color(streak, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(streak, LV_LABEL_LONG_CLIP);
		lv_obj_set_width(streak, kStreakW);
		lv_obj_set_style_text_align(streak, LV_TEXT_ALIGN_RIGHT, 0);
		lv_obj_set_pos(streak, kStreakX, rowY);
		lv_label_set_text(streak, "");
		streakLabels[r] = streak;
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
		"NO HABITS YET.\n"
		"PRESS \"NEW\" TO ADD\n"
		"YOUR FIRST HABIT.");
	lv_obj_add_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
}

void PhoneHabits::buildEditView() {
	editCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(editCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(editCaption, MP_HIGHLIGHT, 0);
	lv_obj_set_align(editCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(editCaption, kEditCaptionY);
	lv_label_set_text(editCaption, "NEW HABIT");

	t9Input = new PhoneT9Input(obj, MaxNameLen);
	lv_obj_set_pos(t9Input->getLvObj(),
	               (160 - PhoneT9Input::Width) / 2,
	               kEditT9Y);
	t9Input->setPlaceholder("HABIT NAME");
	t9Input->setCase(PhoneT9Input::Case::First);

	editHint = lv_label_create(obj);
	lv_obj_set_style_text_font(editHint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(editHint, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(editHint, LV_LABEL_LONG_DOT);
	lv_obj_set_width(editHint, kEditHintW);
	lv_obj_set_style_text_align(editHint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(editHint, kRowLeftX, kEditHintY);
	lv_label_set_text(editHint, "NAME UP TO 16 CHARS");
}

void PhoneHabits::teardownEditView() {
	if(t9Input != nullptr) {
		delete t9Input;
		t9Input = nullptr;
	}
	if(editCaption != nullptr) {
		lv_obj_del(editCaption);
		editCaption = nullptr;
	}
	if(editHint != nullptr) {
		lv_obj_del(editHint);
		editHint = nullptr;
	}
}

// ---------- public introspection -----------------------------------------

const char* PhoneHabits::getHabitName(uint8_t slot) const {
	if(slot >= habitCount) return "";
	return habits[slot].name;
}

bool PhoneHabits::isCompletedToday(uint8_t slot) const {
	if(slot >= habitCount) return false;
	return (habits[slot].history & 0x1u) != 0;
}

bool PhoneHabits::isCompletedDaysAgo(uint8_t slot, uint8_t daysAgo) const {
	if(slot >= habitCount)        return false;
	if(daysAgo >= HistoryDays)    return false;
	return (habits[slot].history & (1u << daysAgo)) != 0;
}

uint8_t PhoneHabits::streakOf(uint8_t slot) const {
	if(slot >= habitCount) return 0;
	return streakOfBits(habits[slot].history);
}

uint32_t PhoneHabits::getHistory(uint8_t slot) const {
	if(slot >= habitCount) return 0;
	return habits[slot].history & kHistoryMask;
}

// ---------- repainters ---------------------------------------------------

void PhoneHabits::refreshCaption() {
	if(captionLabel == nullptr) return;
	char buf[16];
	snprintf(buf, sizeof(buf), "HABITS  %u/%u",
	         (unsigned) habitCount,
	         (unsigned) MaxHabits);
	lv_label_set_text(captionLabel, buf);
}

void PhoneHabits::refreshRows() {
	for(uint8_t r = 0; r < MaxHabits; ++r) {
		const bool   active   = (r < habitCount);
		const bool   isCursor = active && (r == cursor) && (mode == Mode::List);
		const Habit& h        = habits[r];

		if(nameLabels[r] != nullptr) {
			if(active) {
				char buf[8 + MaxNameLen + 4] = {};
				snprintf(buf, sizeof(buf), "%c[%c] %s",
				         isCursor ? '>' : ' ',
				         (h.history & 0x1u) ? 'x' : ' ',
				         h.name);
				// Strip newlines so a row stays a single visual line.
				for(size_t j = 0; buf[j] != '\0'; ++j) {
					if(buf[j] == '\n' || buf[j] == '\r') buf[j] = ' ';
				}
				lv_label_set_text(nameLabels[r], buf);

				lv_color_t color;
				if(isCursor) {
					color = MP_ACCENT;
				} else if(h.history & 0x1u) {
					color = MP_TEXT;
				} else {
					color = MP_LABEL_DIM;
				}
				lv_obj_set_style_text_color(nameLabels[r], color, 0);
				lv_obj_clear_flag(nameLabels[r], LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_label_set_text(nameLabels[r], "");
				lv_obj_add_flag(nameLabels[r], LV_OBJ_FLAG_HIDDEN);
			}
		}

		// Heatmap cells. Position 0 (leftmost) = oldest, position
		// HeatmapDays-1 (rightmost) = today.
		for(uint8_t c = 0; c < HeatmapDays; ++c) {
			lv_obj_t* cell = heatmapCells[r][c];
			if(cell == nullptr) continue;
			if(!active) {
				lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
				continue;
			}
			lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
			const uint8_t daysAgo = (uint8_t)(HeatmapDays - 1 - c);
			const bool    done    = (h.history & (1u << daysAgo)) != 0;
			const bool    today   = (daysAgo == 0);
			lv_obj_set_style_bg_color(cell, cellColor(done, today), 0);
		}

		if(streakLabels[r] != nullptr) {
			if(active) {
				char buf[8] = {};
				snprintf(buf, sizeof(buf), "%ud",
				         (unsigned) streakOfBits(h.history));
				lv_label_set_text(streakLabels[r], buf);
				lv_color_t color;
				if(isCursor && (h.history & 0x1u)) {
					color = MP_ACCENT;
				} else if(h.history & 0x1u) {
					color = MP_HIGHLIGHT;
				} else if(isCursor) {
					color = MP_ACCENT;
				} else {
					color = MP_LABEL_DIM;
				}
				lv_obj_set_style_text_color(streakLabels[r], color, 0);
				lv_obj_clear_flag(streakLabels[r], LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_label_set_text(streakLabels[r], "");
				lv_obj_add_flag(streakLabels[r], LV_OBJ_FLAG_HIDDEN);
			}
		}
	}
}

void PhoneHabits::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	if(mode == Mode::List) {
		softKeys->setLeft("NEW");
		if(habitCount == 0) {
			softKeys->setRight("");
		} else {
			softKeys->setRight((habits[cursor].history & 0x1u) ? "UNDO" : "TICK");
		}
	} else {
		softKeys->setLeft("SAVE");
		softKeys->setRight("BACK");
	}
}

void PhoneHabits::refreshEmptyHint() {
	if(emptyHint == nullptr) return;
	if(habitCount == 0 && mode == Mode::List) {
		lv_obj_clear_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneHabits::refreshEditCaption() {
	if(editCaption == nullptr) return;
	lv_label_set_text(editCaption, editingNew ? "NEW HABIT" : "EDIT HABIT");
}

// ---------- model helpers ------------------------------------------------

int8_t PhoneHabits::firstEmptySlot() const {
	if(habitCount < MaxHabits) return (int8_t) habitCount;
	return -1;
}

void PhoneHabits::writeName(uint8_t slot, const char* text) {
	if(slot >= MaxHabits) return;
	if(text == nullptr) text = "";
	strncpy(habits[slot].name, text, MaxNameLen);
	habits[slot].name[MaxNameLen] = '\0';
}

void PhoneHabits::removeSlot(uint8_t slot) {
	if(slot >= habitCount) return;
	for(uint8_t i = slot; i + 1 < habitCount; ++i) {
		habits[i] = habits[i + 1];
	}
	if(habitCount > 0) {
		habits[habitCount - 1].name[0] = '\0';
		habits[habitCount - 1].history = 0;
		--habitCount;
	}
	if(habitCount == 0) {
		cursor = 0;
	} else if(cursor >= habitCount) {
		cursor = (uint8_t)(habitCount - 1);
	}
}

void PhoneHabits::toggleToday(uint8_t slot) {
	if(slot >= habitCount) return;
	habits[slot].history ^= 0x1u;
	habits[slot].history &= kHistoryMask;
}

// ---------- day rollover -------------------------------------------------

void PhoneHabits::syncToToday() {
	const uint32_t today = todayIndex();
	if(today == lastSyncDay) return;
	if(today < lastSyncDay) {
		// Clock moved backward — adopt the new anchor without rotating
		// any history. Edge case (user time-traveled). The next
		// forward advance will only shift by the new delta.
		lastSyncDay = today;
		return;
	}
	const uint32_t delta = today - lastSyncDay;
	if(delta >= HistoryDays) {
		for(uint8_t i = 0; i < habitCount; ++i) {
			habits[i].history = 0;
		}
	} else {
		const uint8_t shift = (uint8_t) delta;
		for(uint8_t i = 0; i < habitCount; ++i) {
			habits[i].history = (habits[i].history << shift) & kHistoryMask;
		}
	}
	lastSyncDay = today;
}

// ---------- mode transitions ---------------------------------------------

void PhoneHabits::enterList() {
	teardownEditView();
	stopTickFlash();
	mode = Mode::List;

	if(habitCount == 0) {
		cursor = 0;
	} else if(cursor >= habitCount) {
		cursor = (uint8_t)(habitCount - 1);
	}

	// Re-show the list-only widgets that may have been hidden while
	// the Edit overlay was up.
	if(captionLabel)  lv_obj_clear_flag(captionLabel,  LV_OBJ_FLAG_HIDDEN);
	if(topDivider)    lv_obj_clear_flag(topDivider,    LV_OBJ_FLAG_HIDDEN);
	if(bottomDivider) lv_obj_clear_flag(bottomDivider, LV_OBJ_FLAG_HIDDEN);
	for(uint8_t i = 0; i < MaxHabits; ++i) {
		if(nameLabels[i])   lv_obj_clear_flag(nameLabels[i],   LV_OBJ_FLAG_HIDDEN);
		if(streakLabels[i]) lv_obj_clear_flag(streakLabels[i], LV_OBJ_FLAG_HIDDEN);
		for(uint8_t c = 0; c < HeatmapDays; ++c) {
			if(heatmapCells[i][c]) lv_obj_clear_flag(heatmapCells[i][c], LV_OBJ_FLAG_HIDDEN);
		}
	}

	refreshCaption();
	refreshRows();
	refreshSoftKeys();
	refreshEmptyHint();
}

void PhoneHabits::enterEdit(uint8_t slot, bool prefill, bool isNew) {
	if(slot >= MaxHabits) return;

	teardownEditView();
	stopTickFlash();

	editingSlot = slot;
	editingNew  = isNew;
	mode        = Mode::Edit;

	buildEditView();

	// Hide the list-only widgets behind the Edit overlay so the rows
	// + dividers + caption don't ghost through. They are re-shown on
	// enterList(). We keep the wallpaper + status bar + soft-keys up
	// so the screen stays consistent with the rest of the family.
	if(captionLabel)  lv_obj_add_flag(captionLabel,  LV_OBJ_FLAG_HIDDEN);
	if(topDivider)    lv_obj_add_flag(topDivider,    LV_OBJ_FLAG_HIDDEN);
	if(bottomDivider) lv_obj_add_flag(bottomDivider, LV_OBJ_FLAG_HIDDEN);
	for(uint8_t i = 0; i < MaxHabits; ++i) {
		if(nameLabels[i])   lv_obj_add_flag(nameLabels[i],   LV_OBJ_FLAG_HIDDEN);
		if(streakLabels[i]) lv_obj_add_flag(streakLabels[i], LV_OBJ_FLAG_HIDDEN);
		for(uint8_t c = 0; c < HeatmapDays; ++c) {
			if(heatmapCells[i][c]) lv_obj_add_flag(heatmapCells[i][c], LV_OBJ_FLAG_HIDDEN);
		}
	}

	refreshEditCaption();
	refreshSoftKeys();
	refreshEmptyHint();

	if(prefill && t9Input != nullptr && slot < habitCount && habits[slot].name[0] != '\0') {
		t9Input->setText(String(habits[slot].name));
	}
}

// ---------- list actions -------------------------------------------------

void PhoneHabits::moveCursor(int8_t delta) {
	if(habitCount == 0 || delta == 0) return;
	int16_t next = (int16_t) cursor + (int16_t) delta;
	if(next < 0) next = 0;
	if(next >= (int16_t) habitCount) next = (int16_t)(habitCount - 1);
	cursor = (uint8_t) next;
	refreshRows();
	refreshSoftKeys();
}

void PhoneHabits::onNewPressed() {
	if(softKeys) softKeys->flashLeft();

	syncToToday();

	int8_t target = firstEmptySlot();
	if(target < 0) {
		// All slots full — overwrite the cursor row so NEW always
		// has a target. Matches the PhoneTodo / PhoneNotepad muscle
		// memory. The actual overwrite only happens on a non-empty
		// SAVE inside onSavePressed(); backing out leaves the
		// existing habit intact.
		if(habitCount == 0) return;
		target = (int8_t) cursor;
		enterEdit((uint8_t) target, /*prefill=*/false, /*isNew=*/true);
		return;
	}

	// Append path — `target` == habitCount, slot is already empty so
	// no wipe needed. habitCount itself is bumped only on a non-empty
	// SAVE inside onSavePressed().
	cursor = (uint8_t) target;
	enterEdit((uint8_t) target, /*prefill=*/false, /*isNew=*/true);
}

void PhoneHabits::onTickPressed() {
	if(softKeys) softKeys->flashRight();
	if(habitCount == 0) return;

	syncToToday();
	toggleToday(cursor);

	if(cursor < MaxHabits) {
		startTickFlash(cursor);
	}

	save();
	refreshSoftKeys();
}

void PhoneHabits::onEditPressed() {
	if(habitCount == 0) return;
	enterEdit(cursor, /*prefill=*/true, /*isNew=*/false);
}

// ---------- edit actions -------------------------------------------------

void PhoneHabits::onSavePressed() {
	if(softKeys) softKeys->flashLeft();
	if(t9Input == nullptr) {
		enterList();
		return;
	}

	// Force the T9 widget to commit any in-flight pending letter so a
	// user who taps a digit and immediately hits SAVE does not lose
	// their last letter. Same gesture PhoneTodo / PhoneNotepad use.
	t9Input->commitPending();

	char raw[MaxNameLen + 1] = {};
	String live = t9Input->getText();
	const size_t copyLen = (live.length() < MaxNameLen)
	        ? (size_t) live.length() : (size_t) MaxNameLen;
	memcpy(raw, live.c_str(), copyLen);
	raw[copyLen] = '\0';

	char trimmed[MaxNameLen + 1] = {};
	trimText(raw, trimmed, sizeof(trimmed));

	if(editingNew) {
		if(trimmed[0] != '\0') {
			// Writing a fresh habit. Slot is either an append (one
			// past habitCount; bump the count + start blank history)
			// or an overwrite of the cursor row when full (slot <
			// habitCount; reset the history because this is a new
			// habit replacing the slot).
			const bool isAppend = (editingSlot >= habitCount);
			writeName(editingSlot, trimmed);
			habits[editingSlot].history = 0;
			if(isAppend) {
				habitCount = (uint8_t)(editingSlot + 1);
			}
		}
		// Empty-after-trim on the NEW path: no slot is touched. The
		// append peek-past-habitCount slot is already blank, and the
		// overwrite-when-full path leaves the existing cursor row
		// intact (matches the "press NEW + back out" muscle memory).
	} else {
		// EDIT path — clear-to-delete on empty, otherwise rename
		// while preserving history.
		if(trimmed[0] == '\0') {
			removeSlot(editingSlot);
		} else {
			writeName(editingSlot, trimmed);
		}
	}

	save();

	// Land the user on the saved row so they can see their write.
	if(habitCount > 0) {
		if(editingSlot < habitCount) {
			cursor = editingSlot;
		} else {
			cursor = (uint8_t)(habitCount - 1);
		}
	} else {
		cursor = 0;
	}
	enterList();
}

void PhoneHabits::onBackPressed() {
	if(softKeys) softKeys->flashRight();
	enterList();
}

// ---------- tick-off animation -------------------------------------------

void PhoneHabits::startTickFlash(uint8_t visibleRow) {
	stopTickFlash();
	if(visibleRow >= MaxHabits)   return;
	if(visibleRow >= habitCount)  return;
	if(nameLabels[visibleRow] == nullptr) return;

	// First repaint the rows so the flash overrides the right
	// starting state.
	refreshRows();

	// Force the cursor row's name + streak labels to highlight cyan
	// as the "tick lit" colour.
	lv_obj_set_style_text_color(nameLabels[visibleRow], MP_HIGHLIGHT, 0);
	if(streakLabels[visibleRow] != nullptr) {
		lv_obj_set_style_text_color(streakLabels[visibleRow], MP_HIGHLIGHT, 0);
	}
	lv_obj_t* todayCell = heatmapCells[visibleRow][HeatmapDays - 1];
	if(todayCell != nullptr) {
		lv_obj_set_style_bg_color(todayCell, MP_HIGHLIGHT, 0);
	}

	tickRow   = visibleRow;
	tickTimer = lv_timer_create(&PhoneHabits::tickTimerCb,
	                            (uint32_t) TickFlashMs, this);
	lv_timer_set_repeat_count(tickTimer, 1);
}

void PhoneHabits::stopTickFlash() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
	tickRow = 0xFF;
}

void PhoneHabits::tickTimerCb(lv_timer_t* t) {
	auto* self = static_cast<PhoneHabits*>(t->user_data);
	if(self == nullptr) return;

	// LVGL has already auto-deleted the timer because repeat_count
	// was 1, so just clear our handle and revert the row colours.
	self->tickTimer = nullptr;
	self->tickRow   = 0xFF;
	self->refreshRows();
}

// ---------- persistence --------------------------------------------------

void PhoneHabits::load() {
	if(!ensureOpen()) return;

	size_t blobSize = 0;
	auto err = nvs_get_blob(s_handle, kBlobKey, nullptr, &blobSize);
	if(err != ESP_OK)            return;
	if(blobSize < kHeaderBytes)  return;

	const size_t kMaxBlob = kHeaderBytes
	    + (size_t) MaxHabits * (kHabitFixed + (size_t) MaxNameLen);
	if(blobSize > kMaxBlob) return;

	uint8_t buf[kHeaderBytes
	    + (size_t) MaxHabits * (kHabitFixed + (size_t) MaxNameLen)] = {};
	size_t  readLen = blobSize;
	err = nvs_get_blob(s_handle, kBlobKey, buf, &readLen);
	if(err != ESP_OK)             return;
	if(readLen < kHeaderBytes)    return;
	if(buf[0] != kMagic0)         return;
	if(buf[1] != kMagic1)         return;
	if(buf[2] != kVersion)        return;

	uint8_t storedCount = buf[3];
	if(storedCount > MaxHabits) storedCount = MaxHabits;
	lastSyncDay = readU32LE(&buf[4]);

	size_t off = kHeaderBytes;
	uint8_t loaded = 0;
	for(uint8_t i = 0; i < storedCount; ++i) {
		if(off + 1 > readLen)               break;
		const uint8_t nameLen = buf[off++];
		if(nameLen > MaxNameLen)            break;
		if(off + (size_t) nameLen + 4 > readLen) break;

		Habit& h = habits[loaded];
		memcpy(h.name, &buf[off], nameLen);
		h.name[nameLen] = '\0';
		off += nameLen;

		h.history = readU32LE(&buf[off]) & kHistoryMask;
		off += 4;

		++loaded;
	}
	habitCount = loaded;
}

void PhoneHabits::save() {
	if(!ensureOpen()) return;

	uint8_t buf[kHeaderBytes
	    + (size_t) MaxHabits * (kHabitFixed + (size_t) MaxNameLen)] = {};
	size_t off = 0;

	buf[off++] = kMagic0;
	buf[off++] = kMagic1;
	buf[off++] = kVersion;
	buf[off++] = (uint8_t) habitCount;
	writeU32LE(&buf[off], lastSyncDay);
	off += 4;

	for(uint8_t i = 0; i < habitCount; ++i) {
		const Habit& h = habits[i];
		const size_t srcLen = strnlen(h.name, MaxNameLen);
		const uint8_t nameLen = (uint8_t) srcLen;
		buf[off++] = nameLen;
		memcpy(&buf[off], h.name, nameLen);
		off += nameLen;
		writeU32LE(&buf[off], h.history & kHistoryMask);
		off += 4;
	}

	auto err = nvs_set_blob(s_handle, kBlobKey, buf, off);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneHabits", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneHabits", "nvs_commit failed: %d", (int)err);
	}
}

// ---------- input --------------------------------------------------------

void PhoneHabits::buttonPressed(uint i) {
	if(mode == Mode::List) {
		// BTN_UP / BTN_DOWN are aliased to BTN_LEFT / BTN_RIGHT in
		// Pins.hpp (same constraint PhoneTodo documents), so cursor
		// navigation lives on BTN_2 / BTN_8 + the L / R bumpers.
		switch(i) {
			case BTN_2:
			case BTN_L:
				moveCursor(-1);
				break;
			case BTN_8:
			case BTN_R:
				moveCursor(+1);
				break;
			case BTN_5:
				onEditPressed();
				break;
			case BTN_LEFT:
				onNewPressed();
				break;
			case BTN_RIGHT:
			case BTN_ENTER:
				onTickPressed();
				break;
			default:
				break;
		}
		return;
	}

	// Edit mode: route digits + bumpers through the T9 funnel.
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
			onSavePressed();
			break;

		case BTN_RIGHT:
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneHabits::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		backLongFired = true;
		if(softKeys) softKeys->flashRight();
		pop();
	}
}

void PhoneHabits::buttonReleased(uint i) {
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
