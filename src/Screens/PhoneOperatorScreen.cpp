#include "PhoneOperatorScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneT9Input.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the operator-banner editor slots in beside PhoneOwnerNameScreen (S144),
// PhonePowerOffMessageScreen (S146), PhoneScratchpad (S140) and the rest of
// the SYSTEM screens without a visual seam. Inlined per the established
// pattern.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (active cells, focus)
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan (caption, mode hint)
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple (idle frame)
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// ---------- geometry ------------------------------------------------------
//
// 160 x 128 layout. Two coexisting editor regions, one focused at a time.
//   y =  0..  9   PhoneStatusBar (10 px)
//   y = 12        caption "OPERATOR" (pixelbasic7, ~7 px tall)
//   y = 21        top divider rule (1 px)
//   y = 24..  53  PhoneT9Input slab (Width 156, Height 22 + HelpHeight 8)
//   y = 56..  85  16x5 logo editor (96 x 30 px, centred at x=32)
//   y = 88        mode hint "EDITING TEXT" / "EDITING LOGO"
//   y = 95        bottom divider rule (1 px)
//   y = 118..127  PhoneSoftKeyBar (10 px)

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kTopDividerY   = 21;
static constexpr lv_coord_t kT9Y           = 24;
static constexpr lv_coord_t kLogoY         = 56;
static constexpr lv_coord_t kModeHintY     = 88;
static constexpr lv_coord_t kBotDividerY   = 95;
static constexpr lv_coord_t kRowLeftX      = 6;
static constexpr lv_coord_t kRowWidth      = 148;

// ---------- ctor / dtor --------------------------------------------------

PhoneOperatorScreen::PhoneOperatorScreen() : LVScreen() {
	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneOwnerNameScreen / PhonePowerOffMessageScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Synthwave wallpaper at the bottom of the z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Status bar: signal | clock | battery, anchored to the top.
	statusBar = new PhoneStatusBar(obj);

	// Caption + top/bottom dividers stay at fixed offsets independent
	// of mode -- they frame both regions.
	buildCaptionAndDividers();

	// TEXT region first (T9 input + focus border) so the LOGO grid
	// can read the same focus-color palette without race-y init order.
	buildTextRegion();

	// LOGO region (host + 80 cells + cursor + focus border).
	buildLogoRegion();

	// Mode hint sits between the LOGO region and the soft-keys.
	buildModeHint();

	// Soft-key bar at the bottom. CLEAR wipes the focused region,
	// DONE persists + pops. Mode toggle is wired to BTN_R long-press
	// and BTN_ENTER long-press, both flagged in the centre-hint copy.
	softKeys = new PhoneSoftKeyBar(obj);

	// Pre-load the persisted state so the screen opens with the
	// existing values rather than blank. The dirty flag stays false
	// until the first keystroke mutates either buffer.
	{
		const char* persistedText = Settings.get().operatorText;
		if(persistedText != nullptr) {
			size_t n = 0;
			while(n < MaxTextLen && persistedText[n] != '\0') ++n;
			memcpy(textBuf, persistedText, n);
			textBuf[n] = '\0';
			if(t9Input) t9Input->setText(String(textBuf));
		}

		const uint16_t* persistedLogo = Settings.get().operatorLogo;
		if(persistedLogo != nullptr) {
			for(uint8_t r = 0; r < LogoRows; ++r) {
				logoBuf[r] = persistedLogo[r];
			}
		}
	}

	refreshAllLogoCells();
	refreshCursor();
	refreshFocus();
	refreshModeHint();
	refreshSoftKeys();

	// 600 ms feature-phone hold threshold for the mode-toggle gestures
	// (BTN_R, BTN_ENTER) and the auto-save BTN_BACK long-press. Set on
	// the listener itself so the hold timer follows our lifetime.
	setButtonHoldTime(BTN_BACK,  BackHoldMs);
	setButtonHoldTime(BTN_R,     ModeHoldMs);
	setButtonHoldTime(BTN_ENTER, ModeHoldMs);
}

PhoneOperatorScreen::~PhoneOperatorScreen() = default;

void PhoneOperatorScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneOperatorScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- accessors ----------------------------------------------------

uint16_t PhoneOperatorScreen::getTextLength() const {
	if(t9Input == nullptr) return 0;
	return (uint16_t) t9Input->getText().length();
}

const char* PhoneOperatorScreen::getText() const {
	if(t9Input == nullptr) return "";
	static char cache[MaxTextLen + 1];
	const String s = t9Input->getText();
	const size_t n = s.length() < MaxTextLen ? s.length() : MaxTextLen;
	memcpy(cache, s.c_str(), n);
	cache[n] = '\0';
	return cache;
}

