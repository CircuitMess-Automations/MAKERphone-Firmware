#include "PhoneCoinFlip.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone*
// widget so the coin-flip toy slots in beside PhoneCalculator (S60),
// PhoneAlarmClock (S124), PhoneTimers (S125), PhoneCurrencyConverter
// (S126), PhoneUnitConverter (S127), PhoneWorldClock (S128),
// PhoneVirtualPet (S129), PhoneMagic8Ball (S130) and PhoneDiceRoller
// (S131) without a visual seam. Same inline-#define convention
// every other Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption / tails
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream / heads
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple shadow

// =====================================================================
// S132 — PhoneCoinFlip — geometry budget
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar
//   y=12..20   "COIN FLIP" caption (pixelbasic7, cyan)
//   y=24..70   toss area (coin animates inside this band)
//   y=72       baseline / shadow oval
//   y=78..94   result label "HEADS" / "TAILS" (pixelbasic16)
//   y=98..104  history strip (pixelbasic7)
//   y=106..112 stats line (pixelbasic7, dim)
//   y=118..128 PhoneSoftKeyBar
//
// The coin's resting position has its bottom edge sitting on the
// baseline (y = 72). At peak air-time it climbs by kPeakHeight px.
// All coordinates centralised here so a future skin tweak only
// edits this block.

static constexpr lv_coord_t kCaptionY      = 12;

// Coin geometry. The coin is drawn as a rounded rect; CoinW is the
// full ("face-on") width; on edge-on frames we shrink it down to
// CoinEdgeMinW so the user's eye reads it as a 3D rotation.
static constexpr lv_coord_t kCoinFullW     = 26;
static constexpr lv_coord_t kCoinH         = 26;
static constexpr lv_coord_t kCoinEdgeMinW  = 2;
static constexpr lv_coord_t kCoinCenterX   = 80;       // horizontal centre
static constexpr lv_coord_t kCoinBaselineY = 72;       // bottom edge at rest
static constexpr lv_coord_t kPeakHeight    = 40;       // px climbed at apex

// Threshold below which we hide the coin's face label so the
// rotation reads as edge-on rather than as a thin coin with a
// stretched letter on it.
static constexpr lv_coord_t kFaceHideW     = 10;

// Shadow oval geometry (just two-tone strip on the baseline).
static constexpr lv_coord_t kShadowW       = 30;
static constexpr lv_coord_t kShadowH       = 3;
static constexpr lv_coord_t kShadowY       = 75;

static constexpr lv_coord_t kResultY       = 78;
static constexpr lv_coord_t kHistoryY      = 98;
static constexpr lv_coord_t kStatsY        = 106;

// =====================================================================

// Public history accessor -- newest entry at index 0.
uint8_t PhoneCoinFlip::historyAt(uint8_t which) const {
	if(which >= HistorySize)        return 0xFF;
	if(which >= historyFill)        return 0xFF;
	return (uint8_t) history[which];
}

// ---------- ctor / dtor -----------------------------------------------

PhoneCoinFlip::PhoneCoinFlip()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  hintLabel(nullptr),
		  coinBody(nullptr),
		  coinLabel(nullptr),
		  shadow(nullptr),
		  resultLabel(nullptr),
		  historyLabel(nullptr),
		  statsLabel(nullptr) {

	for(uint8_t i = 0; i < HistorySize; ++i) history[i] = FaceHeads;

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
	lv_label_set_text(captionLabel, "COIN FLIP");

	buildHud();

	// Seed rand() with a value distinct from PhoneMagic8Ball's seed
	// (0x8BA11) and PhoneDiceRoller's seed (0xD1CE12) so pushing one
	// toy straight after another doesn't collapse onto the same RNG
	// sequence.
	srand(static_cast<unsigned>(millis() ^ 0xC01F11));

	// Bottom soft-key bar; populated here once because the screen has
	// no mode-dependent label changes (FLIP/BACK are always valid).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("FLIP");
	softKeys->setRight("BACK");

	// Initial paint: idle resting coin + empty result/history/stats.
	renderRestingCoin();
	renderResult();
	renderHistory();
	renderStats();
}

PhoneCoinFlip::~PhoneCoinFlip() {
	stopTossTimer();
}

// ---------- build helpers ---------------------------------------------

