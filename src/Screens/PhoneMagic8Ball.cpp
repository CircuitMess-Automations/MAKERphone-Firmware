#include "PhoneMagic8Ball.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone*
// widget so the Magic 8 Ball slots in beside PhoneCalculator (S60),
// PhoneAlarmClock (S124), PhoneTimers (S125), PhoneCurrencyConverter
// (S126), PhoneUnitConverter (S127), PhoneWorldClock (S128) and
// PhoneVirtualPet (S129) without a visual seam. Same inline-#define
// convention every other Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple

// =====================================================================
// S130 — PhoneMagic8Ball — answer table
//
// The canon 20-answer Magic 8-Ball script. Order matters: the first
// AffirmativeCount entries are the affirmative ("yes") answers, the
// next NonCommittalCount are the wishy-washy "ask again" ones, and
// the final NegativeCount are the discouraging ones. toneOf() reads
// off the index, so reordering here flips the colour-coding.
//
// All strings are sized so they wrap within the 56 px inner window
// at pixelbasic7 (each line is ~7 px tall, the window holds ~6 lines
// comfortably). pixelbasic16 is used only for the idle "8" face and
// for short single-word emphasis -- the answers themselves render in
// pixelbasic7 because they need to wrap.
// =====================================================================

static const char* const kAnswers[PhoneMagic8Ball::AnswerCount] = {
		// Affirmative (10).
		"IT IS\nCERTAIN",
		"IT IS\nDECIDEDLY SO",
		"WITHOUT\nA DOUBT",
		"YES\nDEFINITELY",
		"YOU MAY\nRELY ON IT",
		"AS I SEE IT\nYES",
		"MOST\nLIKELY",
		"OUTLOOK\nGOOD",
		"YES",
		"SIGNS POINT\nTO YES",
		// Non-committal (5).
		"REPLY HAZY\nTRY AGAIN",
		"ASK AGAIN\nLATER",
		"BETTER NOT\nTELL YOU NOW",
		"CANNOT\nPREDICT NOW",
		"CONCENTRATE\nAND ASK\nAGAIN",
		// Negative (5).
		"DON'T\nCOUNT ON IT",
		"MY REPLY\nIS NO",
		"MY SOURCES\nSAY NO",
		"OUTLOOK\nNOT SO GOOD",
		"VERY\nDOUBTFUL",
};

static_assert(PhoneMagic8Ball::AffirmativeCount
              + PhoneMagic8Ball::NonCommittalCount
              + PhoneMagic8Ball::NegativeCount
              == PhoneMagic8Ball::AnswerCount,
              "Magic 8-Ball answer category counts must add up to AnswerCount");

// ---------- geometry --------------------------------------------------
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar (signal + clock + battery)
//   y=12..20   "MAGIC 8 BALL" caption (pixelbasic7, cyan)
//   y=24..96   the 8-ball -- outer disc 72x72 centred at x=44..116
//              (inner window 56x56 nested inside, centred)
//   y=100..108 "ASK A QUESTION" hint (pixelbasic7, dim purple)
//   y=118..128 PhoneSoftKeyBar
//
// All coordinates centralised here so a future skin (e.g. a Y2K-style
// translucent ball) only needs to tweak these constants.

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kHintY         = 102;

// Outer disc: 72x72 black ball with cyan border.
static constexpr lv_coord_t kBallW         = 72;
static constexpr lv_coord_t kBallH         = 72;
static constexpr lv_coord_t kBallX         = (160 - kBallW) / 2;
static constexpr lv_coord_t kBallY         = 24;

// Inner disc: 56x56 dim-purple "answer triangle" window, centred
// inside the outer ball. The geometry math drops to a single
// LV_ALIGN_CENTER call once the window is parented to the ball.
static constexpr lv_coord_t kWindowW       = 56;
static constexpr lv_coord_t kWindowH       = 56;

// Wobble LUT (one ±2 px x and ±1 px y entry per ShakeFrames slot).
// Hand-picked so the wobble reads as a deterministic-but-jittery
// toss rather than a clean sine. Indexed by frameLeft (0..7) so the
// final settle frame always returns to (0, 0).
struct Wobble {
	int8_t dx;
	int8_t dy;
};

static constexpr Wobble kWobbleLUT[PhoneMagic8Ball::ShakeFrames] = {
		{ +0, +0 },   // frameLeft == 0 -> settle
		{ -2,  0 },
		{ +2,  0 },
		{ -1, -1 },
		{ +2, +1 },
		{ -2, +1 },
		{ +1, -1 },
		{ -1,  0 },
};

// ---------- public statics --------------------------------------------

const char* PhoneMagic8Ball::answerAt(uint8_t idx) {
	if(idx >= AnswerCount) return nullptr;
	return kAnswers[idx];
}

PhoneMagic8Ball::Tone PhoneMagic8Ball::toneOf(uint8_t idx) {
	if(idx < AffirmativeCount) {
		return Tone::Affirmative;
	}
	if(idx < (uint8_t)(AffirmativeCount + NonCommittalCount)) {
		return Tone::NonCommittal;
	}
	if(idx < AnswerCount) {
		return Tone::Negative;
	}
	return Tone::NonCommittal;
}

