#include "PhoneDiceRoller.h"

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
// widget so the dice roller slots in beside PhoneCalculator (S60),
// PhoneAlarmClock (S124), PhoneTimers (S125), PhoneCurrencyConverter
// (S126), PhoneUnitConverter (S127), PhoneWorldClock (S128),
// PhoneVirtualPet (S129) and PhoneMagic8Ball (S130) without a
// visual seam. Same inline-#define convention every other Phone*
// screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption / crit-high
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange / crit-low
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple

// =====================================================================
// S131 — PhoneDiceRoller — face-count table
//
// Six standard polyhedral faces. Order matters: cycleFace() walks
// this table linearly, so reordering changes which die "follows"
// which when the user presses LEFT / RIGHT. The current order
// matches a tabletop dice tray's left-to-right sort.
// =====================================================================

static const uint8_t kFaces[PhoneDiceRoller::FaceCount] = {
		4, 6, 8, 10, 12, 20,
};

// ---------- geometry --------------------------------------------------
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar
//   y=12..20   "DICE ROLLER" caption (pixelbasic7, cyan)
//   y=24..40   mode selector "<  2d20  >" (pixelbasic16)
//   y=44..86   result tray (rounded rect, 100x42 centred on x=30..130)
//                inside: total (pixelbasic16) + breakdown (pixelbasic7)
//   y=92..104  hint text (pixelbasic7, dim purple)
//   y=118..128 PhoneSoftKeyBar
//
// All coordinates centralised here so a future skin tweak only
// edits this block.

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kModeY         = 24;
static constexpr lv_coord_t kTrayX         = 30;
static constexpr lv_coord_t kTrayY         = 44;
static constexpr lv_coord_t kTrayW         = 100;
static constexpr lv_coord_t kTrayH         = 42;
static constexpr lv_coord_t kHintY         = 96;

// Mode chevrons sit just outside the mode label.
static constexpr lv_coord_t kChevLeftX     = 32;
static constexpr lv_coord_t kChevRightX    = 122;
static constexpr lv_coord_t kChevY         = 26;

// ---------- public statics --------------------------------------------

uint8_t PhoneDiceRoller::faceAt(uint8_t idx) {
	if(idx >= FaceCount) return 0;
	return kFaces[idx];
}

// ---------- ctor / dtor -----------------------------------------------

PhoneDiceRoller::PhoneDiceRoller()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  hintLabel(nullptr),
		  modeLabel(nullptr),
		  leftChev(nullptr),
		  rightChev(nullptr),
		  tray(nullptr),
		  totalLabel(nullptr),
		  breakdownLabel(nullptr) {

	for(uint8_t i = 0; i < DiceMax; ++i) rollValues[i] = 0;

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
	lv_label_set_text(captionLabel, "DICE ROLLER");

	buildHud();

	// Seed rand() with a value distinct from PhoneMagic8Ball's seed so
	// pushing one screen straight after the other doesn't collapse on
	// to the same RNG sequence -- same magic-XOR trick PhoneSimon and
	// PhoneMagic8Ball use.
	srand(static_cast<unsigned>(millis() ^ 0xD1CE12));

	// Bottom soft-key bar; populated here once because the screen has
	// no mode-dependent label changes (ROLL/BACK are always valid).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("ROLL");
	softKeys->setRight("BACK");

	// Initial paint: idle mode label + empty tray.
	renderMode();
	renderResult(false);
}

PhoneDiceRoller::~PhoneDiceRoller() {
	stopTumbleTimer();
}

// ---------- build helpers ---------------------------------------------

