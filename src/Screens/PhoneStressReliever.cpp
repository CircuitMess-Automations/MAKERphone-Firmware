#include "PhoneStressReliever.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette -- kept identical to every other Phone*
// widget so the stress reliever slots in beside PhoneFortuneCookie
// (S133), PhoneCoinFlip (S132), PhoneMagic8Ball (S130),
// PhoneDiceRoller (S131), PhoneFlashlight (S134) and PhoneAsciiArt
// (S170) without a visual seam. Same inline-#define convention
// every other Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple

// =====================================================================
// S171 -- PhoneStressReliever -- mood band tables
//
// Three parallel tables keyed on Mood enum values. Order MUST match
// the enum (Calm, Relaxed, Energetic, Frantic, Dizzy, Dazed) so a
// raw enum value can be used as the index. moodFor() picks the band
// from a tap count and never returns out-of-range, but each lookup
// helper still range-checks defensively in case a future caller
// passes a raw integer.
// =====================================================================

static const char* const kMoodLabels[PhoneStressReliever::MoodCount] = {
	"CALM",
	"RELAXED",
	"ENERGETIC",
	"FRANTIC",
	"DIZZY",
	"DAZED",
};

// Two-character pixelbasic16-friendly faces. Kept ASCII-only so they
// render reliably in the existing pixel font (no extended Unicode
// glyphs available).
static const char* const kMoodFaces[PhoneStressReliever::MoodCount] = {
	"^_^",
	":3",
	"O_O",
	">_<",
	"@_@",
	"X_X",
};

// ---------- geometry --------------------------------------------------
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar
//   y=12..18   "STRESS RELIEVER" caption (pixelbasic7, cyan)
//   y=24..84   blob 60x60 centred at x=50..110 (rest pose)
//   y=92..98   mood line   "MOOD: CALM"        (colour-coded)
//   y=102..108 counter line "TAPS: 42  STREAK x3" (cream)
//   y=118..128 PhoneSoftKeyBar
//
// All coordinates centralised here so a future skin only needs to
// tweak these constants. The squish animation walks dx/dy/dw/dh
// offsets relative to the rest pose, so changing the rest pose
// here automatically scales the squish too.

static constexpr lv_coord_t kCaptionY      = 12;

static constexpr lv_coord_t kBlobW         = 60;
static constexpr lv_coord_t kBlobH         = 60;
static constexpr lv_coord_t kBlobX         = (160 - kBlobW) / 2;
static constexpr lv_coord_t kBlobY         = 24;

static constexpr lv_coord_t kMoodLineY     = 92;
static constexpr lv_coord_t kCounterLineY  = 102;

// Squish LUT: per-frame {dw, dh, dx, dy} relative to the rest pose.
// Frame 0 is the rest pose so the final settle frame always returns
// home. Frames 1..3 are increasingly relaxed wobble offsets so the
// blob "bounces back" gradually rather than snapping.
//
// Width grows by up to +6 px (max 110%) while height shrinks by up
// to -8 px (min 87%) -- the classic squish-and-stretch cartoon
// shape. dx/dy compensate so the blob stays visually centred while
// it deforms.
struct Squish {
	int8_t dw;
	int8_t dh;
	int8_t dx;
	int8_t dy;
};

static constexpr Squish kSquishLUT[PhoneStressReliever::SquishFrames] = {
	{ +0, +0, +0, +0 },   // frame 0 -- rest pose
	{ +6, -8, -3, +4 },   // frame 1 -- max squish
	{ +4, -4, -2, +2 },   // frame 2 -- partial recovery
	{ +2, -2, -1, +1 },   // frame 3 -- almost rest
};

// ---------- public statics --------------------------------------------

PhoneStressReliever::Mood PhoneStressReliever::moodFor(uint16_t tapCount) {
	if(tapCount == 0)        return Mood::Calm;
	if(tapCount <= 5)        return Mood::Relaxed;
	if(tapCount <= 15)       return Mood::Energetic;
	if(tapCount <= 30)       return Mood::Frantic;
	if(tapCount <= 60)       return Mood::Dizzy;
	return Mood::Dazed;
}

const char* PhoneStressReliever::moodLabel(Mood mood) {
	const uint8_t i = static_cast<uint8_t>(mood);
	if(i >= MoodCount) return "";
	return kMoodLabels[i];
}

const char* PhoneStressReliever::moodFace(Mood mood) {
	const uint8_t i = static_cast<uint8_t>(mood);
	if(i >= MoodCount) return "";
	return kMoodFaces[i];
}

// Helper: per-mood blob fill colour. Kept private to the .cpp because
// the colour palette is implementation-detail -- callers should use
// the public moodLabel() / moodFace() table instead.
static lv_color_t moodBlobColor(PhoneStressReliever::Mood mood) {
	switch(mood) {
		case PhoneStressReliever::Mood::Calm:      return MP_HIGHLIGHT;
		case PhoneStressReliever::Mood::Relaxed:   return MP_TEXT;
		case PhoneStressReliever::Mood::Energetic: return MP_ACCENT;
		case PhoneStressReliever::Mood::Frantic:   return MP_ACCENT;
		case PhoneStressReliever::Mood::Dizzy:     return MP_LABEL_DIM;
		case PhoneStressReliever::Mood::Dazed:     return MP_DIM;
		default:                                   return MP_HIGHLIGHT;
	}
}