// ---------- ctor / dtor -----------------------------------------------

PhoneMagic8Ball::PhoneMagic8Ball()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  hintLabel(nullptr),
		  outerBall(nullptr),
		  innerWindow(nullptr),
		  answerLabel(nullptr) {

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
	lv_label_set_text(captionLabel, "MAGIC 8 BALL");

	buildHud();

	// Seed rand() with a value distinct from every other Phone* game's
	// seed-mode -- same magic-XOR pattern PhoneSimon uses (S97), so two
	// screens pushed in quick succession do not collapse onto the same
	// random sequence.
	srand(static_cast<unsigned>(millis() ^ 0x4ABA11));

	// Bottom soft-key bar; populated here once because the screen has
	// no mode-dependent label changes (SHAKE/BACK are always valid).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("SHAKE");
	softKeys->setRight("BACK");

	// Final paint of the idle "8" face. Done after the soft-key bar
	// is constructed so any future sub-call that touches the bar (it
	// doesn't today, but the order matches PhoneVirtualPet et al)
	// doesn't dereference a null pointer.
	renderIdle();
}

PhoneMagic8Ball::~PhoneMagic8Ball() {
	stopShakeTimer();
}

// ---------- build helpers ---------------------------------------------

void PhoneMagic8Ball::buildHud() {
	// Outer ball -- a black filled circle with a thin cyan border.
	// LV_RADIUS_CIRCLE coerces the rectangle into a perfect disc as
	// long as width == height (we keep them equal at 72 above).
	outerBall = lv_obj_create(obj);
	lv_obj_remove_style_all(outerBall);
	lv_obj_set_size(outerBall, kBallW, kBallH);
	lv_obj_set_pos(outerBall, kBallX, kBallY);
	lv_obj_set_style_radius(outerBall, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_opa(outerBall, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(outerBall, MP_BG_DARK, 0);
	lv_obj_set_style_border_width(outerBall, 1, 0);
	lv_obj_set_style_border_color(outerBall, MP_HIGHLIGHT, 0);
	lv_obj_set_style_pad_all(outerBall, 0, 0);
	lv_obj_clear_flag(outerBall, LV_OBJ_FLAG_SCROLLABLE);

	// Inner window -- the dim purple "answer triangle". Parented to
	// the outer ball so wobbling the ball naturally drags the window
	// along with it. Centred via LV_ALIGN_CENTER so a future ball
	// resize doesn't desync the two discs.
	innerWindow = lv_obj_create(outerBall);
	lv_obj_remove_style_all(innerWindow);
	lv_obj_set_size(innerWindow, kWindowW, kWindowH);
	lv_obj_align(innerWindow, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_radius(innerWindow, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_opa(innerWindow, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(innerWindow, MP_DIM, 0);
	lv_obj_set_style_border_width(innerWindow, 1, 0);
	lv_obj_set_style_border_color(innerWindow, MP_LABEL_DIM, 0);
	lv_obj_set_style_pad_all(innerWindow, 2, 0);
	lv_obj_clear_flag(innerWindow, LV_OBJ_FLAG_SCROLLABLE);

	// Answer label -- starts as the idle "8" in pixelbasic16. The
	// renderAnswer() helper switches to pixelbasic7 (with wrap) once
	// the screen is showing real answers, since the canon answers
	// need 2-3 lines to fit inside the 56 px window.
	answerLabel = lv_label_create(innerWindow);
	lv_obj_set_style_text_font(answerLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(answerLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(answerLabel, kWindowW - 6);
	lv_obj_align(answerLabel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_align(answerLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(answerLabel, LV_LABEL_LONG_WRAP);
	lv_label_set_text(answerLabel, "8");

	// Bottom hint. Stays static for the screen's lifetime.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(hintLabel, 160);
	lv_obj_set_pos(hintLabel, 0, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "ASK A QUESTION");
}

// ---------- lifecycle -------------------------------------------------

void PhoneMagic8Ball::onStart() {
	Input::getInstance()->addListener(this);
	// Make sure we always re-enter as the calm idle "8" face -- a
	// previous push/pop cycle could have left the screen mid-tumble
	// (it cannot today because we tear the timer down in onStop, but
	// the explicit reset is cheap insurance).
	renderIdle();
}

void PhoneMagic8Ball::onStop() {
	Input::getInstance()->removeListener(this);
	stopShakeTimer();
	shaking = false;
}

// ---------- render ----------------------------------------------------

void PhoneMagic8Ball::renderIdle() {
	if(answerLabel == nullptr) return;
	if(outerBall   != nullptr) {
		lv_obj_set_pos(outerBall, kBallX, kBallY);
	}
	lv_obj_set_style_text_font(answerLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(answerLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(answerLabel, "8");
	lv_obj_align(answerLabel, LV_ALIGN_CENTER, 0, 0);
}

void PhoneMagic8Ball::renderAnswer(uint8_t idx, bool tumbling) {
	if(answerLabel == nullptr) return;

	const char* text = answerAt(idx);
	if(text == nullptr) text = "...";

	// Real answers wrap onto 2-3 lines so we drop to pixelbasic7
	// the moment we leave the idle "8" face. Tumble frames render
	// in dim purple (a "blur" while the ball is still in motion),
	// settled answers render coloured by tone.
	lv_obj_set_style_text_font(answerLabel, &pixelbasic7, 0);

	lv_color_t col;
	if(tumbling) {
		col = MP_LABEL_DIM;
	} else {
		switch(toneOf(idx)) {
			case Tone::Affirmative:  col = MP_HIGHLIGHT; break;
			case Tone::Negative:     col = MP_ACCENT;    break;
			case Tone::NonCommittal:
			default:                 col = MP_TEXT;      break;
		}
	}
	lv_obj_set_style_text_color(answerLabel, col, 0);
	lv_label_set_text(answerLabel, text);
	lv_obj_align(answerLabel, LV_ALIGN_CENTER, 0, 0);
}

// ---------- shake animation -------------------------------------------

void PhoneMagic8Ball::beginShake() {
	if(shaking) return;   // mashing 5 must not extend the animation
	shaking         = true;
	shakeFrameLeft  = ShakeFrames;
	finalAnswerIdx  = static_cast<uint8_t>(rand() % AnswerCount);

	// Render the first tumble frame immediately so the user sees
	// instantaneous feedback for their press; the timer takes over
	// from frame 2 onwards.
	const uint8_t firstTumbleIdx = static_cast<uint8_t>(rand() % AnswerCount);
	renderAnswer(firstTumbleIdx, true);
	wobbleBall(shakeFrameLeft);
	currentIdx = firstTumbleIdx;

	startShakeTimer();
}

void PhoneMagic8Ball::wobbleBall(uint8_t frameLeft) {
	if(outerBall == nullptr) return;
	const uint8_t i = (frameLeft >= ShakeFrames) ? 0
	                                             : (uint8_t)(frameLeft % ShakeFrames);
	const Wobble w = kWobbleLUT[i];
	lv_obj_set_pos(outerBall,
	               (lv_coord_t)(kBallX + w.dx),
	               (lv_coord_t)(kBallY + w.dy));
}

void PhoneMagic8Ball::startShakeTimer() {
	if(shakeTimer != nullptr) return;
	shakeTimer = lv_timer_create(&PhoneMagic8Ball::onShakeTickStatic,
	                             ShakePeriodMs, this);
}

void PhoneMagic8Ball::stopShakeTimer() {
	if(shakeTimer == nullptr) return;
	lv_timer_del(shakeTimer);
	shakeTimer = nullptr;
}

void PhoneMagic8Ball::onShakeTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneMagic8Ball*>(timer->user_data);
	if(self == nullptr) return;

	if(self->shakeFrameLeft <= 1) {
		// Final frame: settle on the chosen answer, restore the ball
		// to its rest position and tear the timer down so we go back
		// to zero per-frame cost in the idle case.
		self->renderAnswer(self->finalAnswerIdx, false);
		self->wobbleBall(0);
		self->currentIdx     = self->finalAnswerIdx;
		self->shakeFrameLeft = 0;
		self->shaking        = false;
		self->stopShakeTimer();
		return;
	}

	// Mid-tumble frame: pick a fresh decoy answer and wobble.
	const uint8_t tumbleIdx = static_cast<uint8_t>(rand() % AnswerCount);
	self->currentIdx = tumbleIdx;
	self->renderAnswer(tumbleIdx, true);
	self->shakeFrameLeft--;
	self->wobbleBall(self->shakeFrameLeft);
}

// ---------- input -----------------------------------------------------

void PhoneMagic8Ball::buttonPressed(uint i) {
	switch(i) {
		// Any digit (0..9) shakes -- mirrors the original toy where
		// "any tap" tossed the ball. The dialer-row digits and the
		// centre-row 5 all funnel through beginShake().
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
			if(softKeys) softKeys->flashLeft();
			beginShake();
			break;

		case BTN_L:
			// Left bumper = same as the left softkey ("SHAKE") -- the
			// PhoneCalculator / PhoneCurrencyConverter family pairs
			// the bumper with the left softkey so the muscle memory
			// carries.
			if(softKeys) softKeys->flashLeft();
			beginShake();
			break;

		case BTN_R:
			// Right bumper = same as the right softkey ("BACK"). No
			// long-press distinction needed on this screen.
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		case BTN_LEFT:
		case BTN_RIGHT:
			// The screen has no horizontal cursor today, but a future
			// "history of last N answers" feature could repurpose
			// these. For now we simply absorb the keys so they don't
			// fall through to anything else.
			break;

		default:
			break;
	}
}

void PhoneMagic8Ball::buttonReleased(uint i) {
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

void PhoneMagic8Ball::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK is the same as a short tap -- exit the
			// screen. The flag suppresses the matching short-press
			// fire-back on release.
			backLongFired = true;
			pop();
			break;

		default:
			break;
	}
}
