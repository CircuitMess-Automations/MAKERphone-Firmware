#include "PhoneDrumKitScreen.h"

#include <Audio/Piezo.h>
#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette — kept identical to every other Phone*
// widget so the drum kit slots in beside the rest of the Phase-S
// Easter-egg toys without a visual seam. Same inline-#define pattern
// PhoneDialerScreen / PhoneFortuneCookie / PhoneStressReliever use.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim caption

// =====================================================================
// Drum voice table
//
// Each voice is a 1-3 frame envelope played on the piezo. Frames are
// chosen empirically to read as distinct percussion on a single-tone
// buzzer:
//
//  - KICK : start low (~110 Hz), drop to a dull thud (~70 Hz).
//           Reads as the bass drum's "boom-thud" attack.
//  - SNARE: rapid descent across three bands so the single voice
//           sounds like a noise-burst even though each frame is
//           a pure tone. The descent is what your ear hears as
//           "the snare wires".
//  - HHT  : single very short high tone — a closed hi-hat tick.
//  - TM-L : two-frame mid-low descent, the classic floor tom.
//  - TM-M : two-frame mid descent, a rack tom.
//  - TM-H : two-frame mid-high descent, the smaller rack tom.
//  - CRS  : crash cymbal — a long high tone with a slight downbend
//           so it doesn't sound like a sine wave.
//  - COW  : cowbell — two short bright tones a fifth apart, the
//           classic "tonk-tonk" pattern.
//  - OHH  : open hi-hat — a high tone that lingers ~50 ms (longer
//           than the closed-HHT) so the user can tell them apart.
//  - CLAP : tone-silence-tone double-tap, the "ka-pow" handclap
//           cliche. The silence frame gives the ear a gap that
//           reads as the second hand striking after the first.
//
// Frame durations are kept short overall (each drum's total envelope
// fits in ~50-90 ms) so a fast tap pattern doesn't smear voices.
// =====================================================================

const PhoneDrumKitScreen::Drum PhoneDrumKitScreen::kDrums[NumDrums] = {
	// 0 — CLAP (placed on the digit-0 slot of the dialer-pad layout
	//          so it occupies the same bottom-row middle cell the
	//          dialer's "0" keycap occupies; tactile parity).
	{ "CLP", "CLAP",        3, { {1000,  8}, {   0, 12}, {1000, 10} } },
	// 1 — KICK
	{ "KIK", "KICK",        2, { { 110, 25}, {  70, 35}, {   0,  0} } },
	// 2 — SNARE
	{ "SNR", "SNARE",       3, { { 800, 10}, { 440, 12}, { 200, 18} } },
	// 3 — HI-HAT (closed)
	{ "HHT", "HI-HAT",      1, { {2400, 10}, {   0,  0}, {   0,  0} } },
	// 4 — TOM LOW
	{ "TML", "TOM LOW",     2, { { 180, 28}, { 130, 30}, {   0,  0} } },
	// 5 — TOM MID
	{ "TMM", "TOM MID",     2, { { 260, 26}, { 190, 28}, {   0,  0} } },
	// 6 — TOM HIGH
	{ "TMH", "TOM HIGH",    2, { { 340, 24}, { 260, 26}, {   0,  0} } },
	// 7 — CRASH cymbal
	{ "CRS", "CRASH",       3, { {2000, 18}, {1700, 18}, {1400, 22} } },
	// 8 — COWBELL
	{ "COW", "COWBELL",     2, { { 880, 22}, { 660, 18}, {   0,  0} } },
	// 9 — OPEN HI-HAT
	{ "OHH", "OPEN HAT",    2, { {2200, 22}, {1900, 24}, {   0,  0} } },
};

const PhoneDrumKitScreen::Drum* PhoneDrumKitScreen::drumForDigit(uint8_t digit) {
	if(digit >= NumDrums) return nullptr;
	return &kDrums[digit];
}

// =====================================================================
// Geometry — 160x128 budget, dialer-pad-shaped 3x4 keypad layout
//
// Status bar at y=0..10, soft-key bar at y=118..128. Header takes
// y=12..30 (caption, hint, now-playing line). Pad grid starts at
// y=32 with three rows of digits 1..9 and a fourth row containing
// just the digit-0 cell in the middle column.
//
// Cells are 28x18 with 2 px gaps so the 3-column block (3*28 + 2*2
// = 88 px) fits well centred on the 160 px display, and four
// 18-px rows + 3 gaps total 78 px (y=32..110), leaving 8 px of
// breathing room above the soft-key bar.
// =====================================================================

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kHintY        = 22;
static constexpr lv_coord_t kNowY         = 22;
static constexpr lv_coord_t kPadGridX     =  36;   // grid origin (top-left)
static constexpr lv_coord_t kPadGridY     =  34;
static constexpr lv_coord_t kPadCellW     =  28;
static constexpr lv_coord_t kPadCellH     =  18;
static constexpr lv_coord_t kPadCellGapX  =   2;
static constexpr lv_coord_t kPadCellGapY  =   2;