// ---------- ctor / dtor -----------------------------------------------

PhoneStressReliever::PhoneStressReliever()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  blob(nullptr),
		  faceLabel(nullptr),
		  moodLineLabel(nullptr),
		  counterLineLabel(nullptr) {

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern as every other Phone* utility screen.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, 160);
	lv_obj_set_pos(captionLabel, 0, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "STRESS RELIEVER");

	buildHud();

	// Bottom soft-key bar; populated here once because the screen has
	// no mode-dependent label changes (TAP/BACK are always valid).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("TAP");
	softKeys->setRight("BACK");

	// Final paint of the idle CALM face.
	renderState();
}

PhoneStressReliever::~PhoneStressReliever() {
	stopSquishTimer();
}

// ---------- build helpers ---------------------------------------------

void PhoneStressReliever::buildHud() {
	// --- the blob -------------------------------------------------
	// A 60x60 rounded rect with a thin highlight border. Background
	// colour is set by renderState() and updated on every mood-band
	// transition. The body is parented to obj (not statusBar) so it
	// shares the same z-order as the rest of the foreground hud.
	blob = lv_obj_create(obj);
	lv_obj_remove_style_all(blob);
	lv_obj_set_size(blob, kBlobW, kBlobH);
	lv_obj_set_pos(blob, kBlobX, kBlobY);
	lv_obj_set_style_radius(blob, 12, 0);
	lv_obj_set_style_bg_opa(blob, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(blob, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(blob, 1, 0);
	lv_obj_set_style_border_color(blob, MP_TEXT, 0);
	lv_obj_set_style_pad_all(blob, 0, 0);
	lv_obj_clear_flag(blob, LV_OBJ_FLAG_SCROLLABLE);

	// Face glyph -- single pixelbasic16 label centred inside the
	// blob. Parenting it to the blob means the squish animation
	// drags the face along for free.
	faceLabel = lv_label_create(blob);
	lv_obj_set_style_text_font(faceLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(faceLabel, MP_BG_DARK, 0);
	lv_obj_set_style_text_align(faceLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(faceLabel, "");
	lv_obj_align(faceLabel, LV_ALIGN_CENTER, 0, 0);

	// Mood line -- "MOOD: CALM" -- coloured per band so the user
	// gets fast peripheral feedback without having to read it.
	moodLineLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(moodLineLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(moodLineLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(moodLineLabel, 160);
	lv_obj_set_pos(moodLineLabel, 0, kMoodLineY);
	lv_obj_set_style_text_align(moodLineLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(moodLineLabel, "");

	// Counter line -- "TAPS: 42  STREAK x3" -- always cream so the
	// numbers stay readable as the mood line shifts colour above.
	counterLineLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(counterLineLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(counterLineLabel, MP_TEXT, 0);
	lv_obj_set_width(counterLineLabel, 160);
	lv_obj_set_pos(counterLineLabel, 0, kCounterLineY);
	lv_obj_set_style_text_align(counterLineLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(counterLineLabel, "");
}

// ---------- lifecycle -------------------------------------------------

void PhoneStressReliever::onStart() {
	Input::getInstance()->addListener(this);
	// Always re-enter as the calm idle face; the squish timer is
	// guaranteed dead because onStop() tears it down, but the
	// explicit re-render is cheap insurance.
	renderState();
	applySquish(0);
}

void PhoneStressReliever::onStop() {
	Input::getInstance()->removeListener(this);
	stopSquishTimer();
	squishLeft = 0;
}

// S168: shake-to-tap. The L+R chord registers a single tap so the
// blob squishes + mood escalates the same way as if the user had
// pressed TAP. Forwards to tapBlob() so the mood / streak / squish
// pipeline is reused unchanged.
void PhoneStressReliever::onShake() {
	tapBlob();
}

// ---------- state mutation --------------------------------------------

void PhoneStressReliever::tapBlob() {
	const uint32_t now = millis();

	// Streak bookkeeping: a tap inside the StreakWindowMs window
	// extends the streak (capped at MaxStreak); anything later
	// resets it to 1. This deliberately uses a raw delta against
	// lastTapMs so a 32-bit wraparound (~49 days) just resets the
	// streak rather than producing a giant value.
	if(lastTapMs != 0 && (now - lastTapMs) <= StreakWindowMs) {
		if(streak < MaxStreak) {
			streak = static_cast<uint8_t>(streak + 1);
		}
	} else {
		streak = 1;
	}
	lastTapMs = now;

	if(tapCount < TapCountMax) {
		tapCount = static_cast<uint16_t>(tapCount + 1);
	}

	// Kick the squish animation. The timer is allowed to restart
	// mid-flight -- mashing keys produces continuous wobble, which
	// is exactly the "fidget" affordance.
	squishLeft = SquishFrames;
	applySquish(SquishFrames - 1);  // jump straight to max squish
	startSquishTimer();

	renderState();
}

void PhoneStressReliever::resetState() {
	tapCount  = 0;
	streak    = 1;
	lastTapMs = 0;
	stopSquishTimer();
	squishLeft = 0;
	applySquish(0);
	renderState();
}

// ---------- render ----------------------------------------------------

void PhoneStressReliever::renderState() {
	const Mood     mood   = moodFor(tapCount);
	const lv_color_t col  = moodBlobColor(mood);

	if(blob != nullptr) {
		lv_obj_set_style_bg_color(blob, col, 0);
		// Border colour follows the blob colour, but two stops dimmer
		// when the user is past the FRANTIC band so the blob doesn't
		// glow too aggressively in the late-game DIZZY/DAZED states.
		lv_obj_set_style_border_color(
				blob,
				(mood == Mood::Dizzy || mood == Mood::Dazed)
						? MP_LABEL_DIM
						: MP_TEXT,
				0);
	}

	if(faceLabel != nullptr) {
		const char* face = moodFace(mood);
		lv_label_set_text(faceLabel, face != nullptr ? face : "");
		lv_obj_align(faceLabel, LV_ALIGN_CENTER, 0, 0);
	}

	if(moodLineLabel != nullptr) {
		// The mood label colour mirrors the blob colour so peripheral
		// glances tell the same story as the centred face.
		lv_obj_set_style_text_color(moodLineLabel, col, 0);
		char buf[24];
		const char* name = moodLineLabel != nullptr ? PhoneStressReliever::moodLabel(mood) : "";
		snprintf(buf, sizeof(buf), "MOOD: %s", name != nullptr ? name : "");
		lv_label_set_text(moodLineLabel, buf);
	}

	if(counterLineLabel != nullptr) {
		char buf[32];
		snprintf(buf, sizeof(buf),
		         "TAPS: %u  STREAK x%u",
		         static_cast<unsigned>(tapCount),
		         static_cast<unsigned>(streak));
		lv_label_set_text(counterLineLabel, buf);
	}
}

// ---------- squish animation ------------------------------------------

void PhoneStressReliever::applySquish(uint8_t frameIdx) {
	if(blob == nullptr) return;
	const uint8_t i = (frameIdx >= SquishFrames) ? 0
	                                             : frameIdx;
	const Squish s = kSquishLUT[i];
	lv_obj_set_size(blob,
	                static_cast<lv_coord_t>(kBlobW + s.dw),
	                static_cast<lv_coord_t>(kBlobH + s.dh));
	lv_obj_set_pos(blob,
	               static_cast<lv_coord_t>(kBlobX + s.dx),
	               static_cast<lv_coord_t>(kBlobY + s.dy));
}

void PhoneStressReliever::startSquishTimer() {
	if(squishTimer != nullptr) return;
	squishTimer = lv_timer_create(&PhoneStressReliever::onSquishTickStatic,
	                              SquishPeriodMs, this);
}

void PhoneStressReliever::stopSquishTimer() {
	if(squishTimer == nullptr) return;
	lv_timer_del(squishTimer);
	squishTimer = nullptr;
}

void PhoneStressReliever::onSquishTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneStressReliever*>(timer->user_data);
	if(self == nullptr) return;

	if(self->squishLeft <= 1) {
		// Final frame: return to rest pose and tear the timer down
		// so we go back to zero per-frame cost in the idle case.
		self->applySquish(0);
		self->squishLeft = 0;
		self->stopSquishTimer();
		return;
	}

	self->squishLeft--;
	// squishLeft now indexes into the LUT so frame 0 (rest) is hit
	// only on the final settle path above. Mid-flight frames walk
	// 3 -> 2 -> 1 -> 0 so the blob bounces back gradually.
	self->applySquish(self->squishLeft);
}

// ---------- input -----------------------------------------------------

void PhoneStressReliever::buttonPressed(uint i) {
	switch(i) {
		// Any digit (0..9), centre key, ENTER, the right d-pad arrow
		// or the right bumper all count as a tap. This deliberately
		// mirrors PhoneMagic8Ball / PhoneCoinFlip / PhoneDiceRoller
		// where "press anything" is the affordance the toy is
		// supposed to provide.
		case BTN_0:
		case BTN_1:
		case BTN_2:
		case BTN_3:
		case BTN_4:
		case BTN_5:
		case BTN_6:
		case BTN_7:
		case BTN_8:
		case BTN_9:
		case BTN_ENTER:
		case BTN_RIGHT:
		case BTN_R:
			if(softKeys) softKeys->flashLeft();
			tapBlob();
			break;

		case BTN_LEFT:
			// Reset to instant CALM. Useful when the user has
			// climbed into DAZED territory and wants a fresh run.
			resetState();
			break;

		case BTN_L:
			// Left bumper = same as the right softkey ("BACK") -- the
			// PhoneMagic8Ball family pairs the bumper with the right
			// softkey for "exit" so the muscle memory carries.
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneStressReliever::buttonReleased(uint i) {
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

void PhoneStressReliever::buttonHeld(uint i) {
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
