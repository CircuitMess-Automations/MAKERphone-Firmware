#include "PhoneSimPinScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Fonts/font.h"

// ---- MAKERphone retro palette ----------------------------------------------
// Inlined per the established convention in this codebase (see
// PhoneBootSplash / PhoneWelcomeScreen / PhoneSynthwaveBg). Keeping the
// palette local to each screen makes the Phase-A widgets relocatable
// without dragging a shared header along - the trade-off is each new
// screen restating the same handful of constants.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple background
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple (box fill)
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange (highlight)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan (active digit / OK)
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple hint
#define MP_SIM_GOLD    lv_color_make(220, 170,  60)   // SIM contact gold

// ---- layout constants ------------------------------------------------------
//
// 160x128 budget. The "ENTER PIN" line is the focal element; pin boxes
// cluster on a single row centred under it; the SIM icon hugs the top-
// left corner so the screen reads as "your SIM card needs unlocking".
// All Y-coordinates are absolute on the full-bleed canvas - no status
// bar / soft-key chrome at this point in the boot flow.
//
// Pin boxes:  4 boxes x 18 wide, 6 px gaps -> 4*18 + 3*6 = 90 px
//             centred at x = (160 - 90) / 2 = 35
static constexpr lv_coord_t kBoxW          = 18;
static constexpr lv_coord_t kBoxH          = 22;
static constexpr lv_coord_t kBoxGap        = 6;
static constexpr lv_coord_t kBoxesY        = 64;
static constexpr lv_coord_t kBoxesX        = (160 - (4 * kBoxW + 3 * kBoxGap)) / 2;

static constexpr lv_coord_t kSimIconX      = 6;
static constexpr lv_coord_t kSimIconY      = 6;
static constexpr lv_coord_t kSimIconW      = 14;
static constexpr lv_coord_t kSimIconH      = 18;

static constexpr lv_coord_t kTitleY        = 18;   // "ENTER PIN"
static constexpr lv_coord_t kCaptionY      = 38;   // "SIM 1"
static constexpr lv_coord_t kCheckY        = 96;   // "PIN OK" overlay line


PhoneSimPinScreen::PhoneSimPinScreen(DismissHandler onDismiss)
		: LVScreen(),
		  dismissCb(onDismiss),
		  dismissedAlready(false),
		  digitsEntered(0),
		  checking(false),
		  simIcon(nullptr),
		  simChip(nullptr),
		  title(nullptr),
		  caption(nullptr),
		  hint(nullptr),
		  checkLabel(nullptr),
		  checkTimer(nullptr) {

	for(uint8_t i = 0; i < PinLength; ++i){
		pinBoxes[i]  = nullptr;
		pinGlyphs[i] = nullptr;
	}

	// Full-screen container, no scrollbars, zero padding - same blank
	// canvas pattern every other Phone* boot-flow screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);

	// Build z-order back-to-front: background, sim icon, labels,
	// pin boxes, hint, check overlay (hidden until the buffer fills).
	buildBackground();
	buildSimIcon();
	buildTitle();
	buildCaption();
	buildPinBoxes();
	buildHint();
	buildCheckLabel();

	// Initial paint: empty buffer, idle hint.
	renderPinBoxes();
	updateHint();
}

PhoneSimPinScreen::~PhoneSimPinScreen() {
	stopCheckHold();
	// Every child is parented to obj - LVScreen's destructor frees obj
	// and LVGL recursively tears down their backing storage.
}

// ---- lifecycle ------------------------------------------------------------

void PhoneSimPinScreen::onStart() {
	Input::getInstance()->addListener(this);
	dismissedAlready = false;
}

void PhoneSimPinScreen::onStop() {
	stopCheckHold();
	Input::getInstance()->removeListener(this);
}

// ---- builders -------------------------------------------------------------

