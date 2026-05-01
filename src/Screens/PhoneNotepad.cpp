#include "PhoneNotepad.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneT9Input.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the notepad slots in beside PhoneCalculator (S60) / PhoneStopwatch (S61) /
// PhoneTimer (S62) / PhoneCalendar (S63) without a visual seam. Inlined per
// the established pattern (see PhoneCalculator.cpp / PhoneStopwatch.cpp).
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// ---------- geometry ------------------------------------------------------
//
// 160 x 128 layout. Vertical budget for List view:
//   y =  0..  9   PhoneStatusBar (10 px)
//   y = 12        caption "NOTES  N/4" (pixelbasic7, ~7 px tall)
//   y = 22        top divider rule (1 px)
//   y = 26        row 0 (12 px row stride)
//   y = 38        row 1
//   y = 50        row 2
//   y = 62        row 3
//   y = 76        bottom divider rule (1 px)
//   y = 118..127  PhoneSoftKeyBar (10 px)
//
// 12 px row stride leaves a 5 px gutter under the bottom row before the
// soft-key bar so the divider rule has breathing room. The row labels
// are pixelbasic7 (~8 px tall) so 12 px rows are tightly packed without
// glyph collisions.

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kTopDividerY   = 22;
static constexpr lv_coord_t kRowsTopY      = 26;
static constexpr lv_coord_t kRowStride     = 12;
static constexpr lv_coord_t kBotDividerY   = 76;
static constexpr lv_coord_t kRowLeftX      = 6;
static constexpr lv_coord_t kRowWidth      = 148;

// Edit-view layout.
//   y = 12       "NOTE n/4" caption (pixelbasic7)
//   y = 24       PhoneT9Input (Width x Height + HelpHeight = 156 x 30)
//   y = 60       char counter "X of 120 chars"
//   y = 118..127 PhoneSoftKeyBar
static constexpr lv_coord_t kEditCaptionY  = 12;
static constexpr lv_coord_t kEditT9Y       = 24;
static constexpr lv_coord_t kEditCounterY  = 60;
static constexpr lv_coord_t kEditCounterW  = 148;

// ---------- ctor / dtor ---------------------------------------------------

PhoneNotepad::PhoneNotepad()
		: LVScreen() {

	// Zero the notes table so an early refresh sees stable empty slots.
	for(uint8_t i = 0; i < MaxNotes; ++i) notes[i][0] = '\0';

	// Pre-seed slot 0 with a friendly hint note so the first run has
	// something to look at rather than four "(empty)" rows. This is
	// the same demo-data convention PhoneContactsScreen uses for its
	// fallback list.
	strncpy(notes[0], "Welcome to Notes", MaxNoteLen);
	notes[0][MaxNoteLen] = '\0';

	// Full-screen container, blank canvas - same pattern every Phone*
	// screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	// Bottom soft-key bar. List-view labels by default; refreshSoftKeys()
	// rewrites them whenever the mode changes.
	softKeys = new PhoneSoftKeyBar(obj);

	buildListView();

	// Long-press threshold matches the rest of the MAKERphone shell so
	// the gesture feels identical from any screen.
	setButtonHoldTime(BTN_BACK, BackHoldMs);

	// Initial paint -- enter List mode explicitly so the soft-key labels
	// + row colours + caption all match.
	enterList();
}

PhoneNotepad::~PhoneNotepad() {
	// All children (wallpaper, status bar, soft-keys, labels, optional
	// PhoneT9Input) are parented to obj and freed by the LVScreen base
	// destructor. The PhoneT9Input destructor itself cancels its commit
	// + caret timers so a tear-down mid-edit cannot leave a callback
	// pointing into freed memory.
}

void PhoneNotepad::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneNotepad::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- builders ------------------------------------------------------

void PhoneNotepad::buildListView() {
	// Caption - "NOTES  N/4". Painted in cyan to match the section
	// captions every other Phone* utility app uses.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "NOTES  0/4");

	// Top divider rule below the caption.
	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	// Pre-allocate the row labels. We never grow / shrink this array;
	// rows that have no backing note simply paint "(empty)" in dim.
	for(uint8_t i = 0; i < MaxNotes; ++i) {
		lv_obj_t* row = lv_label_create(obj);
		lv_obj_set_style_text_font(row, &pixelbasic7, 0);
		lv_obj_set_style_text_color(row, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
		lv_obj_set_width(row, kRowWidth);
		lv_obj_set_pos(row, kRowLeftX, kRowsTopY + i * kRowStride);
		lv_label_set_text(row, "");
		rowLabels[i] = row;
	}

	// Bottom divider rule above the soft-keys.
	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);
}