void PhoneCoinFlip::buildHud() {
	// Shadow strip on the baseline. We draw it with two stacked
	// rounded rects' worth of style: a narrow dim strip that we
	// shrink at apex and re-grow on landing so the toss reads as
	// 3D distance.
	shadow = lv_obj_create(obj);
	lv_obj_remove_style_all(shadow);
	lv_obj_set_size(shadow, kShadowW, kShadowH);
	lv_obj_set_pos(shadow, kCoinCenterX - kShadowW / 2, kShadowY);
	lv_obj_set_style_radius(shadow, kShadowH, 0);
	lv_obj_set_style_bg_opa(shadow, LV_OPA_70, 0);
	lv_obj_set_style_bg_color(shadow, MP_DIM, 0);
	lv_obj_set_style_border_width(shadow, 0, 0);
	lv_obj_clear_flag(shadow, LV_OBJ_FLAG_SCROLLABLE);

	// The coin -- a rounded rect drawn with a thin cyan border. The
	// "face" is rendered as a single-letter label centred inside.
	coinBody = lv_obj_create(obj);
	lv_obj_remove_style_all(coinBody);
	lv_obj_set_size(coinBody, kCoinFullW, kCoinH);
	lv_obj_set_pos(coinBody, kCoinCenterX - kCoinFullW / 2,
	               kCoinBaselineY - kCoinH);
	lv_obj_set_style_radius(coinBody, kCoinH / 2, 0);
	lv_obj_set_style_bg_opa(coinBody, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(coinBody, MP_TEXT, 0);
	lv_obj_set_style_border_width(coinBody, 1, 0);
	lv_obj_set_style_border_color(coinBody, MP_HIGHLIGHT, 0);
	lv_obj_set_style_pad_all(coinBody, 0, 0);
	lv_obj_clear_flag(coinBody, LV_OBJ_FLAG_SCROLLABLE);

	coinLabel = lv_label_create(coinBody);
	lv_obj_set_style_text_font(coinLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(coinLabel, MP_BG_DARK, 0);
	lv_obj_align(coinLabel, LV_ALIGN_CENTER, 0, 0);
	lv_label_set_text(coinLabel, "H");

	// Result line below the coin -- "HEADS" / "TAILS" / "—".
	resultLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(resultLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(resultLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(resultLabel, 160);
	lv_obj_set_pos(resultLabel, 0, kResultY);
	lv_obj_set_style_text_align(resultLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(resultLabel, "PRESS FLIP");

	// History strip -- pixelbasic7 row of last-10 letters.
	historyLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(historyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(historyLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(historyLabel, 160);
	lv_obj_set_pos(historyLabel, 0, kHistoryY);
	lv_obj_set_style_text_align(historyLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(historyLabel, "");

	// Stats line.
	statsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(statsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(statsLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(statsLabel, 160);
	lv_obj_set_pos(statsLabel, 0, kStatsY);
	lv_obj_set_style_text_align(statsLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(statsLabel, "0:CLEAR  5:FLIP");

	// (No persistent hint label -- the stats line carries the
	// keypad reminders once the user has flipped at least once.
	// We keep `hintLabel` nullptr so renderStats() owns the slot.)
	hintLabel = nullptr;
}

// ---------- lifecycle -------------------------------------------------

void PhoneCoinFlip::onStart() {
	Input::getInstance()->addListener(this);
	// Always re-enter as the calm idle face.
	renderRestingCoin();
	renderResult();
	renderHistory();
	renderStats();
}

void PhoneCoinFlip::onStop() {
	Input::getInstance()->removeListener(this);
	stopTossTimer();
	tossing = false;
}

// ---------- render ----------------------------------------------------

void PhoneCoinFlip::renderResult() {
	if(resultLabel == nullptr) return;

	if(tossing) {
		lv_obj_set_style_text_color(resultLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(resultLabel, "...");
		return;
	}

	if(totalFlips == 0) {
		// Idle pre-flip prompt.
		lv_obj_set_style_text_color(resultLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(resultLabel, "PRESS FLIP");
		return;
	}

	if(settledFace == FaceHeads) {
		lv_obj_set_style_text_color(resultLabel, MP_TEXT, 0);
		lv_label_set_text(resultLabel, "HEADS");
	} else {
		lv_obj_set_style_text_color(resultLabel, MP_HIGHLIGHT, 0);
		lv_label_set_text(resultLabel, "TAILS");
	}
}

void PhoneCoinFlip::renderHistory() {
	if(historyLabel == nullptr) return;

	if(historyFill == 0) {
		lv_label_set_text(historyLabel, "");
		return;
	}

	// "H T H ..." rendered newest-first. We pad with a leading
	// space between letters so the eye groups them.
	char buf[2 * HistorySize + 1];
	uint8_t pos = 0;
	for(uint8_t i = 0; i < historyFill && i < HistorySize; ++i) {
		if(i > 0) {
			if(pos < sizeof(buf) - 1) buf[pos++] = ' ';
		}
		if(pos < sizeof(buf) - 1) {
			buf[pos++] = (history[i] == FaceHeads) ? 'H' : 'T';
		}
	}
	buf[pos] = '\0';
	lv_label_set_text(historyLabel, buf);
}

void PhoneCoinFlip::renderStats() {
	if(statsLabel == nullptr) return;

	if(totalFlips == 0) {
		// Idle help text -- explains the two non-obvious controls
		// before the user has flipped once.
		lv_obj_set_style_text_color(statsLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(statsLabel, "0:CLEAR  5:FLIP");
		return;
	}

	char buf[40];
	snprintf(buf, sizeof(buf),
	         "FLIPS:%u H:%u STREAK:%u",
	         (unsigned) totalFlips,
	         (unsigned) headsTotal,
	         (unsigned) streak);
	lv_obj_set_style_text_color(statsLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(statsLabel, buf);
}

void PhoneCoinFlip::renderTossFrame(uint8_t face, int yPx, int widthPx, bool edgeOn) {
	if(coinBody == nullptr || coinLabel == nullptr || shadow == nullptr) return;

	if(widthPx < kCoinEdgeMinW) widthPx = kCoinEdgeMinW;
	if(widthPx > kCoinFullW)    widthPx = kCoinFullW;

	const int coinX = kCoinCenterX - widthPx / 2;
	lv_obj_set_size(coinBody, widthPx, kCoinH);
	lv_obj_set_pos(coinBody, coinX, yPx);

	// Coin face colour follows the displayed face so heads / tails
	// have a subtly different fill -- heads = warm cream, tails =
	// cyan (mirrors the result label colours).
	lv_color_t fill = (face == FaceHeads) ? MP_TEXT : MP_HIGHLIGHT;
	lv_obj_set_style_bg_color(coinBody, fill, 0);

	// Hide the face letter while edge-on so the rotation reads
	// cleanly. On wide frames we re-show it.
	if(edgeOn) {
		lv_obj_add_flag(coinLabel, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_clear_flag(coinLabel, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text(coinLabel, (face == FaceHeads) ? "H" : "T");
		lv_obj_align(coinLabel, LV_ALIGN_CENTER, 0, 0);
	}

	// Shadow oval shrinks as the coin rises (further from the
	// ground = smaller shadow). At apex it's roughly half-width.
	const int distAboveBaseline = (kCoinBaselineY - kCoinH) - yPx;
	int shadowW = kShadowW - distAboveBaseline;          // clamp below.
	if(shadowW < kCoinEdgeMinW * 2) shadowW = kCoinEdgeMinW * 2;
	if(shadowW > kShadowW)          shadowW = kShadowW;
	lv_obj_set_size(shadow, shadowW, kShadowH);
	lv_obj_set_pos(shadow, kCoinCenterX - shadowW / 2, kShadowY);
}

void PhoneCoinFlip::renderRestingCoin() {
	if(coinBody == nullptr || coinLabel == nullptr || shadow == nullptr) return;

	lv_obj_set_size(coinBody, kCoinFullW, kCoinH);
	lv_obj_set_pos(coinBody, kCoinCenterX - kCoinFullW / 2,
	               kCoinBaselineY - kCoinH);

	lv_color_t fill = (settledFace == FaceHeads) ? MP_TEXT : MP_HIGHLIGHT;
	lv_obj_set_style_bg_color(coinBody, fill, 0);

	lv_obj_clear_flag(coinLabel, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(coinLabel, (settledFace == FaceHeads) ? "H" : "T");
	lv_obj_align(coinLabel, LV_ALIGN_CENTER, 0, 0);

	lv_obj_set_size(shadow, kShadowW, kShadowH);
	lv_obj_set_pos(shadow, kCoinCenterX - kShadowW / 2, kShadowY);
}

// ---------- toss + history --------------------------------------------

void PhoneCoinFlip::beginToss() {
	if(tossing) return;            // mashing 5 must not extend toss

	tossing       = true;
	tossFrameLeft = TossFrames;
	pendingFace   = ((rand() & 1) == 0) ? FaceHeads : FaceTails;

	// The result label switches to its tossing dim "..." state
	// immediately so the user sees the press registered.
	renderResult();

	// Paint the first toss frame (t = 0): coin at baseline, full
	// width, current settled face.
	renderTossFrame((uint8_t) settledFace,
	                kCoinBaselineY - kCoinH,
	                kCoinFullW,
	                false);

	startTossTimer();
}

void PhoneCoinFlip::clearHistory() {
	historyFill = 0;
	totalFlips  = 0;
	headsTotal  = 0;
	streak      = 0;
	prevValid   = false;
	for(uint8_t i = 0; i < HistorySize; ++i) history[i] = FaceHeads;
	renderHistory();
	renderStats();
	// renderResult() reflects the empty state's "PRESS FLIP" message
	// only when no flip is in progress -- otherwise leave the
	// in-flight "..." until the toss settles.
	if(!tossing) renderResult();
}

void PhoneCoinFlip::startTossTimer() {
	if(tossTimer != nullptr) return;
	tossTimer = lv_timer_create(&PhoneCoinFlip::onTossTickStatic,
	                            TossPeriodMs, this);
}

void PhoneCoinFlip::stopTossTimer() {
	if(tossTimer == nullptr) return;
	lv_timer_del(tossTimer);
	tossTimer = nullptr;
}

void PhoneCoinFlip::onTossTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneCoinFlip*>(timer->user_data);
	if(self == nullptr) return;

	// Frame count: tossFrameLeft starts at TossFrames and ticks
	// down. We define the *normalised* progress t in [0..1] from
	// the current frame index so the gravity arc reads consistently.
	if(self->tossFrameLeft == 0) {
		// Defensive: shouldn't happen because we stop the timer on
		// the settle frame, but guard so a stray tick can't fire.
		self->stopTossTimer();
		self->tossing = false;
		return;
	}

	const uint8_t frameIdx = (uint8_t)(TossFrames - self->tossFrameLeft + 1);

	// On the final frame we settle the coin and update history /
	// stats. We render with the canonical settled face, full width,
	// resting baseline.
	if(self->tossFrameLeft <= 1) {
		self->settledFace = (Face) self->pendingFace;

		// Push to history: rotate the ring, newest at index 0.
		uint8_t entries = self->historyFill;
		if(entries < HistorySize) entries++;
		for(int i = (int) entries - 1; i > 0; --i) {
			self->history[i] = self->history[i - 1];
		}
		self->history[0]  = self->settledFace;
		self->historyFill = entries;

		// Update lifetime stats. Streak resets to 1 if the new face
		// differs from the previous one (or this is the first flip
		// of the session).
		self->totalFlips = (uint16_t)(self->totalFlips + 1);
		if(self->settledFace == FaceHeads) {
			self->headsTotal = (uint16_t)(self->headsTotal + 1);
		}
		if(!self->prevValid || self->prevFace != self->settledFace) {
			self->streak = 1;
		} else {
			if(self->streak < 0xFF) self->streak++;
		}
		self->prevValid = true;
		self->prevFace  = self->settledFace;

		self->tossing       = false;
		self->tossFrameLeft = 0;
		self->stopTossTimer();

		self->renderRestingCoin();
		self->renderResult();
		self->renderHistory();
		self->renderStats();
		return;
	}

	// Mid-toss frame. Compute progress t in (0..1) and derive the
	// gravity arc + rotation from it.
	//
	//   y(t) = baseline - peak * 4*t*(1-t)        (parabolic apex at t=0.5)
	//   w(t) = full     * |cos(pi * Rotations * t)| clamped to [edgeMin, full]
	//
	// We use float math here -- the toss runs for at most 14 frames
	// per press and the ESP32 has hardware float, so the cost is
	// negligible compared to the LVGL repaint that follows.
	const float t = (float) frameIdx / (float) TossFrames;
	const float arc = 4.0f * t * (1.0f - t);                   // 0 .. 1
	const int   y   = (int)(kCoinBaselineY - kCoinH
	                        - (float) kPeakHeight * arc);

	const float angle  = (float) M_PI * (float) Rotations * t;
	float       cosAbs = fabsf(cosf(angle));
	int   widthPx     = (int)((float) kCoinFullW * cosAbs);
	const bool edgeOn = (widthPx < kFaceHideW);

	// While tumbling, alternate the displayed face based on the
	// rotation parity -- positive cosine = "front" face, negative =
	// "back". The pending settle face is treated as the "front" so
	// the user's eye threads the rotation onto the final outcome.
	const bool   showFront = (cosf(angle) >= 0.0f);
	const Face   frontFace = (Face) self->pendingFace;
	const Face   backFace  = (frontFace == FaceHeads) ? FaceTails : FaceHeads;
	const Face   displayed = showFront ? frontFace : backFace;

	self->renderTossFrame((uint8_t) displayed, y, widthPx, edgeOn);
	self->tossFrameLeft--;
}

// ---------- input -----------------------------------------------------

void PhoneCoinFlip::buttonPressed(uint i) {
	switch(i) {
		case BTN_5:
		case BTN_ENTER:
			if(softKeys) softKeys->flashLeft();
			beginToss();
			break;

		case BTN_L:
			if(softKeys) softKeys->flashLeft();
			beginToss();
			break;

		case BTN_R:
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_0:
			// Wipe history + stats. Allowed at any time; the
			// in-flight toss (if any) will land into a freshly
			// cleared history.
			clearHistory();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		// LEFT/RIGHT and the other digits are absorbed silently --
		// the coin toy has no "mode" the way PhoneDiceRoller does.
		case BTN_LEFT:
		case BTN_RIGHT:
		case BTN_1: case BTN_2: case BTN_3: case BTN_4:
		case BTN_6: case BTN_7: case BTN_8: case BTN_9:
		default:
			break;
	}
}

void PhoneCoinFlip::buttonReleased(uint i) {
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

void PhoneCoinFlip::buttonHeld(uint i) {
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