void PhoneDiceRoller::buildHud() {
	// Mode selector label, centred horizontally.
	modeLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(modeLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(modeLabel, MP_TEXT, 0);
	lv_obj_set_width(modeLabel, 160);
	lv_obj_set_pos(modeLabel, 0, kModeY);
	lv_obj_set_style_text_align(modeLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(modeLabel, "1d20");

	// Side chevrons -- pixelbasic7 "<" and ">" framing the mode
	// label. They never disable today (the cycle wraps) but the
	// objects are kept so a future modal lock can ghost them.
	leftChev = lv_label_create(obj);
	lv_obj_set_style_text_font(leftChev, &pixelbasic7, 0);
	lv_obj_set_style_text_color(leftChev, MP_LABEL_DIM, 0);
	lv_obj_set_pos(leftChev, kChevLeftX, kChevY);
	lv_label_set_text(leftChev, "<");

	rightChev = lv_label_create(obj);
	lv_obj_set_style_text_font(rightChev, &pixelbasic7, 0);
	lv_obj_set_style_text_color(rightChev, MP_LABEL_DIM, 0);
	lv_obj_set_pos(rightChev, kChevRightX, kChevY);
	lv_label_set_text(rightChev, ">");

	// Result tray -- a rounded rectangle with a thin cyan border,
	// styled to match PhoneCalculator's display panel so the apps
	// share a visual idiom.
	tray = lv_obj_create(obj);
	lv_obj_remove_style_all(tray);
	lv_obj_set_size(tray, kTrayW, kTrayH);
	lv_obj_set_pos(tray, kTrayX, kTrayY);
	lv_obj_set_style_radius(tray, 6, 0);
	lv_obj_set_style_bg_opa(tray, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(tray, MP_BG_DARK, 0);
	lv_obj_set_style_border_width(tray, 1, 0);
	lv_obj_set_style_border_color(tray, MP_HIGHLIGHT, 0);
	lv_obj_set_style_pad_all(tray, 2, 0);
	lv_obj_clear_flag(tray, LV_OBJ_FLAG_SCROLLABLE);

	// Big total in the top half of the tray.
	totalLabel = lv_label_create(tray);
	lv_obj_set_style_text_font(totalLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(totalLabel, MP_TEXT, 0);
	lv_obj_set_width(totalLabel, kTrayW - 6);
	lv_obj_align(totalLabel, LV_ALIGN_TOP_MID, 0, 2);
	lv_obj_set_style_text_align(totalLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(totalLabel, "?");

	// Per-die breakdown in the bottom half.
	breakdownLabel = lv_label_create(tray);
	lv_obj_set_style_text_font(breakdownLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(breakdownLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(breakdownLabel, kTrayW - 6);
	lv_obj_align(breakdownLabel, LV_ALIGN_BOTTOM_MID, 0, -2);
	lv_obj_set_style_text_align(breakdownLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(breakdownLabel, "PRESS ROLL");

	// Bottom hint. Compact reminder of the keypad shortcuts. The
	// soft-key bar already covers ROLL/BACK so the hint focuses on
	// the less obvious mode controls.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(hintLabel, 160);
	lv_obj_set_pos(hintLabel, 0, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "<>:TYPE  0:#DICE  5:ROLL");
}

// ---------- lifecycle -------------------------------------------------

void PhoneDiceRoller::onStart() {
	Input::getInstance()->addListener(this);
	// Make sure we always re-enter as the calm idle face -- a previous
	// push/pop cycle could in theory have left the screen mid-tumble.
	renderMode();
	renderResult(false);
}

void PhoneDiceRoller::onStop() {
	Input::getInstance()->removeListener(this);
	stopTumbleTimer();
	rolling = false;
}

// ---------- render ----------------------------------------------------

void PhoneDiceRoller::renderMode() {
	if(modeLabel == nullptr) return;
	char buf[12];
	snprintf(buf, sizeof(buf), "%ud%u",
	         (unsigned) diceCount,
	         (unsigned) currentFace());
	lv_label_set_text(modeLabel, buf);
}

void PhoneDiceRoller::renderResult(bool tumbling) {
	if(totalLabel == nullptr || breakdownLabel == nullptr) return;

	if(!hasRolled) {
		// First-paint state: tray reads "?" / "PRESS ROLL".
		lv_obj_set_style_text_color(totalLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(totalLabel, "?");
		lv_obj_set_style_text_color(breakdownLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(breakdownLabel, "PRESS ROLL");
		return;
	}

	// Total -- coloured by how the player did. During tumble we paint
	// in dim purple to read as a "blur"; on settle we pick a tone.
	char totalBuf[8];
	snprintf(totalBuf, sizeof(totalBuf), "%u", (unsigned) rollTotal);
	lv_label_set_text(totalLabel, totalBuf);

	lv_color_t totalCol;
	if(tumbling) {
		totalCol = MP_LABEL_DIM;
	} else {
		const uint8_t face = currentFace();
		const uint16_t maxTotal = (uint16_t)face * (uint16_t)diceCount;
		const uint16_t minTotal = diceCount;
		if(rollTotal == maxTotal && face > 0) {
			totalCol = MP_HIGHLIGHT; // critical high
		} else if(rollTotal == minTotal) {
			totalCol = MP_ACCENT;    // critical low
		} else {
			totalCol = MP_TEXT;
		}
	}
	lv_obj_set_style_text_color(totalLabel, totalCol, 0);

	// Breakdown line -- "12 + 25" for 2 dice, "12" for 1 die. Dim
	// during tumble; settled state picks dim cream so the row reads
	// secondary to the big total.
	char breakBuf[16];
	if(diceCount >= 2) {
		snprintf(breakBuf, sizeof(breakBuf), "%u + %u",
		         (unsigned) rollValues[0],
		         (unsigned) rollValues[1]);
	} else {
		snprintf(breakBuf, sizeof(breakBuf), "%u",
		         (unsigned) rollValues[0]);
	}
	lv_label_set_text(breakdownLabel, breakBuf);
	lv_obj_set_style_text_color(breakdownLabel,
	                            tumbling ? MP_DIM : MP_LABEL_DIM, 0);
}

// ---------- mode controls ---------------------------------------------

void PhoneDiceRoller::cycleFace(int8_t delta) {
	int next = (int) faceIdx + (int) delta;
	while(next < 0)               next += FaceCount;
	while(next >= (int) FaceCount) next -= FaceCount;
	faceIdx = (uint8_t) next;
	renderMode();
}

void PhoneDiceRoller::toggleCount() {
	diceCount = (diceCount == 1) ? 2 : 1;
	renderMode();
	// If the user has rolled before, repaint the breakdown so the
	// count toggle visibly affects the on-screen layout (1d shows
	// one number, 2d shows "a + b") even before the next roll.
	renderResult(false);
}

void PhoneDiceRoller::jumpToFace(uint8_t idx) {
	if(idx >= FaceCount) return;
	faceIdx = idx;
	renderMode();
}

// ---------- roll animation --------------------------------------------

void PhoneDiceRoller::beginRoll() {
	if(rolling) return;            // mashing 5 must not extend tumble
	const uint8_t face = currentFace();
	if(face == 0) return;          // belt-and-braces guard

	rolling          = true;
	tumbleFrameLeft  = TumbleFrames;
	hasRolled        = true;

	// Pick the first decoy frame and paint it immediately so the user
	// sees instant feedback for the press. The intermediate timer
	// frames stream more decoys; the final settle frame in
	// onTumbleTickStatic picks the canonical outcome. This matches
	// PhoneMagic8Ball's pattern -- the tumble is purely cosmetic and
	// the displayed outcome is whatever the last frame produces.
	uint16_t decoyTotal = 0;
	for(uint8_t i = 0; i < diceCount; ++i) {
		rollValues[i] = (uint8_t)((rand() % face) + 1);
		decoyTotal    = (uint16_t)(decoyTotal + rollValues[i]);
	}
	for(uint8_t i = diceCount; i < DiceMax; ++i) {
		rollValues[i] = 0;
	}
	rollTotal = decoyTotal;
	renderResult(true);

	startTumbleTimer();
}

void PhoneDiceRoller::startTumbleTimer() {
	if(tumbleTimer != nullptr) return;
	tumbleTimer = lv_timer_create(&PhoneDiceRoller::onTumbleTickStatic,
	                              TumblePeriodMs, this);
}

void PhoneDiceRoller::stopTumbleTimer() {
	if(tumbleTimer == nullptr) return;
	lv_timer_del(tumbleTimer);
	tumbleTimer = nullptr;
}

void PhoneDiceRoller::onTumbleTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneDiceRoller*>(timer->user_data);
	if(self == nullptr) return;

	if(self->tumbleFrameLeft <= 1) {
		// Final frame: settle on the real values and paint with tone
		// colouring. We re-roll one last time to produce the canonical
		// final outcome so the user always sees a fresh result on the
		// settle frame (the decoy stream is meant to read as tumble,
		// not as a sequence of "real" rolls).
		const uint8_t face = self->currentFace();
		self->rollTotal = 0;
		for(uint8_t i = 0; i < self->diceCount; ++i) {
			self->rollValues[i] = (uint8_t)((rand() % face) + 1);
			self->rollTotal = (uint16_t)(self->rollTotal + self->rollValues[i]);
		}
		for(uint8_t i = self->diceCount; i < DiceMax; ++i) {
			self->rollValues[i] = 0;
		}
		self->renderResult(false);
		self->tumbleFrameLeft = 0;
		self->rolling = false;
		self->stopTumbleTimer();
		return;
	}

	// Mid-tumble frame: pick fresh decoy values and repaint dim.
	const uint8_t face = self->currentFace();
	if(face == 0) {
		self->stopTumbleTimer();
		self->rolling = false;
		return;
	}
	uint16_t decoyTotal = 0;
	for(uint8_t i = 0; i < self->diceCount; ++i) {
		self->rollValues[i] = (uint8_t)((rand() % face) + 1);
		decoyTotal = (uint16_t)(decoyTotal + self->rollValues[i]);
	}
	for(uint8_t i = self->diceCount; i < DiceMax; ++i) {
		self->rollValues[i] = 0;
	}
	self->rollTotal = decoyTotal;
	self->renderResult(true);
	self->tumbleFrameLeft--;
}

// ---------- input -----------------------------------------------------

void PhoneDiceRoller::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
			cycleFace(-1);
			break;
		case BTN_RIGHT:
			cycleFace(+1);
			break;

		case BTN_0:
			toggleCount();
			break;

		// Quick-jump to face. BTN_5 is reserved for ROLL (it's the
		// universal "do the thing" key in the toy family). The other
		// digits map onto the 6-element kFaces table:
		case BTN_1: jumpToFace(0); break; // d4
		case BTN_2: jumpToFace(1); break; // d6
		case BTN_3: jumpToFace(2); break; // d8
		case BTN_4: jumpToFace(3); break; // d10
		case BTN_6: jumpToFace(5); break; // d20
		case BTN_7: jumpToFace(4); break; // d12 (BTN_5 is taken)

		case BTN_5:
		case BTN_ENTER:
			if(softKeys) softKeys->flashLeft();
			beginRoll();
			break;

		case BTN_8:
		case BTN_9:
			// No mapping today; reserved for future "history" / "lock"
			// features. Absorbed so they don't fall through.
			break;

		case BTN_L:
			if(softKeys) softKeys->flashLeft();
			beginRoll();
			break;

		case BTN_R:
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

void PhoneDiceRoller::buttonReleased(uint i) {
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

void PhoneDiceRoller::buttonHeld(uint i) {
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