void PhoneNotepad::buildEditView() {
	// Caption - "NOTE n/4". Cyan to match the List caption tone so the
	// user reads them as the same family of headers.
	editCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(editCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(editCaption, MP_HIGHLIGHT, 0);
	lv_obj_set_align(editCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(editCaption, kEditCaptionY);
	lv_label_set_text(editCaption, "NOTE 1/4");

	// PhoneT9Input - canonical S32 multi-tap entry. Caps at MaxNoteLen.
	t9Input = new PhoneT9Input(obj, MaxNoteLen);
	lv_obj_set_pos(t9Input->getLvObj(),
				   (160 - PhoneT9Input::Width) / 2,
				   kEditT9Y);
	t9Input->setPlaceholder("WRITE NOTE");
	t9Input->setCase(PhoneT9Input::Case::First);

	// Char counter strip - dim hint line under the T9 input. Repaints
	// from the onTextChanged callback so the user gets live feedback
	// as they multi-tap.
	charCounter = lv_label_create(obj);
	lv_obj_set_style_text_font(charCounter, &pixelbasic7, 0);
	lv_obj_set_style_text_color(charCounter, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(charCounter, LV_LABEL_LONG_DOT);
	lv_obj_set_width(charCounter, kEditCounterW);
	lv_obj_set_style_text_align(charCounter, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(charCounter, kRowLeftX, kEditCounterY);
	lv_label_set_text(charCounter, "0 of 120 chars");

	// Wire the live-update callbacks. We capture `this` so the lambdas
	// can re-paint the counter without going through globals. The T9
	// widget owns the std::function copy so the pointer stays valid for
	// the widget's lifetime, which matches this screen's Edit mode.
	auto self = this;
	t9Input->setOnTextChanged([self](const String& text) {
		(void) text;
		self->refreshCharCounter();
	});
}

void PhoneNotepad::teardownEditView() {
	// The PhoneT9Input owns its lv_obj_t children, so deleting the
	// wrapper object is sufficient -- LVGL recursively frees the
	// underlying tree, and the wrapper's destructor cancels its caret
	// + commit timers.
	if(t9Input != nullptr) {
		delete t9Input;
		t9Input = nullptr;
	}

	if(editCaption != nullptr) {
		lv_obj_del(editCaption);
		editCaption = nullptr;
	}

	if(charCounter != nullptr) {
		lv_obj_del(charCounter);
		charCounter = nullptr;
	}
}

// ---------- public introspection -----------------------------------------

uint8_t PhoneNotepad::getNoteCount() const {
	uint8_t count = 0;
	for(uint8_t i = 0; i < MaxNotes; ++i) {
		if(notes[i][0] != '\0') ++count;
	}
	return count;
}

const char* PhoneNotepad::getNoteText(uint8_t slot) const {
	if(slot >= MaxNotes) return "";
	return notes[slot];
}

bool PhoneNotepad::isSlotFilled(uint8_t slot) const {
	if(slot >= MaxNotes) return false;
	return notes[slot][0] != '\0';
}

// ---------- static helpers -----------------------------------------------

void PhoneNotepad::trimText(const char* in, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(in == nullptr) {
		out[0] = '\0';
		return;
	}

	// Skip leading whitespace.
	while(*in != '\0' && isspace((unsigned char) *in)) ++in;

	// Find the trailing whitespace boundary so we can copy without it.
	const char* end = in + strlen(in);
	while(end > in && isspace((unsigned char) *(end - 1))) --end;

	const size_t srcLen = (size_t) (end - in);
	const size_t copyLen = (srcLen < outLen - 1) ? srcLen : (outLen - 1);
	memcpy(out, in, copyLen);
	out[copyLen] = '\0';
}

// ---------- repainters ---------------------------------------------------

void PhoneNotepad::refreshCaption() {
	if(captionLabel == nullptr) return;
	char buf[16];
	snprintf(buf, sizeof(buf), "NOTES  %u/%u",
			 (unsigned) getNoteCount(),
			 (unsigned) MaxNotes);
	lv_label_set_text(captionLabel, buf);
}

void PhoneNotepad::refreshRows() {
	for(uint8_t i = 0; i < MaxNotes; ++i) {
		lv_obj_t* row = rowLabels[i];
		if(row == nullptr) continue;

		const bool isCursor = (i == cursor) && (mode == Mode::List);
		const bool isFilled = isSlotFilled(i);

		// Build the row text. We render slot index 1-based to match the
		// caption count, with a leading chevron when the cursor is on
		// the row so the active row reads as selected even on a
		// monochrome reading of the screen.
		char buf[PreviewChars + 16] = {};
		if(isFilled) {
			char preview[PreviewChars + 1] = {};
			// Hard-truncate to PreviewChars so the row stays in width.
			const size_t srcLen = strlen(notes[i]);
			const size_t copyLen = (srcLen < PreviewChars)
					? srcLen : (size_t) PreviewChars;
			memcpy(preview, notes[i], copyLen);
			preview[copyLen] = '\0';

			// Replace any newlines in the preview with spaces so the
			// row stays a single visual line.
			for(size_t j = 0; j < copyLen; ++j) {
				if(preview[j] == '\n' || preview[j] == '\r') preview[j] = ' ';
			}

			snprintf(buf, sizeof(buf), "%c%u. %s",
					 isCursor ? '>' : ' ',
					 (unsigned) (i + 1),
					 preview);
		}else{
			snprintf(buf, sizeof(buf), "%c%u. (empty)",
					 isCursor ? '>' : ' ',
					 (unsigned) (i + 1));
		}

		lv_label_set_text(row, buf);

		// Colour key:
		//   cursor row, filled  -> cyan highlight
		//   cursor row, empty   -> sunset orange (so an empty cursor
		//                          still reads as selected without
		//                          collision-ing with the dim "(empty)"
		//                          tone of inactive empty rows)
		//   non-cursor, filled  -> cream
		//   non-cursor, empty   -> dim
		lv_color_t color;
		if(isCursor && isFilled)       color = MP_HIGHLIGHT;
		else if(isCursor && !isFilled) color = MP_ACCENT;
		else if(isFilled)              color = MP_TEXT;
		else                            color = MP_LABEL_DIM;
		lv_obj_set_style_text_color(row, color, 0);
	}
}

void PhoneNotepad::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	if(mode == Mode::List) {
		softKeys->setLeft("NEW");
		softKeys->setRight("OPEN");
	}else{
		softKeys->setLeft("SAVE");
		softKeys->setRight("BACK");
	}
}

void PhoneNotepad::refreshEditCaption() {
	if(editCaption == nullptr) return;
	char buf[16];
	snprintf(buf, sizeof(buf), "NOTE %u/%u",
			 (unsigned) (editingSlot + 1),
			 (unsigned) MaxNotes);
	lv_label_set_text(editCaption, buf);
}

void PhoneNotepad::refreshCharCounter() {
	if(charCounter == nullptr || t9Input == nullptr) return;
	const String text = t9Input->getText();
	char buf[24];
	snprintf(buf, sizeof(buf), "%u of %u chars",
			 (unsigned) text.length(),
			 (unsigned) MaxNoteLen);
	lv_label_set_text(charCounter, buf);
}

// ---------- model helpers ------------------------------------------------

int8_t PhoneNotepad::firstEmptySlot() const {
	for(uint8_t i = 0; i < MaxNotes; ++i) {
		if(notes[i][0] == '\0') return (int8_t) i;
	}
	return -1;
}

void PhoneNotepad::clearSlot(uint8_t slot) {
	if(slot >= MaxNotes) return;
	notes[slot][0] = '\0';
}

void PhoneNotepad::writeSlot(uint8_t slot, const char* text) {
	if(slot >= MaxNotes) return;
	if(text == nullptr) {
		notes[slot][0] = '\0';
		return;
	}
	strncpy(notes[slot], text, MaxNoteLen);
	notes[slot][MaxNoteLen] = '\0';
}

// ---------- mode transitions ---------------------------------------------

void PhoneNotepad::enterList() {
	// If we are coming back from Edit, drop the editor widgets. Always
	// safe -- teardown is a no-op when nothing is mounted.
	teardownEditView();

	mode = Mode::List;

	// Clamp the cursor in case a slot we used to hover got cleared.
	if(cursor >= MaxNotes) cursor = MaxNotes - 1;

	refreshCaption();
	refreshRows();
	refreshSoftKeys();
}

void PhoneNotepad::enterEdit(uint8_t slot, bool prefill) {
	if(slot >= MaxNotes) return;

	// If we already have an editor mounted (defensive: hot-restart),
	// tear it down before mounting a fresh one.
	teardownEditView();

	editingSlot = slot;
	mode = Mode::Edit;

	buildEditView();
	refreshEditCaption();

	if(prefill && t9Input != nullptr && notes[slot][0] != '\0') {
		t9Input->setText(String(notes[slot]));
	}

	refreshCharCounter();
	refreshSoftKeys();
}

// ---------- list actions -------------------------------------------------

void PhoneNotepad::moveCursor(int8_t delta) {
	if(delta == 0) return;
	int16_t next = (int16_t) cursor + (int16_t) delta;
	while(next < 0) next += MaxNotes;
	while(next >= (int16_t) MaxNotes) next -= MaxNotes;
	cursor = (uint8_t) next;
	refreshRows();
}

void PhoneNotepad::onNewPressed() {
	if(softKeys) softKeys->flashLeft();

	// Pick the first empty slot. If every slot is full, overwrite the
	// cursor row so the user can always create a new note (matches the
	// "press NEW" muscle-memory rather than silently failing).
	int8_t target = firstEmptySlot();
	if(target < 0) target = (int8_t) cursor;

	cursor = (uint8_t) target;
	enterEdit((uint8_t) target, /*prefill=*/false);
}

void PhoneNotepad::onOpenPressed() {
	if(softKeys) softKeys->flashRight();

	// Open is a no-op flash on an empty slot -- no point in entering an
	// editor when there is nothing to read. The user has NEW for that.
	if(!isSlotFilled(cursor)) return;

	enterEdit(cursor, /*prefill=*/true);
}

// ---------- edit actions -------------------------------------------------

void PhoneNotepad::onSavePressed() {
	if(softKeys) softKeys->flashLeft();
	if(t9Input == nullptr) {
		enterList();
		return;
	}

	// Force the T9 widget to commit any in-flight pending letter so a
	// user who taps a digit and immediately hits SAVE does not lose
	// their last letter. Same gesture PhoneContactEdit uses.
	t9Input->commitPending();

	// Snapshot + trim into a stack buffer before back-writing.
	char raw[MaxNoteLen + 1] = {};
	String live = t9Input->getText();
	const size_t copyLen = (live.length() < MaxNoteLen)
			? (size_t) live.length() : (size_t) MaxNoteLen;
	memcpy(raw, live.c_str(), copyLen);
	raw[copyLen] = '\0';

	char trimmed[MaxNoteLen + 1] = {};
	trimText(raw, trimmed, sizeof(trimmed));

	// Empty-after-trim clears the slot rather than persisting whitespace.
	if(trimmed[0] == '\0') {
		clearSlot(editingSlot);
	}else{
		writeSlot(editingSlot, trimmed);
	}

	// Land the user back on the saved row so they can immediately see
	// their write on the list.
	cursor = editingSlot;
	enterList();
}

void PhoneNotepad::onBackPressed() {
	if(softKeys) softKeys->flashRight();
	enterList();
}

// ---------- input --------------------------------------------------------

void PhoneNotepad::buttonPressed(uint i) {
	if(mode == Mode::List) {
		// Note: BTN_UP / BTN_DOWN are aliased to BTN_LEFT / BTN_RIGHT
		// in Pins.hpp, so we can't list them as separate switch cases
		// without a duplicate-label compile error. Cursor navigation
		// is instead bound to BTN_2 / BTN_8 (the dialer-arrow chord
		// every other Phase-L app uses) and the L / R bumpers.
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
			case BTN_ENTER:
				onOpenPressed();
				break;
			default:
				break;
		}
		return;
	}

	// Edit mode: route digits + bumpers through the T9 funnel exactly
	// the way PhoneContactEdit does. BTN_BACK / softkey RIGHT are
	// handled in buttonReleased so a long-press can pre-empt them.
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
			// Bumper L: T9 backspace -- cancels a pending letter or
			// erases the last committed character.
			if(t9Input) t9Input->keyPress('*');
			break;

		case BTN_R:
			// Bumper R: T9 case toggle (Abc -> ABC -> abc).
			if(t9Input) t9Input->keyPress('#');
			break;

		case BTN_ENTER:
			// Lock in the in-flight pending letter without leaving Edit.
			if(t9Input) t9Input->commitPending();
			break;

		case BTN_LEFT:
			onSavePressed();
			break;

		case BTN_RIGHT:
			// Defer to buttonReleased so a long-press can pre-empt.
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneNotepad::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		// Hold-BACK = bail to the parent screen, regardless of mode.
		// Same convention every Phase-D / Phase-F / Phase-L screen
		// uses, so muscle memory transfers cleanly.
		backLongFired = true;
		if(softKeys) softKeys->flashRight();
		pop();
	}
}

void PhoneNotepad::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		// Short-press BACK: in Edit mode it discards the in-flight
		// edit and returns to List; in List mode it pops the screen.
		// The long-press path is suppressed via backLongFired so a
		// hold does not double-fire on release.
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		if(mode == Mode::Edit) {
			onBackPressed();
		}else{
			if(softKeys) softKeys->flashRight();
			pop();
		}
		return;
	}

	if(i == BTN_RIGHT && mode == Mode::Edit) {
		// Right softkey in Edit = BACK. Suppressed if a long-press
		// already exited the screen on the same hold cycle.
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		onBackPressed();
		return;
	}
}