uint16_t PhoneOperatorScreen::getLogoRow(uint8_t r) const {
	if(r >= LogoRows) return 0;
	return logoBuf[r];
}

// ---------- builders -----------------------------------------------------

void PhoneOperatorScreen::buildCaptionAndDividers() {
	// Caption -- "OPERATOR". Painted in cyan to match the section
	// captions every other Phone* SYSTEM screen uses.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "OPERATOR");

	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);
}

void PhoneOperatorScreen::buildTextRegion() {
	// Focus frame around the T9 region. Recoloured by refreshFocus()
	// to indicate which region currently owns the keypad.
	textBorder = lv_obj_create(obj);
	lv_obj_remove_style_all(textBorder);
	lv_obj_clear_flag(textBorder, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(textBorder, PhoneT9Input::Width + 4, PhoneT9Input::Height + PhoneT9Input::HelpHeight + 4);
	lv_obj_set_pos(textBorder, (160 - (PhoneT9Input::Width + 4)) / 2, kT9Y - 2);
	lv_obj_set_style_bg_opa(textBorder, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(textBorder, 0, 0);
	lv_obj_set_style_border_width(textBorder, 1, 0);
	lv_obj_set_style_border_opa(textBorder, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(textBorder, MP_DIM, 0);

	// PhoneT9Input -- canonical S32 multi-tap entry. Caps at MaxTextLen.
	t9Input = new PhoneT9Input(obj, MaxTextLen);
	lv_obj_set_pos(t9Input->getLvObj(),
				   (160 - PhoneT9Input::Width) / 2,
				   kT9Y);
	t9Input->setPlaceholder("CARRIER");
	t9Input->setCase(PhoneT9Input::Case::First);

	auto self = this;
	t9Input->setOnTextChanged([self](const String& text) {
		(void) text;
		self->dirty = true;
	});
}

void PhoneOperatorScreen::buildLogoRegion() {
	// Focus frame around the LOGO region.
	const lv_coord_t borderW = EditorWidth + 4;
	const lv_coord_t borderH = EditorHeight + 4;
	logoBorder = lv_obj_create(obj);
	lv_obj_remove_style_all(logoBorder);
	lv_obj_clear_flag(logoBorder, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(logoBorder, borderW, borderH);
	lv_obj_set_pos(logoBorder, (160 - borderW) / 2, kLogoY - 2);
	lv_obj_set_style_bg_opa(logoBorder, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(logoBorder, 0, 0);
	lv_obj_set_style_border_width(logoBorder, 1, 0);
	lv_obj_set_style_border_opa(logoBorder, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(logoBorder, MP_DIM, 0);

	// Host container for the 80 logo cells + cursor.
	logoHost = lv_obj_create(obj);
	lv_obj_remove_style_all(logoHost);
	lv_obj_clear_flag(logoHost, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(logoHost, EditorWidth, EditorHeight);
	lv_obj_set_pos(logoHost, (160 - EditorWidth) / 2, kLogoY);
	lv_obj_set_style_bg_color(logoHost, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(logoHost, LV_OPA_30, 0); // hint of "canvas"

	// Build all 80 cells up front; toggleAt repaints them in place
	// rather than allocating/freeing during editing.
	for(uint8_t r = 0; r < LogoRows; ++r) {
		for(uint8_t c = 0; c < LogoCols; ++c) {
			lv_obj_t* cell = lv_obj_create(logoHost);
			lv_obj_remove_style_all(cell);
			lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_set_size(cell, EditorCellPx - 1, EditorCellPx - 1);
			lv_obj_set_pos(cell, c * EditorCellPx, r * EditorCellPx);
			lv_obj_set_style_radius(cell, 0, 0);
			lv_obj_set_style_bg_color(cell, MP_DIM, 0);
			lv_obj_set_style_bg_opa(cell, LV_OPA_50, 0);
			logoCells[r][c] = cell;
		}
	}

	// Cursor: 1 px outline rect floating over the focused cell. Painted
	// in MP_HIGHLIGHT so the user can always tell where the cursor is
	// even on a fully-filled grid.
	cursor = lv_obj_create(logoHost);
	lv_obj_remove_style_all(cursor);
	lv_obj_clear_flag(cursor, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(cursor, EditorCellPx, EditorCellPx);
	lv_obj_set_style_bg_opa(cursor, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(cursor, 1, 0);
	lv_obj_set_style_border_opa(cursor, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(cursor, MP_HIGHLIGHT, 0);
	lv_obj_set_style_radius(cursor, 0, 0);
}

void PhoneOperatorScreen::buildModeHint() {
	modeHint = lv_label_create(obj);
	lv_obj_set_style_text_font(modeHint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(modeHint, MP_HIGHLIGHT, 0);
	lv_label_set_long_mode(modeHint, LV_LABEL_LONG_DOT);
	lv_obj_set_width(modeHint, kRowWidth);
	lv_obj_set_style_text_align(modeHint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(modeHint, kRowLeftX, kModeHintY);
	lv_label_set_text(modeHint, "EDITING TEXT  -  HOLD R: LOGO");
}

// ---------- repainters ---------------------------------------------------

void PhoneOperatorScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	softKeys->setLeft("CLEAR");
	softKeys->setRight("DONE");
}

void PhoneOperatorScreen::refreshModeHint() {
	if(modeHint == nullptr) return;
	if(mode == Mode::Text) {
		lv_label_set_text(modeHint, "EDITING TEXT  -  HOLD R: LOGO");
	}else{
		lv_label_set_text(modeHint, "EDITING LOGO  -  HOLD R: TEXT");
	}
}

void PhoneOperatorScreen::refreshFocus() {
	if(textBorder) {
		lv_obj_set_style_border_color(textBorder,
			(mode == Mode::Text) ? MP_HIGHLIGHT : MP_DIM, 0);
	}
	if(logoBorder) {
		lv_obj_set_style_border_color(logoBorder,
			(mode == Mode::Logo) ? MP_HIGHLIGHT : MP_DIM, 0);
	}
	// Hide the cursor when LOGO mode is not active so the editor frame
	// stays calm while the user is typing.
	if(cursor) {
		if(mode == Mode::Logo) {
			lv_obj_clear_flag(cursor, LV_OBJ_FLAG_HIDDEN);
		}else{
			lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
		}
	}
}

void PhoneOperatorScreen::refreshLogoCell(uint8_t r, uint8_t c) {
	if(r >= LogoRows || c >= LogoCols) return;
	lv_obj_t* cell = logoCells[r][c];
	if(cell == nullptr) return;
	const uint16_t mask = (uint16_t) 0x8000u >> c;
	const bool on = (logoBuf[r] & mask) != 0;
	if(on) {
		lv_obj_set_style_bg_color(cell, MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
	}else{
		lv_obj_set_style_bg_color(cell, MP_DIM, 0);
		lv_obj_set_style_bg_opa(cell, LV_OPA_50, 0);
	}
}

void PhoneOperatorScreen::refreshAllLogoCells() {
	for(uint8_t r = 0; r < LogoRows; ++r) {
		for(uint8_t c = 0; c < LogoCols; ++c) {
			refreshLogoCell(r, c);
		}
	}
}

void PhoneOperatorScreen::refreshCursor() {
	if(cursor == nullptr) return;
	if(cx >= LogoCols) cx = LogoCols - 1;
	if(cy >= LogoRows) cy = LogoRows - 1;
	lv_obj_set_pos(cursor, cx * EditorCellPx, cy * EditorCellPx);
}

// ---------- persistence --------------------------------------------------

void PhoneOperatorScreen::persist() {
	if(t9Input != nullptr) {
		// Flush any pending T9 letter so a user who taps a digit and
		// immediately exits does not lose their last letter.
		t9Input->commitPending();
		const String live = t9Input->getText();
		const size_t cap = MaxTextLen;
		const size_t n = live.length() < cap ? live.length() : cap;
		memset(textBuf, 0, sizeof(textBuf));
		memcpy(textBuf, live.c_str(), n);
	}

	// Copy the staged buffers into Settings + persist.
	char* slotText = Settings.get().operatorText;
	memset(slotText, 0, sizeof(Settings.get().operatorText));
	const size_t copyN = strnlen(textBuf, MaxTextLen);
	memcpy(slotText, textBuf, copyN);

	uint16_t* slotLogo = Settings.get().operatorLogo;
	for(uint8_t r = 0; r < LogoRows; ++r) {
		slotLogo[r] = logoBuf[r];
	}

	Settings.store();
	dirty = false;
}

void PhoneOperatorScreen::clearFocused() {
	if(mode == Mode::Text) {
		if(t9Input) t9Input->clear();
		memset(textBuf, 0, sizeof(textBuf));
	}else{
		for(uint8_t r = 0; r < LogoRows; ++r) {
			logoBuf[r] = 0;
		}
		refreshAllLogoCells();
	}
	dirty = true;
}

void PhoneOperatorScreen::invertLogo() {
	if(mode != Mode::Logo) return;
	for(uint8_t r = 0; r < LogoRows; ++r) {
		// Mask to 16 cols so spurious top bits never sneak in.
		logoBuf[r] = (uint16_t) (~logoBuf[r] & 0xFFFFu);
	}
	refreshAllLogoCells();
	dirty = true;
}

void PhoneOperatorScreen::togglePixel() {
	if(mode != Mode::Logo) return;
	if(cx >= LogoCols || cy >= LogoRows) return;
	const uint16_t mask = (uint16_t) 0x8000u >> cx;
	logoBuf[cy] ^= mask;
	refreshLogoCell(cy, cx);
	dirty = true;
}

void PhoneOperatorScreen::switchMode() {
	mode = (mode == Mode::Text) ? Mode::Logo : Mode::Text;
	refreshFocus();
	refreshModeHint();
}

// ---------- input actions ------------------------------------------------

void PhoneOperatorScreen::onClearPressed() {
	if(softKeys) softKeys->flashLeft();
	clearFocused();
}

void PhoneOperatorScreen::onDonePressed() {
	if(softKeys) softKeys->flashRight();
	persist();
	pop();
}

// ---------- input --------------------------------------------------------

void PhoneOperatorScreen::buttonPressed(uint i) {
	// Reset hold-flags for any key that participates in long-press
	// shortcuts so a pending suppression does not leak across presses.
	if(i == BTN_BACK)  backLongFired  = false;
	if(i == BTN_ENTER) enterLongFired = false;

	if(mode == Mode::Text) {
		switch(i) {
			case BTN_0: if(t9Input) t9Input->keyPress('0'); return;
			case BTN_1: if(t9Input) t9Input->keyPress('1'); return;
			case BTN_2: if(t9Input) t9Input->keyPress('2'); return;
			case BTN_3: if(t9Input) t9Input->keyPress('3'); return;
			case BTN_4: if(t9Input) t9Input->keyPress('4'); return;
			case BTN_5: if(t9Input) t9Input->keyPress('5'); return;
			case BTN_6: if(t9Input) t9Input->keyPress('6'); return;
			case BTN_7: if(t9Input) t9Input->keyPress('7'); return;
			case BTN_8: if(t9Input) t9Input->keyPress('8'); return;
			case BTN_9: if(t9Input) t9Input->keyPress('9'); return;

			case BTN_L:
				// T9 backspace -- cancels a pending letter or erases
				// the last committed character.
				if(t9Input) t9Input->keyPress('*');
				return;

			case BTN_R:
				// T9 case toggle on a short press; long-press flips
				// to LOGO mode (handled by buttonHeld).
				if(t9Input) t9Input->keyPress('#');
				return;

			default:
				break;
		}
	}else{
		// LOGO mode -- numpad + ENTER drive the cursor / pixel toggle.
		switch(i) {
			case BTN_2:
				if(cy > 0) { cy--; refreshCursor(); }
				return;
			case BTN_8:
				if(cy + 1 < LogoRows) { cy++; refreshCursor(); }
				return;
			case BTN_4:
				if(cx > 0) { cx--; refreshCursor(); }
				return;
			case BTN_6:
				if(cx + 1 < LogoCols) { cx++; refreshCursor(); }
				return;

			case BTN_5:
				togglePixel();
				return;

			case BTN_L:
				invertLogo();
				return;

			default:
				break;
		}
	}

	// Shared softkey handlers + ENTER (mode-dependent).
	switch(i) {
		case BTN_ENTER:
			// Defer the short-press action to buttonReleased so a
			// long-press (mode flip) can pre-empt it without first
			// firing the commit / toggle. Without this guard a hold
			// to switch into LOGO mode would also accidentally
			// commit the pending T9 letter.
			return;

		case BTN_LEFT:
			onClearPressed();
			return;

		case BTN_RIGHT:
			// Defer to buttonReleased so a long-press on BACK can
			// pre-empt this on the same hold cycle.
			return;

		default:
			break;
	}
}

void PhoneOperatorScreen::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		backLongFired = true;
		if(softKeys) softKeys->flashRight();
		persist();
		pop();
		return;
	}
	if(i == BTN_R) {
		// Long-press on the bumper flips between TEXT and LOGO mode.
		// In TEXT mode the short-press already fired the T9 case
		// toggle on press; flipping mode on hold is purely additive.
		switchMode();
		return;
	}
	if(i == BTN_ENTER) {
		// Long-press on ENTER also flips mode -- alternate gesture
		// for the same intent so the user can discover either.
		enterLongFired = true;
		switchMode();
		return;
	}
}

void PhoneOperatorScreen::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		if(softKeys) softKeys->flashRight();
		persist();
		pop();
		return;
	}

	if(i == BTN_RIGHT) {
		onDonePressed();
		return;
	}

	if(i == BTN_ENTER) {
		// Skip the short-press action if a long-press already
		// flipped mode on this hold cycle. Otherwise: in TEXT
		// commit the pending T9 letter; in LOGO toggle the
		// focused pixel.
		if(enterLongFired) {
			enterLongFired = false;
			return;
		}
		if(mode == Mode::Text) {
			if(t9Input) t9Input->commitPending();
		}else{
			togglePixel();
		}
		return;
	}
}