void PhoneSimPinScreen::buildBackground() {
	// Hard-fill the screen with deep purple. Keeps a single pre-LVGL
	// flush from showing the default theme tint, and reads as the
	// MAKERphone "phone is asleep" canvas behind every overlay.
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

void PhoneSimPinScreen::buildSimIcon() {
	// Small pixel-art SIM card in the top-left corner: a 14x18 rounded
	// rectangle with a clipped corner (top-right) and a gold "contact
	// chip" rectangle inset on the lower half. Drawn entirely from
	// LVGL primitives - no SPIFFS asset cost.
	simIcon = lv_obj_create(obj);
	lv_obj_remove_style_all(simIcon);
	lv_obj_clear_flag(simIcon, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(simIcon, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(simIcon, kSimIconW, kSimIconH);
	lv_obj_set_pos(simIcon, kSimIconX, kSimIconY);
	lv_obj_set_style_radius(simIcon, 2, 0);
	lv_obj_set_style_bg_color(simIcon, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(simIcon, LV_OPA_COVER, 0);
	// 1 px outline reading as the card edge, in dim purple.
	lv_obj_set_style_border_color(simIcon, MP_LABEL_DIM, 0);
	lv_obj_set_style_border_width(simIcon, 1, 0);
	lv_obj_set_style_border_opa(simIcon, LV_OPA_COVER, 0);

	// Gold contact chip rectangle on the upper half of the card,
	// inset by 2 px on each side. The classic SIM-card silhouette.
	simChip = lv_obj_create(simIcon);
	lv_obj_remove_style_all(simChip);
	lv_obj_clear_flag(simChip, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(simChip, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(simChip, kSimIconW - 4, 8);
	lv_obj_set_pos(simChip, 2, 4);
	lv_obj_set_style_radius(simChip, 1, 0);
	lv_obj_set_style_bg_color(simChip, MP_SIM_GOLD, 0);
	lv_obj_set_style_bg_opa(simChip, LV_OPA_COVER, 0);
	// Faint lateral lines on the chip would be nice but eat object
	// budget; the solid gold rect reads as a chip already.
}

void PhoneSimPinScreen::buildTitle() {
	title = lv_label_create(obj);
	lv_obj_set_style_text_font(title, &pixelbasic16, 0);
	lv_obj_set_style_text_color(title, MP_TEXT, 0);
	lv_label_set_text(title, "ENTER PIN");
	lv_obj_set_align(title, LV_ALIGN_TOP_MID);
	lv_obj_set_y(title, kTitleY);
}

void PhoneSimPinScreen::buildCaption() {
	// "SIM 1" sub-line, dim purple - reads as the slot indicator on
	// the real Sony-Ericsson PIN UX without competing with the title.
	caption = lv_label_create(obj);
	lv_obj_set_style_text_font(caption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(caption, MP_LABEL_DIM, 0);
	lv_label_set_text(caption, "SIM 1");
	lv_obj_set_align(caption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(caption, kCaptionY);
}

void PhoneSimPinScreen::buildPinBoxes() {
	// Four mask boxes laid out horizontally. Each box is a small
	// rounded rectangle filled with MP_DIM (muted purple) with a
	// 1 px sunset-orange border so it pops against the background.
	// A child label centred inside each box renders the masked
	// glyph ("*") once that slot is filled. Keeping the label as a
	// child (rather than re-drawing the box's text style) means we
	// can drive its visibility independently per-box.
	for(uint8_t i = 0; i < PinLength; ++i){
		lv_obj_t* box = lv_obj_create(obj);
		lv_obj_remove_style_all(box);
		lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(box, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(box, kBoxW, kBoxH);
		lv_obj_set_pos(box, kBoxesX + i * (kBoxW + kBoxGap), kBoxesY);
		lv_obj_set_style_radius(box, 2, 0);
		lv_obj_set_style_bg_color(box, MP_DIM, 0);
		lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(box, MP_ACCENT, 0);
		lv_obj_set_style_border_width(box, 1, 0);
		lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
		pinBoxes[i] = box;

		// Mask glyph child label - "*" in cream when filled, hidden
		// otherwise (renderPinBoxes flips the LV_OBJ_FLAG_HIDDEN bit).
		lv_obj_t* glyph = lv_label_create(box);
		lv_obj_set_style_text_font(glyph, &pixelbasic16, 0);
		lv_obj_set_style_text_color(glyph, MP_TEXT, 0);
		lv_label_set_text(glyph, "*");
		// pixelbasic16 ships only upper-case + a handful of glyphs
		// including '*' (ASCII 42 lives in the 37..93 range of the
		// font's cmap, see src/Fonts/pixelbasic16.c). Centred inside
		// the 18x22 box - LV_ALIGN_CENTER is good enough at this size,
		// the half-pixel of vertical-baseline drift is invisible at
		// 160x128.
		lv_obj_set_align(glyph, LV_ALIGN_CENTER);
		pinGlyphs[i] = glyph;
	}
}

void PhoneSimPinScreen::buildHint() {
	// Bottom-strip soft-key style hint. Updated dynamically by
	// updateHint() to reflect the buffer state.
	hint = lv_label_create(obj);
	lv_obj_set_style_text_font(hint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hint, MP_LABEL_DIM, 0);
	lv_label_set_text(hint, "");
	// Anchored to the bottom of the screen the same way PhoneBootSplash
	// pins its "press any key" hint - a 4 px lift keeps the glyph
	// baseline clear of the screen edge on real hardware.
	lv_obj_set_align(hint, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(hint, -4);
}

void PhoneSimPinScreen::buildCheckLabel() {
	// "PIN OK" tick line, hidden until the buffer fills. Cyan so it
	// reads as a positive, hot-key-style confirmation rather than a
	// neutral status line. A thin overlay above the hint row.
	checkLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(checkLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(checkLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(checkLabel, "PIN OK");
	lv_obj_set_align(checkLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(checkLabel, kCheckY);
	lv_obj_add_flag(checkLabel, LV_OBJ_FLAG_HIDDEN);
}

// ---- rendering ------------------------------------------------------------

void PhoneSimPinScreen::renderPinBoxes() {
	// For each box: show the "*" glyph if its slot is filled, hide it
	// otherwise. Tint the border based on whether the slot is filled,
	// the active "next" slot, or empty - the active slot picks up
	// MP_HIGHLIGHT cyan so the user always knows where the next digit
	// will land. Filled slots get the solid sunset orange border so
	// they read as committed.
	for(uint8_t i = 0; i < PinLength; ++i){
		lv_obj_t* box = pinBoxes[i];
		lv_obj_t* glyph = pinGlyphs[i];
		if(box == nullptr) continue;
		const bool filled = (i < digitsEntered);
		const bool active = (i == digitsEntered) && !checking;

		if(glyph != nullptr){
			if(filled) lv_obj_clear_flag(glyph, LV_OBJ_FLAG_HIDDEN);
			else       lv_obj_add_flag(glyph, LV_OBJ_FLAG_HIDDEN);
		}

		lv_color_t border = MP_ACCENT;
		if(filled)      border = MP_ACCENT;
		else if(active) border = MP_HIGHLIGHT;
		else            border = MP_LABEL_DIM;
		lv_obj_set_style_border_color(box, border, 0);

		// Filled slots get a slightly brighter fill so the row
		// silhouette progresses visually as the user types.
		lv_obj_set_style_bg_color(box, filled ? MP_DIM : MP_BG_DARK, 0);
	}
}

void PhoneSimPinScreen::updateHint() {
	if(checking){
		// Don't fight the "PIN OK" overlay during the check hold -
		// the hint row goes blank so the cyan tick is the only line
		// of secondary text on screen.
		lv_label_set_text(hint, "");
		return;
	}
	if(digitsEntered == 0){
		// Empty buffer: BACK is the global-skip escape hatch. Surface
		// it explicitly so a dev who has seen the screen a hundred
		// times can blast through.
		lv_label_set_text(hint, "(B) skip   type 4 digits");
	}else if(digitsEntered < PinLength){
		// Mid-entry: BACK clears the most recent digit, ENTER will
		// only fire on a full buffer.
		lv_label_set_text(hint, "(B) clear   type more");
	}else{
		// Full buffer: ENTER is the manual confirm path. The screen
		// will auto-fire after CheckHoldMs anyway, so this is mostly
		// for users who hit ENTER as a habit.
		lv_label_set_text(hint, "(A) ok   (B) clear");
	}
}

// ---- input dispatch -------------------------------------------------------

void PhoneSimPinScreen::buttonPressed(uint i) {
	// Discard input while the post-confirm "PIN OK" hold is on the
	// screen - the dismiss is already in flight and a stray key would
	// only race with the dispatch.
	if(checking || dismissedAlready) return;

	// BTN_0..BTN_9 feed the digit buffer. Pin numbers are pure 0..9
	// indices not encoded as ASCII (the only thing the buffer cares
	// about is "did a slot get filled"), so we do not store the
	// actual digit value - any 4 digits unlock the decorative gate.
	switch(i){
		case BTN_0: case BTN_1: case BTN_2: case BTN_3: case BTN_4:
		case BTN_5: case BTN_6: case BTN_7: case BTN_8: case BTN_9:
			onDigit(0); // value ignored - decorative
			return;

		case BTN_BACK:
			onBackspace();
			return;

		case BTN_ENTER:
			onConfirm();
			return;

		default:
			// L / R / LEFT / RIGHT bumpers are inert here. The user
			// is meant to feel "pinned" to the PIN entry surface, the
			// same way the real Sony-Ericsson SIM unlock did.
			return;
	}
}

void PhoneSimPinScreen::onDigit(uint8_t /*d*/) {
	if(digitsEntered >= PinLength) return;
	++digitsEntered;
	renderPinBoxes();
	updateHint();
	if(digitsEntered >= PinLength){
		// Auto-fire the check / dismiss path the moment the 4th digit
		// lands. The user does not need to press ENTER explicitly -
		// real SIM PIN UIs of the era did this same auto-confirm.
		startCheckHold();
	}
}

void PhoneSimPinScreen::onBackspace() {
	if(digitsEntered == 0){
		// Empty buffer: BACK is the global-skip escape hatch. Honour
		// it immediately so power users can boot through every time.
		fireDismiss();
		return;
	}
	--digitsEntered;
	renderPinBoxes();
	updateHint();
}

void PhoneSimPinScreen::onConfirm() {
	// Manual confirm path. Only meaningful with a full buffer; on a
	// partial buffer this no-ops (the user has to keep typing). Ignore
	// re-entry while the check hold is already running.
	if(checking) return;
	if(digitsEntered < PinLength) return;
	startCheckHold();
}

// ---- check hold + dismiss --------------------------------------------------

void PhoneSimPinScreen::startCheckHold() {
	if(checking) return;
	checking = true;
	// Re-render the boxes one last time so the "active" slot tint
	// drops away (no slot is "next" anymore - the buffer is full).
	renderPinBoxes();
	// Surface the "PIN OK" tick line on top of where the hint was.
	updateHint();
	if(checkLabel != nullptr){
		lv_obj_clear_flag(checkLabel, LV_OBJ_FLAG_HIDDEN);
	}
	checkTimer = lv_timer_create(onCheckTimer, CheckHoldMs, this);
}

void PhoneSimPinScreen::stopCheckHold() {
	if(checkTimer == nullptr) return;
	lv_timer_del(checkTimer);
	checkTimer = nullptr;
}

void PhoneSimPinScreen::onCheckTimer(lv_timer_t* timer) {
	auto self = static_cast<PhoneSimPinScreen*>(timer->user_data);
	if(self == nullptr) return;
	self->fireDismiss();
}

void PhoneSimPinScreen::fireDismiss() {
	if(dismissedAlready) return;
	dismissedAlready = true;

	// Stop the check timer up-front so a slow handler can not let it
	// re-enter fireDismiss via the static callback.
	stopCheckHold();

	// Tear ourselves down BEFORE invoking the host callback. Same
	// "first-screen-of-boot has no parent to pop back to" pattern
	// PhoneBootSplash uses - the host callback will push the next
	// screen (typically IntroScreen) directly.
	auto cb = dismissCb;
	stop();
	lv_obj_del(getLvObj());

	if(cb != nullptr) cb();
}
