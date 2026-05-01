#include "PhoneGalleryScreen.h"

#include <stdio.h>
#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneMainMenu.cpp / PhoneHomeScreen.cpp / PhoneCameraScreen.cpp).
// Cyan owns the "active" elements (title, brackets, "VIEW" caption), sunset
// orange owns the cursor highlight + filled-slot horizon, dim purple sits
// behind the empty thumbnail backdrops and idle hint, warm cream carries
// the per-thumbnail "EMPTY" caption and the count label.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple sub-caption

// ===========================================================================
// Construction / destruction
// ===========================================================================

PhoneGalleryScreen::PhoneGalleryScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  titleLabel(nullptr),
		  countLabel(nullptr),
		  hintLabel(nullptr),
		  hintResetTimer(nullptr),
		  cursorBorder(nullptr),
		  cursor(0) {

	// All slots default to empty (fillSeed == 0). Containers are nulled
	// here and populated in buildSlots(); slotFill stays 0 so the
	// initial paint shows four "EMPTY" thumbnails - matching the S46
	// "placeholder" intent.
	for(uint8_t i = 0; i < SlotCount; ++i){
		slotContainer[i] = nullptr;
		slotFill[i]      = 0;
	}

	// Full-screen container, no scrollbars, no inner padding - same blank
	// canvas pattern PhoneHomeScreen / PhoneCameraScreen / PhoneMusicPlayer
	// use. Children below either pin themselves with IGNORE_LAYOUT or are
	// LVGL primitives that we anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The
	// thumbnails and bars overlay it. Same z-order discipline every other
	// phone-style screen uses, so a future page-transition helper (S66)
	// gets a consistent base layer to animate.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall) so the user
	// can always see the device is alive even with the gallery open.
	statusBar = new PhoneStatusBar(obj);

	// Title strip (cyan "GALLERY" + cream "X / 4" count) sits directly
	// under the status bar.
	buildTitleStrip();

	// 2x2 thumbnail grid + the moving cursor highlight border.
	buildSlots();

	// Bottom hint line ("no captures yet" by default) just above the
	// soft-key bar.
	buildHint();

	// Bottom: feature-phone soft-keys. Left = VIEW (BTN_ENTER), right
	// = BACK (BTN_BACK). Same single-action layout PhoneAppStubScreen
	// uses, but with both keys wired up so the user always has somewhere
	// to go.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("VIEW");
	softKeys->setRight("BACK");

	// Initial focus on slot 0; refreshCursorBorder pins the highlight.
	setCursor(0);
	refreshCountLabel();
}

PhoneGalleryScreen::~PhoneGalleryScreen() {
	// Defensive: kill any in-flight hint-reset timer so the screen never
	// frees itself while LVGL still holds a timer ref pointing at it.
	// onStop() also runs this teardown; covering both paths keeps us
	// safe if the screen is destroyed without ever being started (host
	// built it, changed its mind before push()).
	if(hintResetTimer != nullptr){
		lv_timer_del(hintResetTimer);
		hintResetTimer = nullptr;
	}

	// Children (wallpaper, statusBar, softKeys, slot containers, labels,
	// cursor border) are all parented to obj - LVGL frees them
	// recursively when the screen's obj is destroyed by the LVScreen
	// base destructor. Nothing manual.
}

// ===========================================================================
// LVScreen lifecycle
// ===========================================================================

void PhoneGalleryScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneGalleryScreen::onStop() {
	Input::getInstance()->removeListener(this);

	// Kill any transient-hint timer so we never wake up to call back
	// into a screen that has been popped. Idempotent - lv_timer_del on
	// a null pointer would be a problem, so we guard.
	if(hintResetTimer != nullptr){
		lv_timer_del(hintResetTimer);
		hintResetTimer = nullptr;
	}
}

// ===========================================================================
// Builders
// ===========================================================================