// Helper: pixel x of the centre column for the bottom-row digit-0 slot.
static constexpr lv_coord_t padCellX(uint8_t col) {
	return (lv_coord_t) (kPadGridX + col * (kPadCellW + kPadCellGapX));
}
static constexpr lv_coord_t padCellY(uint8_t row) {
	return (lv_coord_t) (kPadGridY + row * (kPadCellH + kPadCellGapY));
}

// Map the 0..9 digit index to a (row, col) on the 3x4 dialer-pad
// silhouette. Returns false for slots that have no drum mapping
// (the * and # cells, which we never call this for from the screen
// itself but the helper stays defensive in case a future caller
// iterates over all 12 cells).
static bool digitToCell(uint8_t digit, uint8_t* outRow, uint8_t* outCol) {
	switch(digit) {
		case 1: *outRow = 0; *outCol = 0; return true;
		case 2: *outRow = 0; *outCol = 1; return true;
		case 3: *outRow = 0; *outCol = 2; return true;
		case 4: *outRow = 1; *outCol = 0; return true;
		case 5: *outRow = 1; *outCol = 1; return true;
		case 6: *outRow = 1; *outCol = 2; return true;
		case 7: *outRow = 2; *outCol = 0; return true;
		case 8: *outRow = 2; *outCol = 1; return true;
		case 9: *outRow = 2; *outCol = 2; return true;
		case 0: *outRow = 3; *outCol = 1; return true;
		default: return false;
	}
}

// =====================================================================
// Construction
// =====================================================================

PhoneDrumKitScreen::PhoneDrumKitScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  hintLabel(nullptr),
		  nowPlayingLabel(nullptr) {

	for(uint8_t i = 0; i < NumDrums; ++i) pads[i] = nullptr;

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. Same
	// pattern as PhoneDialerScreen / PhoneImeiRevealScreen / etc.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	buildHeader();
	buildPadGrid();

	// Bottom: simple BACK softkey. The screen has no commit/discard
	// distinction, so the left softkey stays empty — keeps the bar
	// uncluttered and signals "tap drums; press BACK to leave".
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");

	// Suppress unused-color warnings for palette macros that the
	// drum-kit doesn't actively style (kept defined for parity with
	// every other Phone* screen, so a future tweak is one-line).
	(void) MP_BG_DARK;
	(void) MP_DIM;
}

PhoneDrumKitScreen::~PhoneDrumKitScreen() {
	// Tear down our two timers if either is still alive — they hold
	// raw pointers into LVGL's timer table and outlive the LVScreen
	// destructor in worst-case shutdown ordering. The lv_timer_del
	// API is safe on a stale pointer because we own the only
	// reference (we never publish envTimer/flashTimer to LVGL
	// callbacks that might re-enter after a screen pop).
	if(envTimer != nullptr) {
		lv_timer_del(envTimer);
		envTimer = nullptr;
	}
	if(flashTimer != nullptr) {
		lv_timer_del(flashTimer);
		flashTimer = nullptr;
	}
	// Belt-and-braces: silence the piezo on tear-down so a long
	// envelope (e.g. CRASH at ~60 ms total) that was mid-flight at
	// pop() time doesn't keep buzzing into whichever screen takes
	// over. Piezo.noTone() is idempotent.
	Piezo.noTone();
}

void PhoneDrumKitScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneDrumKitScreen::onStop() {
	Input::getInstance()->removeListener(this);
	stopEnvelope();
	clearPadFlash();
}

// =====================================================================
// Builders
// =====================================================================

void PhoneDrumKitScreen::buildHeader() {
	// "DRUM KIT" caption — same anchor pattern PhoneImeiRevealScreen
	// uses for "IMEI" and PhoneFortuneCookie uses for "FORTUNE
	// COOKIE". Cyan because it's the headline of the screen.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "DRUM KIT");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);

	// Left-anchored hint that tells the user how to play. Stays
	// visible until the first hit so a curious newcomer doesn't
	// have to guess.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "PRESS 0-9");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(hintLabel, 4, kHintY);

	// Right-anchored "NOW: ..." strip. Empty until the first hit;
	// hitDrum() updates the text to the long name of the most
	// recent voice.
	nowPlayingLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nowPlayingLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nowPlayingLabel, MP_TEXT, 0);
	lv_label_set_text(nowPlayingLabel, "");
	lv_obj_set_align(nowPlayingLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(nowPlayingLabel, -4, kNowY);
}

