#include "PhoneTodo.h"

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

// MAKERphone retro palette — kept identical to every other Phone*
// widget so the to-do screen slots in beside PhoneCalculator (S60),
// PhoneStopwatch (S61), PhoneTimer (S62), PhoneCalendar (S63),
// PhoneNotepad (S64), PhoneAlarmClock (S124), PhoneTimers (S125),
// PhoneBirthdayReminders (S135) and the rest of the Phase-Q family
// without a visual seam. Same inline-#define convention every other
// Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (HIGH)
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan (caption / MED)
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream (LOW)
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream (done)

// ---------- geometry ------------------------------------------------------
//
// 160 x 128 layout. Vertical budget for List view:
//   y =  0..  9   PhoneStatusBar (10 px)
//   y = 12        caption "TODO  N/M" (pixelbasic7, ~7 px tall)
//   y = 22        top divider rule (1 px)
//   y = 26        row 0 (12 px row stride)
//   y = 38        row 1
//   y = 50        row 2
//   y = 62        row 3
//   y = 74        row 4
//   y = 86        row 5
//   y = 98        bottom divider rule (1 px)
//   y = 118..127  PhoneSoftKeyBar (10 px)
//
// 12 px row stride at pixelbasic7 keeps glyph collisions away while
// fitting 6 visible rows in the strip between the dividers.

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kTopDividerY  = 22;
static constexpr lv_coord_t kRowsTopY     = 26;
static constexpr lv_coord_t kRowStride    = 12;
static constexpr lv_coord_t kBotDividerY  = 98;
static constexpr lv_coord_t kRowLeftX     = 4;
static constexpr lv_coord_t kRowWidth     = 152;

// "Empty list" multi-line hint geometry. Sits roughly in the middle
// of the rows strip so the user reads it as occupying the whole list.
static constexpr lv_coord_t kEmptyHintY   = 44;
static constexpr lv_coord_t kEmptyHintW   = 152;

// Edit-view layout.
//   y = 12       "NEW TASK" / "EDIT TASK" caption (pixelbasic7)
//   y = 24       PhoneT9Input (Width x Height + HelpHeight = 156 x 30)
//   y = 60       priority hint strip
//   y = 118..127 PhoneSoftKeyBar
static constexpr lv_coord_t kEditCaptionY = 12;
static constexpr lv_coord_t kEditT9Y      = 24;
static constexpr lv_coord_t kEditHintY    = 60;
static constexpr lv_coord_t kEditHintW    = 152;

// ---------- NVS persistence ----------------------------------------------

namespace {

constexpr const char* kNamespace = "mptodo";
constexpr const char* kBlobKey   = "t";

constexpr uint8_t kMagic0  = 'M';
constexpr uint8_t kMagic1  = 'P';
constexpr uint8_t kVersion = 1;

// Per-task on-disk record: 1 flags + 1 length + up to MaxLen bytes.
constexpr size_t  kHeaderBytes = 4;
constexpr size_t  kTaskHeader  = 2;

// Single shared NVS handle. Mirrors PhoneVirtualPet's lazy-open
// pattern so we never spam nvs_open() retries.
nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneTodo",
		         "nvs_open(%s) failed: %d -- todo runs without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

uint8_t encodeFlags(PhoneTodo::Priority pr, bool done) {
	uint8_t flags = (uint8_t)((uint8_t)pr & 0x03) << 4;
	if(done) flags |= 0x01;
	return flags;
}

void decodeFlags(uint8_t flags, PhoneTodo::Priority& prOut, bool& doneOut) {
	const uint8_t pr = (flags >> 4) & 0x03;
	switch(pr) {
		case 0:  prOut = PhoneTodo::Priority::High; break;
		case 1:  prOut = PhoneTodo::Priority::Med;  break;
		case 2:  prOut = PhoneTodo::Priority::Low;  break;
		default: prOut = PhoneTodo::Priority::Med;  break;
	}
	doneOut = (flags & 0x01) != 0;
}

lv_color_t priorityColor(PhoneTodo::Priority pr) {
	switch(pr) {
		case PhoneTodo::Priority::High: return MP_ACCENT;
		case PhoneTodo::Priority::Med:  return MP_HIGHLIGHT;
		case PhoneTodo::Priority::Low:  return MP_TEXT;
	}
	return MP_TEXT;
}

} // namespace