lv_obj_t* PhoneGalleryScreen::makeRect(lv_obj_t* parent,
									   lv_coord_t x, lv_coord_t y,
									   lv_coord_t w, lv_coord_t h,
									   lv_color_t c) {
	// Tiny helper: a flat-coloured rectangle parented to parent, with
	// IGNORE_LAYOUT so we can pin it absolutely. Used heavily by the
	// per-slot painters below where we need a few rects to draw the
	// bracket frame + (when filled) the synthwave silhouette. Mirrors
	// PhoneCameraScreen::makeRect but parametrises the parent so we
	// can put primitives inside a slot container.
	lv_obj_t* r = lv_obj_create(parent);
	lv_obj_remove_style_all(r);
	lv_obj_add_flag(r, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(r, w, h);
	lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(r, c, 0);
	lv_obj_set_style_border_width(r, 0, 0);
	lv_obj_set_style_radius(r, 0, 0);
	lv_obj_set_style_pad_all(r, 0, 0);
	lv_obj_set_pos(r, x, y);
	lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
	return r;
}

void PhoneGalleryScreen::buildTitleStrip() {
	// "GALLERY" caption - cyan, pixelbasic7, anchored hard left at the
	// same X the soft-key bar uses internally so the visual rhythm is
	// consistent with every other phone-style screen.
	titleLabel = lv_label_create(obj);
	lv_obj_add_flag(titleLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(titleLabel, "GALLERY");
	lv_obj_set_pos(titleLabel, 4, 12);

	// "X / 4" count caption - warm cream, pixelbasic7, right-anchored
	// so it pairs visually with the camera screen's "X/24" frame
	// counter and reads as "this many of these are filled".
	countLabel = lv_label_create(obj);
	lv_obj_add_flag(countLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(countLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(countLabel, MP_TEXT, 0);
	lv_label_set_long_mode(countLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(countLabel, 40);
	lv_obj_set_style_text_align(countLabel, LV_TEXT_ALIGN_RIGHT, 0);
	// Right edge ~4 px shy of the screen so the digits do not kiss the
	// frame; with a 40 px wide label that puts x = 160 - 40 - 4 = 116.
	lv_obj_set_pos(countLabel, ScreenW - 40 - 4, 12);
	refreshCountLabel();
}

void PhoneGalleryScreen::buildSlots() {
	// Build four slot containers + a single moving cursor border.
	for(uint8_t i = 0; i < SlotCount; ++i){
		const uint8_t col = i % GridCols;
		const uint8_t row = i / GridCols;
		const lv_coord_t x = (lv_coord_t)(GridMarginX + col * (ThumbW + ThumbGap));
		const lv_coord_t y = (lv_coord_t)(GridY        + row * (ThumbH + ThumbGap));

		// Container is itself a flat MP_DIM-coloured rect (the empty
		// thumbnail backdrop). Children drawn inside it (brackets,
		// caption, optional fill silhouette) overlay this base.
		lv_obj_t* c = lv_obj_create(obj);
		lv_obj_remove_style_all(c);
		lv_obj_add_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(c, ThumbW, ThumbH);
		lv_obj_set_pos(c, x, y);
		lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(c, MP_DIM, 0);
		lv_obj_set_style_border_width(c, 0, 0);
		lv_obj_set_style_radius(c, 0, 0);
		lv_obj_set_style_pad_all(c, 0, 0);
		lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
		// Disable click bubbling - we handle input via the screen's
		// hardware listener, not LVGL's pointer pipeline.
		lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);

		slotContainer[i] = c;

		// Initial paint reflects the default-empty state.
		buildSlotContents(i);
	}

	// Single moving cursor border - 2 px sunset-orange frame that sits
	// just outside the focused thumbnail container. We position it in
	// refreshCursorBorder() and update its (x, y) on every move; size
	// is fixed so we never have to retouch geometry.
	cursorBorder = lv_obj_create(obj);
	lv_obj_remove_style_all(cursorBorder);
	lv_obj_add_flag(cursorBorder, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_clear_flag(cursorBorder, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(cursorBorder, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(cursorBorder, ThumbW + 4, ThumbH + 4);
	lv_obj_set_style_bg_opa(cursorBorder, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(cursorBorder, MP_ACCENT, 0);
	lv_obj_set_style_border_width(cursorBorder, 2, 0);
	lv_obj_set_style_border_opa(cursorBorder, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(cursorBorder, 0, 0);
	lv_obj_set_style_pad_all(cursorBorder, 0, 0);
	// Sit on top of the slot containers so the border is never hidden.
	lv_obj_move_foreground(cursorBorder);
}

void PhoneGalleryScreen::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_CLIP);
	// Span the full screen width so we can centre the message regardless
	// of length without having to re-measure on every text change.
	lv_obj_set_width(hintLabel, ScreenW);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "no captures yet");
	// Sits in the 12 px strip between the bottom thumbnail row (ends at
	// y = GridY + 2*ThumbH + ThumbGap = 24 + 80 + 2 = 106) and the
	// soft-key bar (starts at y = 118). Anchoring at y = 107 leaves a
	// crisp 1 px gap above the soft-key bar.
	lv_obj_set_pos(hintLabel, 0, 107);
}

void PhoneGalleryScreen::clearSlotContents(uint8_t slotIndex) {
	if(slotIndex >= SlotCount) return;
	lv_obj_t* c = slotContainer[slotIndex];
	if(c == nullptr) return;
	// LVGL has lv_obj_clean() to drop all children of a parent in one
	// call. We use it instead of walking + deleting because the slot
	// container only ever holds primitives we own (no listeners attached
	// to children, no style refs that need teardown). After the clean
	// the container is back to the bare MP_DIM-coloured backdrop.
	lv_obj_clean(c);
}

void PhoneGalleryScreen::buildSlotContents(uint8_t slotIndex) {
	if(slotIndex >= SlotCount) return;
	lv_obj_t* c = slotContainer[slotIndex];
	if(c == nullptr) return;

	// Always start from a clean container so a setSlotFilled() that
	// flips the state from filled -> empty (or empty -> filled with a
	// new seed) does not leave stale primitives behind.
	clearSlotContents(slotIndex);

	const uint8_t fill = slotFill[slotIndex];

	// Restore the backdrop colour for the (potentially) repainted slot.
	// Empty slots use MP_DIM (muted purple, recedes on the synthwave
	// wallpaper); filled slots use MP_BG_DARK (deep purple sky) so the
	// horizon stripe + stars feel like they sit on top of a real night
	// scene rather than a swatch of the empty background.
	lv_obj_set_style_bg_color(c, fill == 0 ? MP_DIM : MP_BG_DARK, 0);

	// ----- corner brackets -----
	// Same 4-corner L-shape PhoneCameraScreen uses for the viewfinder,
	// scaled down for the 72x40 thumbnail. Cyan to read as "frame" on
	// either backdrop; only 4 px arms so the brackets never crowd a
	// caption sitting near the centre.
	const lv_coord_t innerW = ThumbW;
	const lv_coord_t innerH = ThumbH;
	const lv_color_t bracketC = MP_HIGHLIGHT;
	const uint8_t arm = 4;
	makeRect(c, 0,                  0,                  arm, 1, bracketC);  // TL horiz
	makeRect(c, 0,                  0,                  1, arm, bracketC);  // TL vert
	makeRect(c, innerW - arm,       0,                  arm, 1, bracketC);  // TR horiz
	makeRect(c, innerW - 1,         0,                  1, arm, bracketC);  // TR vert
	makeRect(c, 0,                  innerH - 1,         arm, 1, bracketC);  // BL horiz
	makeRect(c, 0,                  innerH - arm,       1, arm, bracketC);  // BL vert
	makeRect(c, innerW - arm,       innerH - 1,         arm, 1, bracketC);  // BR horiz
	makeRect(c, innerW - 1,         innerH - arm,       1, arm, bracketC);  // BR vert

	if(fill == 0){
		// ----- empty caption -----
		// Centred "EMPTY" label in warm cream so the slot reads as
		// "no capture here yet". pixelbasic7 keeps the type aligned
		// with the rest of the phone-style chrome.
		lv_obj_t* lbl = lv_label_create(c);
		lv_obj_add_flag(lbl, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
		lv_label_set_text(lbl, "EMPTY");
		lv_obj_set_align(lbl, LV_ALIGN_CENTER);
	} else {
		// ----- synthetic capture silhouette -----
		// Tiny code-only synthwave scene seeded by `fill` so two
		// different fillSeed values produce two visibly different
		// thumbnails. The horizon hue cycles through a tiny palette
		// (sunset orange / cyan / dim purple) and the star positions
		// are picked from low-bit slices of the seed - deterministic,
		// allocation-free, and recognisable across redraws.
		const uint8_t hueIdx = (fill - 1) % 3;
		lv_color_t horizonC;
		switch(hueIdx){
			case 0:  horizonC = MP_ACCENT;     break;   // sunset orange
			case 1:  horizonC = MP_HIGHLIGHT;  break;   // cyan
			default: horizonC = MP_LABEL_DIM;  break;   // dim purple
		}

		// Horizon stripe across the lower third of the thumbnail.
		const lv_coord_t horizonY = (lv_coord_t)(innerH * 2 / 3);
		makeRect(c, 1, horizonY,     innerW - 2, 2, horizonC);
		// Sub-horizon shadow line (one pixel of MP_DIM beneath the
		// horizon stripe) to imply ground depth.
		makeRect(c, 1, horizonY + 2, innerW - 2, 1, MP_DIM);

		// Two pixel "stars" in the sky band, positions seeded from
		// the fillSeed bits so each fill index has a unique-looking
		// sky without us having to ship a per-thumbnail texture.
		const uint8_t  sx0 = 6 + ((fill * 7)  % (innerW - 16));
		const uint8_t  sy0 = 4 + ((fill * 3)  % (horizonY - 8));
		const uint8_t  sx1 = 6 + ((fill * 11) % (innerW - 16));
		const uint8_t  sy1 = 4 + ((fill * 5)  % (horizonY - 8));
		makeRect(c, (lv_coord_t)sx0, (lv_coord_t)sy0, 1, 1, MP_TEXT);
		makeRect(c, (lv_coord_t)sx1, (lv_coord_t)sy1, 1, 1, MP_TEXT);

		// Slot-index caption ("PHOTO 1".."PHOTO 4") in cream so the
		// user can identify the focused capture at a glance. We anchor
		// it 4 px down from the top to clear the bracket arms.
		lv_obj_t* lbl = lv_label_create(c);
		lv_obj_add_flag(lbl, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
		char buf[12];
		snprintf(buf, sizeof(buf), "PHOTO %u", (unsigned)(slotIndex + 1));
		lv_label_set_text(lbl, buf);
		lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 4);
	}
}

// ===========================================================================
// Public API
// ===========================================================================

void PhoneGalleryScreen::setSlotFilled(uint8_t slotIndex, uint8_t fillSeed) {
	if(slotIndex >= SlotCount) return;
	if(slotFill[slotIndex] == fillSeed) return;  // no-op for repeats
	slotFill[slotIndex] = fillSeed;
	buildSlotContents(slotIndex);
	refreshCountLabel();
}

uint8_t PhoneGalleryScreen::getSlotFilled(uint8_t slotIndex) const {
	if(slotIndex >= SlotCount) return 0;
	return slotFill[slotIndex];
}

uint8_t PhoneGalleryScreen::getFilledCount() const {
	uint8_t n = 0;
	for(uint8_t i = 0; i < SlotCount; ++i){
		if(slotFill[i] != 0) n++;
	}
	return n;
}

// ===========================================================================
// Helpers
// ===========================================================================

void PhoneGalleryScreen::moveCursor(int8_t dx, int8_t dy) {
	const uint8_t curCol = cursor % GridCols;
	const uint8_t curRow = cursor / GridCols;

	// Clamp dx/dy to {-1, 0, +1} so a future remap that fires multiple
	// presses per tick still produces sensible single-step navigation.
	int8_t sdx = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
	int8_t sdy = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

	int16_t newCol = (int16_t)curCol + sdx;
	int16_t newRow = (int16_t)curRow + sdy;

	// Wrap on both axes - feature-phone users expect "right past the
	// edge" to land on the next item, not get stuck. We do the wrap
	// on a per-axis basis (rather than a single flat-index mod) because
	// the 2x2 grid is small enough that a vertical-wrap from the bottom
	// row should land on the top of the SAME column, not the first slot.
	if(newCol < 0) newCol = GridCols - 1;
	if(newCol >= (int16_t)GridCols) newCol = 0;
	if(newRow < 0) newRow = GridRows - 1;
	if(newRow >= (int16_t)GridRows) newRow = 0;

	const uint8_t newIdx = (uint8_t)(newRow * GridCols + newCol);
	if(newIdx == cursor) return;
	setCursor(newIdx);
}

void PhoneGalleryScreen::setCursor(uint8_t newCursor) {
	if(newCursor >= SlotCount) return;
	cursor = newCursor;
	refreshCursorBorder();
}

void PhoneGalleryScreen::refreshCursorBorder() {
	if(cursorBorder == nullptr) return;
	if(cursor >= SlotCount) return;
	if(slotContainer[cursor] == nullptr) return;
	// Position the border 2 px outside the focused container's top-left
	// corner; the border itself is (ThumbW+4 x ThumbH+4) so it fits
	// snugly around the thumbnail without overlapping its content.
	const uint8_t col = cursor % GridCols;
	const uint8_t row = cursor / GridCols;
	const lv_coord_t x = (lv_coord_t)(GridMarginX + col * (ThumbW + ThumbGap)) - 2;
	const lv_coord_t y = (lv_coord_t)(GridY        + row * (ThumbH + ThumbGap)) - 2;
	lv_obj_set_pos(cursorBorder, x, y);
	// Re-foreground the border in case a setSlotFilled() rebuild stacked
	// new primitives over it. Cheap (just a list reorder) and keeps the
	// highlight visible after any in-place repaint.
	lv_obj_move_foreground(cursorBorder);
}

void PhoneGalleryScreen::refreshCountLabel() {
	if(countLabel == nullptr) return;
	char buf[16];
	snprintf(buf, sizeof(buf), "%u / %u",
			 (unsigned)getFilledCount(), (unsigned)SlotCount);
	lv_label_set_text(countLabel, buf);
}

void PhoneGalleryScreen::flashHint(const char* msg) {
	if(hintLabel == nullptr || msg == nullptr) return;
	lv_label_set_text(hintLabel, msg);

	// Tear down any in-flight reset timer so spamming VIEW does not
	// stack timers - the latest press always owns the (re-)reset.
	if(hintResetTimer != nullptr){
		lv_timer_del(hintResetTimer);
		hintResetTimer = nullptr;
	}
	hintResetTimer = lv_timer_create(onHintResetTick, HintFlashMs, this);
	lv_timer_set_repeat_count(hintResetTimer, 1);
}

void PhoneGalleryScreen::onHintResetTick(lv_timer_t* t) {
	if(t == nullptr) return;
	auto* self = static_cast<PhoneGalleryScreen*>(t->user_data);
	if(self == nullptr){
		lv_timer_del(t);
		return;
	}
	if(self->hintLabel != nullptr){
		lv_label_set_text(self->hintLabel, "no captures yet");
	}
	self->hintResetTimer = nullptr;
	lv_timer_del(t);
}

// ===========================================================================
// Input
// ===========================================================================

void PhoneGalleryScreen::buttonPressed(uint i) {
	switch(i){
		// ----- vertical (UP / DOWN) -----
		// BTN_LEFT == BTN_UP, BTN_RIGHT == BTN_DOWN per the alias macros
		// in libraries/Chatter-Library/src/Pins.hpp. We honour both
		// flavours plus the numeric-pad ergonomics (BTN_2 / BTN_8) so
		// the gallery feels at home next to the dialer-pad.
		case BTN_LEFT:
		case BTN_2:
			moveCursor(0, -1);
			break;
		case BTN_RIGHT:
		case BTN_8:
			moveCursor(0, +1);
			break;

		// ----- horizontal (LEFT / RIGHT) -----
		// The 2x2 grid needs a horizontal axis too. The d-pad doesn't
		// have one (BTN_LEFT/BTN_RIGHT are the vertical alias above),
		// so we lean on the numeric-pad columns: BTN_4 / BTN_6 - the
		// same convention every Sony-Ericsson user already has in their
		// fingers from numeric-keypad menus.
		case BTN_4:
			moveCursor(-1, 0);
			break;
		case BTN_6:
			moveCursor(+1, 0);
			break;

		// ----- primary action -----
		case BTN_ENTER: {
			// "View" the focused slot. With no real capture pipeline
			// (S46 is a placeholder), all we can do is acknowledge the
			// press via a soft-key flash + a transient hint message so
			// the user gets explicit feedback that the keypress was
			// received. Filled slots also get a "SLOT N" tag so a host
			// that pre-populates fills can still tell which one fired.
			if(softKeys != nullptr) softKeys->flashLeft();
			char buf[16];
			if(slotFill[cursor] == 0){
				flashHint("EMPTY SLOT");
			}else{
				snprintf(buf, sizeof(buf), "PHOTO %u", (unsigned)(cursor + 1));
				flashHint(buf);
			}
			break;
		}

		// ----- exit -----
		case BTN_BACK:
			if(softKeys != nullptr) softKeys->flashRight();
			pop();
			break;

		default:
			// Any other key is intentionally ignored - the dialer-pad
			// digit keys (apart from 2/4/6/8 which we use for nav) and
			// the shoulder bumpers have no action inside the gallery
			// stub.
			break;
	}
}
