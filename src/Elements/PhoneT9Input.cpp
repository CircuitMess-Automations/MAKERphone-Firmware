#include "PhoneT9Input.h"
#include "../Fonts/font.h"
#include <ctype.h>
#include <string.h>

// MAKERphone retro palette - identical to every other Phone* widget.
// Kept duplicated rather than centralised at this small scale so each
// widget is self-contained; if the palette ever moves out of widgets,
// every widget moves together.
#define MP_BG_DARK      lv_color_make(20, 12, 36)     // deep purple slab
#define MP_ACCENT       lv_color_make(255, 140, 30)   // sunset orange (caret + pending underline)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)  // cyan (case label)
#define MP_DIM          lv_color_make(70, 56, 100)    // muted purple (idle border, help strip bg)
#define MP_TEXT         lv_color_make(255, 220, 180)  // warm cream committed text
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)  // dim cream placeholder

// Canonical ITU-T E.161 / Sony-Ericsson keymap. Index = digit (0..9).
// '1' carries the standard punctuation cluster every classic phone
// supported. '0' carries space + '0'. Each ring ends with the digit
// itself so a sequence of N taps eventually loops back to "type the
// number literally" - same affordance every feature phone shipped.
static const char* kKeyLetters[10] = {
		" 0",        // 0
		".,?!1",     // 1
		"abc2",      // 2
		"def3",      // 3
		"ghi4",      // 4
		"jkl5",      // 5
		"mno6",      // 6
		"pqrs7",     // 7
		"tuv8",      // 8
		"wxyz9"      // 9
};

const char* PhoneT9Input::lettersForKey(uint8_t keyIndex){
	if(keyIndex >= 10) return "";
	return kKeyLetters[keyIndex];
}

PhoneT9Input::PhoneT9Input(lv_obj_t* parent, uint16_t maxLengthIn)
		: LVObject(parent), maxLength(maxLengthIn){

	lv_obj_set_size(obj, Width, Height + HelpHeight);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

	buildBackground();
	buildTextLabel();
	buildPlaceholder();
	buildCaret();
	buildPendingStrip();

	// Caret blinks from construction so the empty entry feels alive.
	armCaretTimer();
	refreshDisplay();
	refreshCaseLabel();
	refreshPlaceholder();
}

PhoneT9Input::~PhoneT9Input(){
	cancelCommitTimer();
	cancelCaretTimer();
}

// ----- builders -----