void PhoneDrumKitScreen::buildPadGrid() {
	for(uint8_t d = 0; d < NumDrums; ++d) {
		uint8_t row = 0, col = 0;
		if(!digitToCell(d, &row, &col)) continue;
		buildPad(d, padCellX(col), padCellY(row));
	}
}

void PhoneDrumKitScreen::buildPad(uint8_t digit, lv_coord_t x, lv_coord_t y) {
	if(digit >= NumDrums) return;

	// Pad rectangle — dim purple background with a 1 px MP_DIM
	// border, mirroring the visual rhythm of PhoneDialerPad's
	// idle keycaps so the screen reads as "the dialer pad,
	// repurposed".
	lv_obj_t* pad = lv_obj_create(obj);
	lv_obj_remove_style_all(pad);
	lv_obj_set_size(pad, kPadCellW, kPadCellH);
	lv_obj_set_pos(pad, x, y);
	lv_obj_set_style_bg_color(pad, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(pad, 200, 0);
	lv_obj_set_style_border_color(pad, MP_DIM, 0);
	lv_obj_set_style_border_width(pad, 1, 0);
	lv_obj_set_style_border_opa(pad, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(pad, 2, 0);
	lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(pad, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_scrollbar_mode(pad, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(pad, 0, 0);

	// Digit glyph — pixelbasic7 cyan, top-left of the pad with 2 px
	// inset so the eye reads the digit as the pad's "name".
	lv_obj_t* digitLabel = lv_label_create(pad);
	lv_obj_set_style_text_font(digitLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(digitLabel, MP_HIGHLIGHT, 0);
	{
		char buf[2] = { (char) ('0' + digit), '\0' };
		lv_label_set_text(digitLabel, buf);
	}
	lv_obj_set_align(digitLabel, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(digitLabel, 2, 1);

	// Drum short name — pixelbasic7 cream, bottom-right of the pad
	// with 1 px inset so it reads as the secondary label.
	lv_obj_t* nameLabel = lv_label_create(pad);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nameLabel, MP_TEXT, 0);
	lv_label_set_text(nameLabel, kDrums[digit].shortName);
	lv_obj_set_align(nameLabel, LV_ALIGN_BOTTOM_RIGHT);
	lv_obj_set_pos(nameLabel, -2, -1);

	pads[digit] = pad;
}

// =====================================================================
// Interaction
// =====================================================================

void PhoneDrumKitScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_0: hitDrum(0); break;
		case BTN_1: hitDrum(1); break;
		case BTN_2: hitDrum(2); break;
		case BTN_3: hitDrum(3); break;
		case BTN_4: hitDrum(4); break;
		case BTN_5: hitDrum(5); break;
		case BTN_6: hitDrum(6); break;
		case BTN_7: hitDrum(7); break;
		case BTN_8: hitDrum(8); break;
		case BTN_9: hitDrum(9); break;

		case BTN_ENTER:
			// Re-fire the last drum the user hit, so a roll pattern
			// (tap a drum, then mash ENTER) doesn't require moving
			// the thumb back to the keypad. With nothing fired yet,
			// ENTER is a no-op.
			if(lastDigit >= 0 && lastDigit < (int8_t) NumDrums) {
				hitDrum((uint8_t) lastDigit);
			}
			break;

		case BTN_BACK:
		case BTN_RIGHT:
			if(softKeys) softKeys->flashRight();
			stopEnvelope();
			clearPadFlash();
			pop();
			break;

		default:
			break;
	}
}

void PhoneDrumKitScreen::hitDrum(uint8_t digit) {
	const Drum* drum = drumForDigit(digit);
	if(drum == nullptr) return;

	lastDigit = (int8_t) digit;

	// Update the "NOW: KICK" / "NOW: SNARE" / ... strip so a user
	// who can't see the small short-name on a 28-px pad still gets
	// confirmation of the voice that fired.
	if(nowPlayingLabel != nullptr) {
		char buf[20];
		snprintf(buf, sizeof(buf), "NOW: %s", drum->longName);
		lv_label_set_text(nowPlayingLabel, buf);
	}

	// Hide the "PRESS 0-9" hint on the very first hit. We never
	// re-show it — once the user has played a drum they know the
	// rules, and the strip would become noise.
	if(hintLabel != nullptr) lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);

	startPadFlash(digit);
	startEnvelope(drum);
}

// =====================================================================
// Audio envelope
// =====================================================================

void PhoneDrumKitScreen::startEnvelope(const Drum* drum) {
	if(drum == nullptr || drum->frameCount == 0) return;

	// Cancel any in-flight voice — drum-machine-correct cut-off so
	// rapid hits don't smear into each other.
	stopEnvelope();

	activeDrum    = drum;
	activeFrameIx = 0;

	// Sound respect: visual flash is unconditional but the piezo
	// itself stays silent in Mute / Vibrate profile. We still walk
	// the envelope's timer so a future toggle (e.g. a "Volume up
	// while drumming" gesture) could re-arm it without restarting
	// the screen — but the actual Piezo.tone() calls are gated.
	if(Settings.get().sound) {
		const Frame& f0 = drum->frames[0];
		if(f0.freq != 0) Piezo.tone(f0.freq, f0.durMs);
		// freq == 0 is an explicit silence step (CLAP frame 1) — we
		// just let the piezo stay quiet; Piezo.tone() will not
		// re-fire until the next frame.
	}

	// Schedule the next frame to land exactly when this one ends.
	// LVGL's lv_timer_t resolution is millisecond, which matches
	// our drum frame durations.
	envTimer = lv_timer_create(&PhoneDrumKitScreen::onEnvTimerStatic,
							   drum->frames[0].durMs, this);
	lv_timer_set_repeat_count(envTimer, 1);
}

void PhoneDrumKitScreen::stepEnvelope() {
	// envTimer fires once and is auto-deleted by LVGL when the
	// repeat count drops to zero, so clear our pointer first to
	// avoid a stale reference if startEnvelope re-arms below.
	envTimer = nullptr;

	if(activeDrum == nullptr) return;

	activeFrameIx++;
	if(activeFrameIx >= activeDrum->frameCount) {
		// Envelope finished — silence the buzzer and drop our
		// tracking state.
		Piezo.noTone();
		activeDrum    = nullptr;
		activeFrameIx = 0;
		return;
	}

	const Frame& f = activeDrum->frames[activeFrameIx];
	if(Settings.get().sound) {
		if(f.freq == 0) {
			Piezo.noTone();
		} else {
			Piezo.tone(f.freq, f.durMs);
		}
	}

	envTimer = lv_timer_create(&PhoneDrumKitScreen::onEnvTimerStatic,
							   f.durMs, this);
	lv_timer_set_repeat_count(envTimer, 1);
}

void PhoneDrumKitScreen::stopEnvelope() {
	if(envTimer != nullptr) {
		lv_timer_del(envTimer);
		envTimer = nullptr;
	}
	if(activeDrum != nullptr) {
		Piezo.noTone();
		activeDrum    = nullptr;
		activeFrameIx = 0;
	}
}

void PhoneDrumKitScreen::onEnvTimerStatic(lv_timer_t* t) {
	if(t == nullptr) return;
	auto* self = static_cast<PhoneDrumKitScreen*>(t->user_data);
	if(self == nullptr) return;
	self->stepEnvelope();
}

// =====================================================================
// Pad flash
// =====================================================================

void PhoneDrumKitScreen::startPadFlash(uint8_t digit) {
	if(digit >= NumDrums) return;
	if(pads[digit] == nullptr) return;

	// Clear the previous flash if the user is hammering keys
	// faster than FlashMs. The new flash supersedes the old one
	// so the eye always tracks the most recent hit, which is the
	// drum-machine-correct visual feedback.
	clearPadFlash();

	flashingPad = (int8_t) digit;
	lv_obj_set_style_bg_color(pads[digit], MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(pads[digit], LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(pads[digit], MP_HIGHLIGHT, 0);

	flashTimer = lv_timer_create(&PhoneDrumKitScreen::onFlashTimerStatic,
								 FlashMs, this);
	lv_timer_set_repeat_count(flashTimer, 1);
}

void PhoneDrumKitScreen::clearPadFlash() {
	if(flashTimer != nullptr) {
		lv_timer_del(flashTimer);
		flashTimer = nullptr;
	}
	if(flashingPad >= 0 && flashingPad < (int8_t) NumDrums
			&& pads[flashingPad] != nullptr) {
		// Restore the idle pad style — same values buildPad set up.
		lv_obj_set_style_bg_color(pads[flashingPad], MP_BG_DARK, 0);
		lv_obj_set_style_bg_opa(pads[flashingPad], 200, 0);
		lv_obj_set_style_border_color(pads[flashingPad], MP_DIM, 0);
	}
	flashingPad = -1;
}

void PhoneDrumKitScreen::onFlashTimerStatic(lv_timer_t* t) {
	if(t == nullptr) return;
	auto* self = static_cast<PhoneDrumKitScreen*>(t->user_data);
	if(self == nullptr) return;
	self->flashTimer = nullptr; // LVGL frees the timer after this returns
	self->clearPadFlash();
}