// ---------- public statics -----------------------------------------------

void PhoneTodo::trimText(const char* in, char* out, size_t outLen) {
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

PhoneTodo::Priority PhoneTodo::nextPriority(PhoneTodo::Priority p) {
	switch(p) {
		case Priority::High: return Priority::Med;
		case Priority::Med:  return Priority::Low;
		case Priority::Low:  return Priority::High;
	}
	return Priority::Med;
}

char PhoneTodo::priorityLetter(PhoneTodo::Priority p) {
	switch(p) {
		case Priority::High: return 'H';
		case Priority::Med:  return 'M';
		case Priority::Low:  return 'L';
	}
	return 'M';
}

// ---------- ctor / dtor --------------------------------------------------

PhoneTodo::PhoneTodo()
		: LVScreen() {

	// Zero the slots so an early refresh sees stable empty state.
	for(uint8_t i = 0; i < MaxTasks; ++i) {
		tasks[i].text[0]  = '\0';
		tasks[i].priority = Priority::Med;
		tasks[i].done     = false;
	}
	taskCount = 0;

	// Try to load from NVS. If that fails we start with an empty
	// list — the screen runs RAM-only.
	load();

	// Full-screen blank canvas, same pattern every Phone* screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top status bar (10 px).
	statusBar = new PhoneStatusBar(obj);

	// Bottom soft-key bar.
	softKeys = new PhoneSoftKeyBar(obj);

	buildListView();

	setButtonHoldTime(BTN_BACK, BackHoldMs);

	enterList();
}

PhoneTodo::~PhoneTodo() {
	stopTickFlash();

	// Children are parented to obj and freed by the LVScreen base
	// destructor. The optional PhoneT9Input destructor itself
	// cancels its commit + caret timers so a tear-down mid-edit
	// can't leave a callback pointing into freed memory.
}

void PhoneTodo::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneTodo::onStop() {
	stopTickFlash();
	Input::getInstance()->removeListener(this);
}

// ---------- builders -----------------------------------------------------

void PhoneTodo::buildListView() {
	// Caption "TODO  N/M". Cyan to match the section captions every
	// other Phone* utility app uses.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "TODO  0/8");

	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	for(uint8_t i = 0; i < VisibleRows; ++i) {
		lv_obj_t* row = lv_label_create(obj);
		lv_obj_set_style_text_font(row, &pixelbasic7, 0);
		lv_obj_set_style_text_color(row, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
		lv_obj_set_width(row, kRowWidth);
		lv_obj_set_pos(row, kRowLeftX, kRowsTopY + i * kRowStride);
		lv_label_set_text(row, "");
		rowLabels[i] = row;
	}

	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);

	// Empty-list hint label. Shown only when taskCount == 0.
	emptyHint = lv_label_create(obj);
	lv_obj_set_style_text_font(emptyHint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(emptyHint, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(emptyHint, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(emptyHint, kEmptyHintW);
	lv_obj_set_style_text_align(emptyHint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(emptyHint, kRowLeftX, kEmptyHintY);
	lv_label_set_text(emptyHint,
		"NO TASKS YET.\n"
		"PRESS \"NEW\" TO ADD\n"
		"YOUR FIRST TASK.");
	lv_obj_add_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
}

void PhoneTodo::buildEditView() {
	editCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(editCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(editCaption, MP_HIGHLIGHT, 0);
	lv_obj_set_align(editCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(editCaption, kEditCaptionY);
	lv_label_set_text(editCaption, "NEW TASK");

	t9Input = new PhoneT9Input(obj, MaxLen);
	lv_obj_set_pos(t9Input->getLvObj(),
				   (160 - PhoneT9Input::Width) / 2,
				   kEditT9Y);
	t9Input->setPlaceholder("WRITE TASK");
	t9Input->setCase(PhoneT9Input::Case::First);

	editHint = lv_label_create(obj);
	lv_obj_set_style_text_font(editHint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(editHint, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(editHint, LV_LABEL_LONG_DOT);
	lv_obj_set_width(editHint, kEditHintW);
	lv_obj_set_style_text_align(editHint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(editHint, kRowLeftX, kEditHintY);
	lv_label_set_text(editHint, "PRIO M | PRESS 5 TO CYCLE");
}

void PhoneTodo::teardownEditView() {
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

const char* PhoneTodo::getTaskText(uint8_t slot) const {
	if(slot >= taskCount) return "";
	return tasks[slot].text;
}

bool PhoneTodo::isTaskDone(uint8_t slot) const {
	if(slot >= taskCount) return false;
	return tasks[slot].done;
}

PhoneTodo::Priority PhoneTodo::getTaskPriority(uint8_t slot) const {
	if(slot >= taskCount) return Priority::Med;
	return tasks[slot].priority;
}

// ---------- repainters ---------------------------------------------------

void PhoneTodo::refreshCaption() {
	if(captionLabel == nullptr) return;
	char buf[16];
	snprintf(buf, sizeof(buf), "TODO  %u/%u",
	         (unsigned) taskCount,
	         (unsigned) MaxTasks);
	lv_label_set_text(captionLabel, buf);
}

void PhoneTodo::refreshRows() {
	for(uint8_t r = 0; r < VisibleRows; ++r) {
		lv_obj_t* row = rowLabels[r];
		if(row == nullptr) continue;

		const uint8_t slot = (uint8_t)(scrollTop + r);
		if(slot >= taskCount) {
			lv_label_set_text(row, "");
			continue;
		}

		const Task& t = tasks[slot];
		const bool isCursor = (slot == cursor) && (mode == Mode::List);

		// Format: ">[x] H Buy milk" / " [ ] L Call dentist"
		char buf[8 + MaxLen + 8] = {};
		snprintf(buf, sizeof(buf), "%c[%c] %c %s",
		         isCursor ? '>' : ' ',
		         t.done ? 'x' : ' ',
		         priorityLetter(t.priority),
		         t.text);

		// Strip newlines so a row stays a single visual line.
		for(size_t j = 0; buf[j] != '\0'; ++j) {
			if(buf[j] == '\n' || buf[j] == '\r') buf[j] = ' ';
		}

		lv_label_set_text(row, buf);

		// Colour key:
		//   done                -> dim (label dim regardless of cursor)
		//   cursor + undone     -> priority colour boosted (use HIGHLIGHT
		//                          for MED/LOW so it pops, ACCENT for
		//                          HIGH stays ACCENT; combined: cursor
		//                          paints in HIGHLIGHT for visibility,
		//                          unless priority is HIGH which keeps
		//                          ACCENT)
		//   non-cursor + undone -> priority colour (HIGH=ACCENT,
		//                          MED=HIGHLIGHT, LOW=TEXT)
		lv_color_t color;
		if(t.done) {
			color = MP_LABEL_DIM;
		} else if(isCursor) {
			// Cursor lift: HIGH stays accent, MED/LOW use highlight cyan
			// so the row visibly "lights up" under the cursor regardless
			// of its priority.
			color = (t.priority == Priority::High) ? MP_ACCENT : MP_HIGHLIGHT;
		} else {
			color = priorityColor(t.priority);
		}
		lv_obj_set_style_text_color(row, color, 0);
	}
}

void PhoneTodo::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	if(mode == Mode::List) {
		softKeys->setLeft("NEW");
		if(taskCount == 0) {
			softKeys->setRight("");
		} else {
			softKeys->setRight(tasks[cursor].done ? "UNDO" : "DONE");
		}
	} else {
		softKeys->setLeft("SAVE");
		softKeys->setRight("BACK");
	}
}

void PhoneTodo::refreshEmptyHint() {
	if(emptyHint == nullptr) return;
	if(taskCount == 0 && mode == Mode::List) {
		lv_obj_clear_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(emptyHint, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneTodo::refreshEditCaption() {
	if(editCaption == nullptr) return;
	lv_label_set_text(editCaption, editingNew ? "NEW TASK" : "EDIT TASK");
}

void PhoneTodo::refreshEditHint() {
	if(editHint == nullptr) return;
	char buf[40];
	snprintf(buf, sizeof(buf), "PRIO %c | PRESS 5 TO CYCLE",
	         priorityLetter(editingPriority));
	lv_label_set_text(editHint, buf);

	// Tint hint by priority colour so the user reads it as belonging
	// to the staged priority, not just decorative dim text.
	lv_obj_set_style_text_color(editHint, priorityColor(editingPriority), 0);
}

// ---------- model helpers ------------------------------------------------

int8_t PhoneTodo::firstEmptySlot() const {
	if(taskCount < MaxTasks) return (int8_t) taskCount;
	return -1;
}

void PhoneTodo::writeSlot(uint8_t slot, Priority pr, const char* text) {
	if(slot >= MaxTasks) return;
	if(text == nullptr) text = "";
	strncpy(tasks[slot].text, text, MaxLen);
	tasks[slot].text[MaxLen] = '\0';
	tasks[slot].priority = pr;
	// Preserve done flag if the slot already held a task; default
	// to undone for fresh appends.
	if(slot >= taskCount) {
		tasks[slot].done = false;
	}
}

void PhoneTodo::removeSlot(uint8_t slot) {
	if(slot >= taskCount) return;
	for(uint8_t i = slot; i + 1 < taskCount; ++i) {
		tasks[i] = tasks[i + 1];
	}
	if(taskCount > 0) {
		tasks[taskCount - 1].text[0]  = '\0';
		tasks[taskCount - 1].priority = Priority::Med;
		tasks[taskCount - 1].done     = false;
		--taskCount;
	}
	if(cursor >= taskCount && taskCount > 0) {
		cursor = (uint8_t)(taskCount - 1);
	}
	if(taskCount == 0) {
		cursor = 0;
		scrollTop = 0;
	}
}

void PhoneTodo::ensureCursorVisible() {
	if(taskCount == 0) {
		scrollTop = 0;
		return;
	}
	if(cursor < scrollTop) {
		scrollTop = cursor;
	} else if(cursor >= (uint8_t)(scrollTop + VisibleRows)) {
		scrollTop = (uint8_t)(cursor - (VisibleRows - 1));
	}
	const uint8_t maxScroll =
	    (taskCount > VisibleRows) ? (uint8_t)(taskCount - VisibleRows) : 0;
	if(scrollTop > maxScroll) scrollTop = maxScroll;
}

// ---------- mode transitions ---------------------------------------------

void PhoneTodo::enterList() {
	teardownEditView();

	stopTickFlash();

	mode = Mode::List;

	if(taskCount == 0) {
		cursor    = 0;
		scrollTop = 0;
	} else {
		if(cursor >= taskCount) cursor = (uint8_t)(taskCount - 1);
		ensureCursorVisible();
	}

	// Re-show the list-only widgets that may have been hidden while
	// the Edit overlay was up.
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

void PhoneTodo::enterEdit(uint8_t slot, bool prefill, bool isNew) {
	if(slot >= MaxTasks) return;

	teardownEditView();
	stopTickFlash();

	editingSlot = slot;
	editingNew  = isNew;
	mode        = Mode::Edit;

	// Stage the priority. A NEW task starts at MED; an existing task
	// preserves its current priority so EDIT is non-destructive.
	if(isNew) {
		editingPriority = Priority::Med;
	} else if(slot < taskCount) {
		editingPriority = tasks[slot].priority;
	} else {
		editingPriority = Priority::Med;
	}

	buildEditView();

	// Hide the list-only widgets behind the Edit overlay so the rows
	// + dividers + caption don't ghost through. They are re-shown on
	// enterList(). We keep the wallpaper + status bar + soft-keys up
	// so the screen stays consistent with the rest of the family.
	if(captionLabel)  lv_obj_add_flag(captionLabel,  LV_OBJ_FLAG_HIDDEN);
	if(topDivider)    lv_obj_add_flag(topDivider,    LV_OBJ_FLAG_HIDDEN);
	if(bottomDivider) lv_obj_add_flag(bottomDivider, LV_OBJ_FLAG_HIDDEN);
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		if(rowLabels[i]) lv_obj_add_flag(rowLabels[i], LV_OBJ_FLAG_HIDDEN);
	}

	refreshEditCaption();
	refreshEditHint();
	refreshSoftKeys();
	refreshEmptyHint();

	if(prefill && t9Input != nullptr && slot < taskCount && tasks[slot].text[0] != '\0') {
		t9Input->setText(String(tasks[slot].text));
	}
}

// ---------- list actions -------------------------------------------------

void PhoneTodo::moveCursor(int8_t delta) {
	if(taskCount == 0 || delta == 0) return;
	int16_t next = (int16_t) cursor + (int16_t) delta;
	if(next < 0) next = 0;
	if(next >= (int16_t) taskCount) next = (int16_t)(taskCount - 1);
	cursor = (uint8_t) next;
	ensureCursorVisible();
	refreshRows();
	refreshSoftKeys();
}

void PhoneTodo::onNewPressed() {
	if(softKeys) softKeys->flashLeft();

	int8_t target = firstEmptySlot();
	if(target < 0) {
		// All slots full — overwrite the cursor row so NEW always
		// has a target. Matches the PhoneNotepad muscle memory. We
		// don't wipe the slot in advance; if the user backs out
		// without typing anything, the original task survives. Only
		// a non-empty SAVE actually replaces the cursor row.
		if(taskCount == 0) return;
		target = (int8_t) cursor;
		enterEdit((uint8_t) target, /*prefill=*/false, /*isNew=*/true);
		return;
	}

	// Append path — `target` == taskCount, slot is already empty so
	// no wipe needed. taskCount itself is bumped only on a non-empty
	// SAVE inside onSavePressed().
	cursor = (uint8_t) target;
	enterEdit((uint8_t) target, /*prefill=*/false, /*isNew=*/true);
}

void PhoneTodo::onDonePressed() {
	if(softKeys) softKeys->flashRight();
	if(taskCount == 0) return;

	tasks[cursor].done = !tasks[cursor].done;

	// Visible-row index for the toggled task (only flash if visible).
	if(cursor >= scrollTop && cursor < (uint8_t)(scrollTop + VisibleRows)) {
		startTickFlash((uint8_t)(cursor - scrollTop));
	} else {
		// Cursor was off-screen for some reason — just refresh.
		refreshRows();
	}

	save();
	refreshSoftKeys();
}

void PhoneTodo::onEditPressed() {
	if(taskCount == 0) return;
	enterEdit(cursor, /*prefill=*/true, /*isNew=*/false);
}

void PhoneTodo::onCyclePriority() {
	if(taskCount == 0) return;
	tasks[cursor].priority = nextPriority(tasks[cursor].priority);
	save();
	refreshRows();
}

// ---------- edit actions -------------------------------------------------

void PhoneTodo::onSavePressed() {
	if(softKeys) softKeys->flashLeft();
	if(t9Input == nullptr) {
		enterList();
		return;
	}

	// Force the T9 widget to commit any in-flight pending letter so a
	// user who taps a digit and immediately hits SAVE does not lose
	// their last letter. Same gesture PhoneNotepad / PhoneContactEdit
	// use.
	t9Input->commitPending();

	char raw[MaxLen + 1] = {};
	String live = t9Input->getText();
	const size_t copyLen = (live.length() < MaxLen)
	        ? (size_t) live.length() : (size_t) MaxLen;
	memcpy(raw, live.c_str(), copyLen);
	raw[copyLen] = '\0';

	char trimmed[MaxLen + 1] = {};
	trimText(raw, trimmed, sizeof(trimmed));

	if(editingNew) {
		if(trimmed[0] != '\0') {
			// Writing a fresh task. Slot is either an append (one past
			// taskCount; bump the count) or an overwrite of the cursor
			// row when full (slot < taskCount; preserve count).
			const bool isAppend = (editingSlot >= taskCount);
			writeSlot(editingSlot, editingPriority, trimmed);
			tasks[editingSlot].done = false;
			if(isAppend) {
				taskCount = (uint8_t)(editingSlot + 1);
			}
		}
		// Empty-after-trim on the NEW path: no slot is touched. The
		// append was a peek beyond taskCount and the overwrite path
		// also leaves the existing cursor row intact (matches the
		// "press NEW + back out" muscle memory).
	} else {
		// EDIT path — preserve done flag, allow priority change, drop
		// only on explicit empty-after-trim.
		if(trimmed[0] == '\0') {
			removeSlot(editingSlot);
		} else {
			const bool wasDone = tasks[editingSlot].done;
			writeSlot(editingSlot, editingPriority, trimmed);
			tasks[editingSlot].done = wasDone;
		}
	}

	save();

	// Land the user on the saved row so they can see their write.
	if(taskCount > 0) {
		if(editingSlot < taskCount) {
			cursor = editingSlot;
		} else {
			cursor = (uint8_t)(taskCount - 1);
		}
	} else {
		cursor = 0;
	}
	enterList();
}

void PhoneTodo::onBackPressed() {
	if(softKeys) softKeys->flashRight();

	// onNewPressed never modifies any slot in advance (the append
	// peek-past-taskCount slot is already blank; the overwrite-when-
	// full path leaves the existing cursor row alone), so backing out
	// is the same in NEW and EDIT modes: just return to the list.
	enterList();
}

void PhoneTodo::onCyclePriorityEdit() {
	editingPriority = nextPriority(editingPriority);
	refreshEditHint();
}

// ---------- tick-off animation -------------------------------------------

void PhoneTodo::startTickFlash(uint8_t visibleRow) {
	stopTickFlash();
	if(visibleRow >= VisibleRows) return;
	if(rowLabels[visibleRow] == nullptr) return;

	// First repaint the row text + base colour so the flash overrides
	// the right starting state.
	refreshRows();

	// Force the row to highlight cyan as the "tick lit" colour.
	lv_obj_set_style_text_color(rowLabels[visibleRow], MP_HIGHLIGHT, 0);

	tickRow   = visibleRow;
	tickTimer = lv_timer_create(&PhoneTodo::tickTimerCb,
	                            (uint32_t) TickFlashMs, this);
	lv_timer_set_repeat_count(tickTimer, 1);
}

void PhoneTodo::stopTickFlash() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
	tickRow = 0xFF;
}

void PhoneTodo::tickTimerCb(lv_timer_t* t) {
	auto* self = static_cast<PhoneTodo*>(t->user_data);
	if(self == nullptr) return;

	// LVGL has already auto-deleted the timer because repeat_count was
	// 1, so just clear our handle and revert the row colour.
	self->tickTimer = nullptr;
	self->tickRow   = 0xFF;
	self->refreshRows();
}

// ---------- persistence --------------------------------------------------

void PhoneTodo::load() {
	if(!ensureOpen()) return;

	// First read header to know the count.
	uint8_t header[kHeaderBytes] = {};
	size_t  headerSize = sizeof(header);
	auto err = nvs_get_blob(s_handle, kBlobKey, header, &headerSize);
	if(err != ESP_OK)             return;
	if(headerSize < kHeaderBytes) return;
	if(header[0] != kMagic0)      return;
	if(header[1] != kMagic1)      return;
	if(header[2] != kVersion)     return;

	uint8_t storedCount = header[3];
	if(storedCount > MaxTasks) storedCount = MaxTasks;

	if(storedCount == 0) {
		taskCount = 0;
		return;
	}

	// Now read the full blob — re-query its length so we can size the
	// scratch buffer.
	size_t blobSize = 0;
	err = nvs_get_blob(s_handle, kBlobKey, nullptr, &blobSize);
	if(err != ESP_OK || blobSize == 0) return;
	const size_t kMaxBlob = kHeaderBytes + (size_t)MaxTasks * (kTaskHeader + (size_t)MaxLen);
	if(blobSize > kMaxBlob) return;

	uint8_t buf[kHeaderBytes + (size_t)MaxTasks * (kTaskHeader + (size_t)MaxLen)] = {};
	size_t  readLen = blobSize;
	err = nvs_get_blob(s_handle, kBlobKey, buf, &readLen);
	if(err != ESP_OK) return;
	if(readLen < kHeaderBytes) return;

	size_t off = kHeaderBytes;
	uint8_t loaded = 0;
	for(uint8_t i = 0; i < storedCount; ++i) {
		if(off + kTaskHeader > readLen) break;
		const uint8_t flags  = buf[off++];
		const uint8_t txtLen = buf[off++];
		if(txtLen > MaxLen)               break;
		if(off + txtLen > readLen)        break;

		Priority pr;
		bool done;
		decodeFlags(flags, pr, done);

		Task& t = tasks[loaded];
		memcpy(t.text, &buf[off], txtLen);
		t.text[txtLen] = '\0';
		t.priority = pr;
		t.done     = done;

		off += txtLen;
		++loaded;
	}
	taskCount = loaded;
}

void PhoneTodo::save() {
	if(!ensureOpen()) return;

	uint8_t buf[kHeaderBytes + (size_t)MaxTasks * (kTaskHeader + (size_t)MaxLen)] = {};
	size_t off = 0;

	buf[off++] = kMagic0;
	buf[off++] = kMagic1;
	buf[off++] = kVersion;
	buf[off++] = (uint8_t) taskCount;

	for(uint8_t i = 0; i < taskCount; ++i) {
		const Task& t = tasks[i];
		const size_t srcLen = strnlen(t.text, MaxLen);
		const uint8_t txtLen = (uint8_t) srcLen;

		buf[off++] = encodeFlags(t.priority, t.done);
		buf[off++] = txtLen;
		memcpy(&buf[off], t.text, txtLen);
		off += txtLen;
	}

	auto err = nvs_set_blob(s_handle, kBlobKey, buf, off);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneTodo", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneTodo", "nvs_commit failed: %d", (int)err);
	}
}

// ---------- input --------------------------------------------------------

void PhoneTodo::buttonPressed(uint i) {
	if(mode == Mode::List) {
		// BTN_UP / BTN_DOWN are aliased to BTN_LEFT / BTN_RIGHT in
		// Pins.hpp (same constraint PhoneNotepad documents), so cursor
		// navigation is bound to BTN_2 / BTN_8 + the L / R bumpers.
		switch(i) {
			case BTN_2:
			case BTN_L:
				moveCursor(-1);
				break;
			case BTN_8:
				moveCursor(+1);
				break;
			case BTN_R:
				// In list view, R is "cycle priority" rather than
				// scroll-down so the user can re-prioritise without
				// leaving List.
				onCyclePriority();
				break;
			case BTN_5:
				onEditPressed();
				break;
			case BTN_LEFT:
				onNewPressed();
				break;
			case BTN_RIGHT:
			case BTN_ENTER:
				onDonePressed();
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
		case BTN_5:
			// Special chord: cycle the staged priority instead of
			// emitting a T9 letter. Matches the in-edit hint strip.
			onCyclePriorityEdit();
			break;
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

void PhoneTodo::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		backLongFired = true;
		if(softKeys) softKeys->flashRight();
		pop();
	}
}

void PhoneTodo::buttonReleased(uint i) {
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