void PhoneT9Input::buildBackground(){
	// Two-tone slab: top Height px = entry, bottom HelpHeight px = pending
	// strip. We render them as one rounded slab with a 1 px divider rule
	// drawn by the pending strip itself so we don't pay for two rounded
	// corner draws.
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, 2, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_color(obj, MP_DIM, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
}

void PhoneT9Input::buildTextLabel(){
	textLabel = lv_label_create(obj);
	lv_obj_add_flag(textLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(textLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(textLabel, MP_TEXT, 0);
	// Long lines clip rather than wrap - the entry is single-line; an
	// SMS that exceeds the visible width simply scrolls left as more
	// characters arrive (the caret stays at the right edge so the user
	// always sees their latest keystroke).
	lv_label_set_long_mode(textLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(textLabel, Width - 6);
	lv_obj_set_align(textLabel, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(textLabel, 3, 4);
	lv_label_set_text(textLabel, "");
}

void PhoneT9Input::buildPlaceholder(){
	placeholderLabel = lv_label_create(obj);
	lv_obj_add_flag(placeholderLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(placeholderLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(placeholderLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(placeholderLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(placeholderLabel, Width - 6);
	lv_obj_set_align(placeholderLabel, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(placeholderLabel, 3, 4);
	lv_label_set_text(placeholderLabel, "");
	lv_obj_add_flag(placeholderLabel, LV_OBJ_FLAG_HIDDEN);
}

void PhoneT9Input::buildCaret(){
	// 1 px wide, 8 px tall vertical bar drawn as a tinted lv_obj. We
	// move it horizontally to sit just after the rendered text and toggle
	// its visibility on a 450 ms timer for the classic phone caret blink.
	caret = lv_obj_create(obj);
	lv_obj_remove_style_all(caret);
	lv_obj_clear_flag(caret, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(caret, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(caret, 1, 9);
	lv_obj_set_style_bg_color(caret, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(caret, LV_OPA_COVER, 0);
	lv_obj_set_align(caret, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(caret, 3, 4);
}

void PhoneT9Input::buildPendingStrip(){
	pendingStrip = lv_obj_create(obj);
	lv_obj_remove_style_all(pendingStrip);
	lv_obj_clear_flag(pendingStrip, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(pendingStrip, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(pendingStrip, Width, HelpHeight);
	lv_obj_set_style_bg_color(pendingStrip, MP_DIM, 0);
	lv_obj_set_style_bg_opa(pendingStrip, LV_OPA_COVER, 0);
	lv_obj_set_align(pendingStrip, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(pendingStrip, 0, Height);

	pendingLabel = lv_label_create(pendingStrip);
	lv_obj_add_flag(pendingLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(pendingLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(pendingLabel, MP_TEXT, 0);
	lv_obj_set_align(pendingLabel, LV_ALIGN_LEFT_MID);
	lv_obj_set_x(pendingLabel, 3);
	lv_label_set_text(pendingLabel, "");

	caseLabel = lv_label_create(pendingStrip);
	lv_obj_add_flag(caseLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(caseLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(caseLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(caseLabel, LV_ALIGN_RIGHT_MID);
	lv_obj_set_x(caseLabel, -3);
	lv_label_set_text(caseLabel, "Abc");
}

// ----- public API -----

void PhoneT9Input::keyPress(char glyph){
	// '*' = backspace, '#' = case toggle. Digits drive the multi-tap
	// state machine. Anything else (LR softkeys, BTN_BACK, BTN_ENTER...)
	// is the host's responsibility.
	if(glyph == '*'){
		backspace();
		return;
	}
	if(glyph == '#'){
		// Toggling case mid-cycle commits the pending letter first so
		// we never end up with a half-cased pending letter visible.
		commitPendingInternal(true);
		cycleCase();
		return;
	}
	if(glyph < '0' || glyph > '9') return;

	const int8_t keyIndex = (int8_t) (glyph - '0');

	if(pendingActive && keyIndex == pendingKeyIndex){
		advancePendingCycle();
	}else{
		// Different key (or first tap after idle): commit any pending
		// letter, then start a fresh cycle on the new key. The full text
		// length is checked here because committing the current pending
		// promotes it from pending into committed - if that already
		// fills the buffer to maxLength we silently drop the new tap.
		commitPendingInternal(true);
		if(committed.length() >= maxLength) return;
		startPendingCycle(keyIndex);
	}

	armCommitTimer();
	caretVisible = true; // stop the blink mid-cycle for visual stability
	if(caret) lv_obj_clear_flag(caret, LV_OBJ_FLAG_HIDDEN);
	refreshDisplay();
	// Fire after refreshDisplay() because that's the call that updates
	// `text` to include the latest pending letter. Hosts that listen on
	// onTextChanged see every visible mutation - same-key cycling
	// included.
	if(onTextChanged) onTextChanged(text);
}

void PhoneT9Input::backspace(){
	if(pendingActive){
		// Cancel pending letter without touching committed text.
		cancelPending();
		refreshDisplay();
		if(onTextChanged) onTextChanged(text);
		return;
	}
	if(committed.length() == 0) return;
	committed.remove(committed.length() - 1);
	text = committed;
	// Sentence-terminator detection so "First" mode resets to upper
	// after the user backspaces over a period etc. Cheap to recompute
	// from scratch every time.
	const char last = committed.length() == 0
			? '.'
			: committed.charAt(committed.length() - 1);
	nextStartsUpper = (last == '.' || last == '!' || last == '?');
	refreshDisplay();
	if(onTextChanged) onTextChanged(text);
}

void PhoneT9Input::clear(){
	cancelPending();
	committed = "";
	text = "";
	nextStartsUpper = true;
	refreshDisplay();
	if(onTextChanged) onTextChanged(text);
}

void PhoneT9Input::commitPending(){
	commitPendingInternal(true);
	cancelCommitTimer();
	refreshDisplay();
}

void PhoneT9Input::setText(const String& s){
	cancelPending();
	committed = s.length() <= maxLength ? s : s.substring(0, maxLength);
	text = committed;
	nextStartsUpper = committed.length() == 0;
	refreshDisplay();
	if(onTextChanged) onTextChanged(text);
}

void PhoneT9Input::setPlaceholder(const String& s){
	placeholder = s;
	refreshPlaceholder();
}

void PhoneT9Input::setCase(Case c){
	if(caseMode == c) return;
	caseMode = c;
	refreshCaseLabel();
}

void PhoneT9Input::cycleCase(){
	switch(caseMode){
		case Case::Lower: caseMode = Case::First; break;
		case Case::First: caseMode = Case::Upper; break;
		case Case::Upper: caseMode = Case::Lower; break;
	}
	refreshCaseLabel();
}

// ----- internal: pending-letter state machine -----

void PhoneT9Input::startPendingCycle(int8_t keyIndex){
	pendingActive = true;
	pendingKeyIndex = keyIndex;
	pendingCharIndex = 0;
}

void PhoneT9Input::advancePendingCycle(){
	const char* ring = lettersForKey((uint8_t) pendingKeyIndex);
	const size_t len = strlen(ring);
	if(len == 0) return;
	pendingCharIndex = (uint8_t) ((pendingCharIndex + 1) % len);
}

void PhoneT9Input::commitPendingInternal(bool fireCallback){
	if(!pendingActive) return;

	const char* ring = lettersForKey((uint8_t) pendingKeyIndex);
	const size_t len = strlen(ring);
	if(len == 0){
		// Defensive: shouldn't happen because keyPress() never starts a
		// cycle on an empty ring. Just unwind.
		pendingActive = false;
		pendingKeyIndex = -1;
		pendingCharIndex = 0;
		return;
	}
	const char base = ring[pendingCharIndex];
	const char ch = applyCase(base);

	if(committed.length() < maxLength){
		committed += ch;
	}

	pendingActive = false;
	pendingKeyIndex = -1;
	pendingCharIndex = 0;

	// Update sentence-terminator tracking for "First" mode. After
	// '.', '!' or '?' the next pending letter starts uppercase again.
	// Spaces are treated as word boundaries within a sentence and DO
	// NOT re-capitalise (iOS convention which most users expect).
	nextStartsUpper = (ch == '.' || ch == '!' || ch == '?');

	text = committed;
	if(fireCallback){
		if(onCommit) onCommit(text);
		if(onTextChanged) onTextChanged(text);
	}
}

void PhoneT9Input::cancelPending(){
	pendingActive = false;
	pendingKeyIndex = -1;
	pendingCharIndex = 0;
	cancelCommitTimer();
	text = committed;
}

// ----- internal: timers -----

void PhoneT9Input::armCommitTimer(){
	cancelCommitTimer();
	commitTimer = lv_timer_create(commitTimerCb, CycleMs, this);
	lv_timer_set_repeat_count(commitTimer, 1);
}

void PhoneT9Input::cancelCommitTimer(){
	if(commitTimer != nullptr){
		lv_timer_del(commitTimer);
		commitTimer = nullptr;
	}
}

void PhoneT9Input::armCaretTimer(){
	cancelCaretTimer();
	caretTimer = lv_timer_create(caretTimerCb, CaretMs, this);
}

void PhoneT9Input::cancelCaretTimer(){
	if(caretTimer != nullptr){
		lv_timer_del(caretTimer);
		caretTimer = nullptr;
	}
}

void PhoneT9Input::commitTimerCb(lv_timer_t* timer){
	auto* self = static_cast<PhoneT9Input*>(timer->user_data);
	self->commitTimer = nullptr; // LVGL deletes the timer on its own when repeat_count hits 0
	self->commitPendingInternal(true);
	self->refreshDisplay();
}

void PhoneT9Input::caretTimerCb(lv_timer_t* timer){
	auto* self = static_cast<PhoneT9Input*>(timer->user_data);
	if(self->caret == nullptr) return;
	// Caret stays on while a pending letter is in progress so the user
	// can clearly see "this letter isn't committed yet". Outside of a
	// pending cycle, simple square-wave blink.
	if(self->pendingActive){
		self->caretVisible = true;
		lv_obj_clear_flag(self->caret, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	self->caretVisible = !self->caretVisible;
	if(self->caretVisible){
		lv_obj_clear_flag(self->caret, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_add_flag(self->caret, LV_OBJ_FLAG_HIDDEN);
	}
}

// ----- internal: rendering -----

void PhoneT9Input::refreshDisplay(){
	if(textLabel == nullptr) return;

	// Build the visible string: committed + the pending letter (if any).
	String shown = committed;
	if(pendingActive){
		const char* ring = lettersForKey((uint8_t) pendingKeyIndex);
		const size_t len = strlen(ring);
		if(len > 0){
			shown += applyCase(ring[pendingCharIndex]);
		}
	}
	text = shown;
	lv_label_set_text(textLabel, shown.c_str());

	// Position the caret right after the rendered text. We measure the
	// rendered width via the same trick PhoneChatBubble uses: a fresh
	// SIZE_CONTENT layout pass on the label, then read its natural
	// width back out. This avoids reaching for lv_txt_get_size (which
	// is technically public in LVGL 8.x but not used anywhere else in
	// this firmware) and keeps our surface area smaller.
	lv_obj_set_width(textLabel, LV_SIZE_CONTENT);
	lv_obj_update_layout(textLabel);
	const lv_coord_t natural = lv_obj_get_width(textLabel);
	lv_obj_set_width(textLabel, Width - 6);
	const int16_t maxX = (int16_t) (Width - 6);
	int16_t caretX = (int16_t) (3 + natural);
	if(caretX > maxX) caretX = maxX;
	if(caret){
		lv_obj_set_pos(caret, caretX, 4);
	}

	// Hide caret when there's nothing to anchor it next to AND the
	// placeholder is showing - putting caret + placeholder together
	// looks busy. We still keep the caret visible for an empty buffer
	// without placeholder so the user sees "ready for input".
	refreshPlaceholder();

	// Pending strip content: "abc -> [a]bc" style helper that highlights
	// which letter the user is currently on. When idle, leave it empty.
	if(pendingLabel){
		if(pendingActive){
			const char* ring = lettersForKey((uint8_t) pendingKeyIndex);
			const size_t len = strlen(ring);
			String help;
			help.reserve(len + 4);
			for(size_t i = 0; i < len; ++i){
				char ch = applyCase(ring[i]);
				if(i == pendingCharIndex){
					help += '[';
					help += ch;
					help += ']';
				}else{
					help += ch;
				}
			}
			lv_label_set_text(pendingLabel, help.c_str());
		}else{
			lv_label_set_text(pendingLabel, "");
		}
	}
}

void PhoneT9Input::refreshCaseLabel(){
	if(caseLabel == nullptr) return;
	switch(caseMode){
		case Case::Lower: lv_label_set_text(caseLabel, "abc"); break;
		case Case::First: lv_label_set_text(caseLabel, "Abc"); break;
		case Case::Upper: lv_label_set_text(caseLabel, "ABC"); break;
	}
}

void PhoneT9Input::refreshPlaceholder(){
	if(placeholderLabel == nullptr) return;
	const bool empty = committed.length() == 0 && !pendingActive;
	if(empty && placeholder.length() > 0){
		lv_label_set_text(placeholderLabel, placeholder.c_str());
		lv_obj_clear_flag(placeholderLabel, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_add_flag(placeholderLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

char PhoneT9Input::applyCase(char base) const {
	// Non-letters (digits, punctuation, space) ignore case mode entirely.
	if(!isalpha((unsigned char) base)) return base;

	switch(caseMode){
		case Case::Lower:
			return (char) tolower((unsigned char) base);
		case Case::Upper:
			return (char) toupper((unsigned char) base);
		case Case::First:
			// "First" -> uppercase only the first letter of a fresh
			// pending sequence. We approximate this by checking the
			// committed buffer: if the prior committed char is empty,
			// a sentence terminator, or the user just cleared the
			// buffer, render upper; otherwise lower.
			if(nextStartsUpper && pendingActive && pendingCharIndex == 0){
				return (char) toupper((unsigned char) base);
			}
			if(committed.length() == 0){
				return (char) toupper((unsigned char) base);
			}
			return (char) tolower((unsigned char) base);
	}
	return base;
}
