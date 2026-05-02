#include "PhoneHelicopter.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside PhoneLunarLander (S91), PhoneWhackAMole (S90),
// PhoneReversi (S89), etc. without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneHelicopter::PhoneHelicopter()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildBorders();
	buildPillars();
	buildCopter();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("FLY");
	softKeys->setRight("BACK");

	// Park the chopper in the vertical middle of the playfield so the
	// idle preview reads like a "before launch" still rather than a
	// blank panel.
	copterY  = static_cast<int32_t>(FieldY + (FieldH - CopterH) / 2) * 16;
	copterVy = 0;
	renderCopter();

	// Idle: rules overlay visible, no pillars yet.
	enterIdle();
}

PhoneHelicopter::~PhoneHelicopter() {
	stopTickTimer();
	// All children parented to obj; LVScreen frees them recursively.
}

void PhoneHelicopter::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneHelicopter::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
	thrustHeld = false;
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneHelicopter::buildHud() {
	hudDistLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudDistLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudDistLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudDistLabel, "DIST 0000");
	lv_obj_set_pos(hudDistLabel, 4, StatusBarH + 1);

	hudHiLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudHiLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudHiLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudHiLabel, "HI 0");
	lv_obj_set_pos(hudHiLabel, 110, StatusBarH + 1);
}

void PhoneHelicopter::buildBorders() {
	ceilingObj = lv_obj_create(obj);
	lv_obj_remove_style_all(ceilingObj);
	lv_obj_set_size(ceilingObj, FieldW, CeilingH);
	lv_obj_set_pos(ceilingObj, FieldX, FieldY);
	lv_obj_set_style_bg_color(ceilingObj, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(ceilingObj, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(ceilingObj, 0, 0);
	lv_obj_set_style_radius(ceilingObj, 0, 0);
	lv_obj_clear_flag(ceilingObj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(ceilingObj, LV_OBJ_FLAG_CLICKABLE);

	floorObj = lv_obj_create(obj);
	lv_obj_remove_style_all(floorObj);
	lv_obj_set_size(floorObj, FieldW, FloorH);
	lv_obj_set_pos(floorObj, FieldX, FieldY + FieldH - FloorH);
	lv_obj_set_style_bg_color(floorObj, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(floorObj, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(floorObj, 0, 0);
	lv_obj_set_style_radius(floorObj, 0, 0);
	lv_obj_clear_flag(floorObj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(floorObj, LV_OBJ_FLAG_CLICKABLE);
}

void PhoneHelicopter::buildPillars() {
	for(uint8_t i = 0; i < kPillarCount; ++i) {
		// Top half (hangs from ceiling).
		pillarTop[i] = lv_obj_create(obj);
		lv_obj_remove_style_all(pillarTop[i]);
		lv_obj_set_size(pillarTop[i], PillarW, 4);
		lv_obj_set_pos(pillarTop[i], FieldW, FieldY + CeilingH);
		lv_obj_set_style_bg_color(pillarTop[i], MP_DIM, 0);
		lv_obj_set_style_bg_opa(pillarTop[i], LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(pillarTop[i], MP_HIGHLIGHT, 0);
		lv_obj_set_style_border_width(pillarTop[i], 1, 0);
		lv_obj_set_style_radius(pillarTop[i], 0, 0);
		lv_obj_clear_flag(pillarTop[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(pillarTop[i], LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(pillarTop[i], LV_OBJ_FLAG_HIDDEN);

		// Bottom half (rises from floor).
		pillarBottom[i] = lv_obj_create(obj);
		lv_obj_remove_style_all(pillarBottom[i]);
		lv_obj_set_size(pillarBottom[i], PillarW, 4);
		lv_obj_set_pos(pillarBottom[i], FieldW, FieldY + FieldH - FloorH - 4);
		lv_obj_set_style_bg_color(pillarBottom[i], MP_DIM, 0);
		lv_obj_set_style_bg_opa(pillarBottom[i], LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(pillarBottom[i], MP_ACCENT, 0);
		lv_obj_set_style_border_width(pillarBottom[i], 1, 0);
		lv_obj_set_style_radius(pillarBottom[i], 0, 0);
		lv_obj_clear_flag(pillarBottom[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(pillarBottom[i], LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(pillarBottom[i], LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneHelicopter::buildCopter() {
	copterRoot = lv_obj_create(obj);
	lv_obj_remove_style_all(copterRoot);
	lv_obj_set_size(copterRoot, CopterW, CopterH + 2);
	lv_obj_set_pos(copterRoot, CopterX, FieldY + (FieldH - CopterH) / 2);
	lv_obj_set_style_pad_all(copterRoot, 0, 0);
	lv_obj_set_style_bg_opa(copterRoot, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(copterRoot, 0, 0);
	lv_obj_clear_flag(copterRoot, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(copterRoot, LV_OBJ_FLAG_CLICKABLE);

	// Body: cream rounded rectangle. Length 7 px, height 4 px,
	// positioned so the rotor/skid extras still fit within the
	// 12x8 bounding box.
	copterBody = lv_obj_create(copterRoot);
	lv_obj_remove_style_all(copterBody);
	lv_obj_set_size(copterBody, 7, 4);
	lv_obj_set_pos(copterBody, 0, 1);
	lv_obj_set_style_bg_color(copterBody, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(copterBody, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(copterBody, MP_ACCENT, 0);
	lv_obj_set_style_border_width(copterBody, 1, 0);
	lv_obj_set_style_radius(copterBody, 2, 0);
	lv_obj_clear_flag(copterBody, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(copterBody, LV_OBJ_FLAG_CLICKABLE);

	// Cockpit dome: cyan square at the front of the body so the
	// silhouette reads as a manned cockpit rather than a brick.
	copterDome = lv_obj_create(copterRoot);
	lv_obj_remove_style_all(copterDome);
	lv_obj_set_size(copterDome, 2, 2);
	lv_obj_set_pos(copterDome, 1, 2);
	lv_obj_set_style_bg_color(copterDome, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(copterDome, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(copterDome, 0, 0);
	lv_obj_set_style_radius(copterDome, 1, 0);
	lv_obj_clear_flag(copterDome, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(copterDome, LV_OBJ_FLAG_CLICKABLE);

	// Tail boom: a thin cream bar reaching to the right.
	copterTail = lv_obj_create(copterRoot);
	lv_obj_remove_style_all(copterTail);
	lv_obj_set_size(copterTail, 4, 1);
	lv_obj_set_pos(copterTail, 7, 3);
	lv_obj_set_style_bg_color(copterTail, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(copterTail, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(copterTail, 0, 0);
	lv_obj_set_style_radius(copterTail, 0, 0);
	lv_obj_clear_flag(copterTail, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(copterTail, LV_OBJ_FLAG_CLICKABLE);

	// Tail rotor: tiny accent rectangle at the very tip of the tail.
	copterTailRotor = lv_obj_create(copterRoot);
	lv_obj_remove_style_all(copterTailRotor);
	lv_obj_set_size(copterTailRotor, 1, 3);
	lv_obj_set_pos(copterTailRotor, 11, 2);
	lv_obj_set_style_bg_color(copterTailRotor, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(copterTailRotor, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(copterTailRotor, 0, 0);
	lv_obj_set_style_radius(copterTailRotor, 0, 0);
	lv_obj_clear_flag(copterTailRotor, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(copterTailRotor, LV_OBJ_FLAG_CLICKABLE);

	// Main rotor: a horizontal bar on top of the body. We animate it
	// per-tick (alternating wide/narrow) to suggest spinning blades.
	copterRotor = lv_obj_create(copterRoot);
	lv_obj_remove_style_all(copterRotor);
	lv_obj_set_size(copterRotor, 9, 1);
	lv_obj_set_pos(copterRotor, -1, 0);
	lv_obj_set_style_bg_color(copterRotor, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(copterRotor, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(copterRotor, 0, 0);
	lv_obj_set_style_radius(copterRotor, 0, 0);
	lv_obj_clear_flag(copterRotor, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(copterRotor, LV_OBJ_FLAG_CLICKABLE);

	// Skids: a thin orange bar across the bottom of the body.
	copterSkids = lv_obj_create(copterRoot);
	lv_obj_remove_style_all(copterSkids);
	lv_obj_set_size(copterSkids, 7, 1);
	lv_obj_set_pos(copterSkids, 0, 5);
	lv_obj_set_style_bg_color(copterSkids, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(copterSkids, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(copterSkids, 0, 0);
	lv_obj_set_style_radius(copterSkids, 0, 0);
	lv_obj_clear_flag(copterSkids, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(copterSkids, LV_OBJ_FLAG_CLICKABLE);
}

void PhoneHelicopter::buildOverlay() {
	overlayPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(overlayPanel);
	lv_obj_set_size(overlayPanel, 124, 60);
	lv_obj_set_align(overlayPanel, LV_ALIGN_CENTER);
	lv_obj_set_y(overlayPanel, -2);
	lv_obj_set_style_bg_color(overlayPanel, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(overlayPanel, LV_OPA_90, 0);
	lv_obj_set_style_border_color(overlayPanel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(overlayPanel, 1, 0);
	lv_obj_set_style_radius(overlayPanel, 3, 0);
	lv_obj_set_style_pad_all(overlayPanel, 4, 0);
	lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_CLICKABLE);

	overlayTitle = lv_label_create(overlayPanel);
	lv_obj_set_style_text_font(overlayTitle, &pixelbasic16, 0);
	lv_obj_set_style_text_color(overlayTitle, MP_ACCENT, 0);
	lv_obj_set_style_text_align(overlayTitle, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayTitle, "COPTER");
	lv_obj_set_align(overlayTitle, LV_ALIGN_TOP_MID);
	lv_obj_set_y(overlayTitle, -1);

	overlayLines = lv_label_create(overlayPanel);
	lv_obj_set_style_text_font(overlayLines, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLines, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLines,
	                  "HOLD A TO CLIMB\nDODGE THE PILLARS");
	lv_obj_set_align(overlayLines, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(overlayLines, -1);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneHelicopter::enterIdle() {
	stopTickTimer();
	state = GameState::Idle;
	thrustHeld = false;
	rotorPhase = 0;

	// Hide pillars while idle so the rules overlay reads cleanly.
	for(uint8_t i = 0; i < kPillarCount; ++i) {
		pillarActive[i] = false;
		if(pillarTop[i])    lv_obj_add_flag(pillarTop[i],    LV_OBJ_FLAG_HIDDEN);
		if(pillarBottom[i]) lv_obj_add_flag(pillarBottom[i], LV_OBJ_FLAG_HIDDEN);
	}

	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshRotor();
}

void PhoneHelicopter::startRound() {
	stopTickTimer();
	state = GameState::Playing;
	thrustHeld = false;
	rotorPhase = 0;
	distance   = 0;
	tickCount  = 0;
	scrollSpeed = kScrollMin;
	++attempt;

	// Re-seed the LCG per round (deterministic on attempt count) so
	// successive rounds vary while staying reproducible per
	// screen-life. Mixing in a couple of constants keeps the first
	// few rounds visually distinct.
	rngState = 0x1234ABCDu ^ (static_cast<uint32_t>(attempt) * 0x9E3779B1u);

	// Seed chopper at the centre of the playfield, momentarily still.
	copterY  = static_cast<int32_t>(FieldY + (FieldH - CopterH) / 2) * 16;
	copterVy = 0;

	// Lay out an initial pattern of pillars stretching off to the
	// right of the playfield so the first one walks in over a few
	// ticks rather than appearing instantly.
	resetPillars();

	renderCopter();
	renderPillars();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshRotor();
	startTickTimer();
}

void PhoneHelicopter::crashRound() {
	stopTickTimer();
	state = GameState::Crashed;
	thrustHeld = false;

	if(distance > highScore) highScore = distance;

	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshRotor();
}

// ===========================================================================
// game loop
// ===========================================================================

void PhoneHelicopter::tick() {
	if(state != GameState::Playing) return;

	++tickCount;

	// 1. Ramp scroll speed every kScrollRamp ticks (capped).
	if(tickCount > 0 && (tickCount % kScrollRamp) == 0) {
		if(scrollSpeed < kScrollMax) ++scrollSpeed;
	}

	// 2. Apply thrust / gravity to chopper velocity.
	if(thrustHeld) {
		copterVy -= kThrust;
	} else {
		copterVy += kGravity;
	}
	if(copterVy >  kVMax) copterVy =  kVMax;
	if(copterVy < -kVMax) copterVy = -kVMax;

	// 3. Integrate.
	copterY += copterVy;

	// 4. Hard wall: ceiling/floor crash on overlap.
	const int16_t pxY = static_cast<int16_t>(copterY / 16);
	if(pxY <= FieldY + CeilingH - 1) {
		copterY = static_cast<int32_t>(FieldY + CeilingH) * 16;
		crashRound();
		renderCopter();
		return;
	}
	if(pxY + CopterH >= FieldY + FieldH - FloorH + 1) {
		copterY = static_cast<int32_t>(FieldY + FieldH - FloorH - CopterH) * 16;
		crashRound();
		renderCopter();
		return;
	}

	// 5. Scroll pillars left.
	for(uint8_t i = 0; i < kPillarCount; ++i) {
		if(!pillarActive[i]) continue;
		pillarX[i] = static_cast<int16_t>(pillarX[i] - scrollSpeed);
		if(pillarX[i] + PillarW < FieldX) {
			// Off-screen; recycle to the right of the rightmost pillar.
			int16_t maxRight = FieldX + FieldW;
			for(uint8_t j = 0; j < kPillarCount; ++j) {
				if(pillarActive[j] && pillarX[j] > maxRight) maxRight = pillarX[j];
			}
			recyclePillarTo(i, static_cast<int16_t>(maxRight + PillarSpacing));
		}
	}

	// 6. Collision with pillars.
	if(checkCollision()) {
		crashRound();
		renderCopter();
		renderPillars();
		return;
	}

	// 7. Score.
	const uint32_t newDist =
		static_cast<uint32_t>(distance) + static_cast<uint32_t>(scrollSpeed);
	distance = static_cast<uint16_t>(newDist > kDistMax ? kDistMax : newDist);

	// 8. Rotor animation phase (advance every other tick so it doesn't
	//    flicker too aggressively).
	if((tickCount & 1u) == 0u) {
		rotorPhase = static_cast<uint8_t>((rotorPhase + 1) & 0x03u);
	}

	// 9. Render.
	renderCopter();
	renderPillars();
	refreshHud();
	refreshRotor();
}

// ===========================================================================
// pillar helpers
// ===========================================================================

uint16_t PhoneHelicopter::lcg() {
	// Numerical Recipes-style LCG. Output range is the upper 16 bits.
	rngState = rngState * 1664525u + 1013904223u;
	return static_cast<uint16_t>((rngState >> 16) & 0xFFFFu);
}

int16_t PhoneHelicopter::nextGapY(int16_t gapH) {
	// Available y-range is [FieldY+CeilingH+PillarMargin,
	//                       FieldY+FieldH-FloorH-PillarMargin-gapH].
	const int16_t loY = static_cast<int16_t>(FieldY + CeilingH + PillarMargin);
	const int16_t hiY = static_cast<int16_t>(FieldY + FieldH - FloorH - PillarMargin - gapH);
	if(hiY <= loY) return loY;
	const uint16_t span = static_cast<uint16_t>(hiY - loY);
	const uint16_t r    = lcg();
	return static_cast<int16_t>(loY + (r % (span + 1u)));
}

void PhoneHelicopter::recyclePillarTo(uint8_t idx, int16_t newX) {
	// Gap height shrinks as the round progresses (capped at the min).
	int16_t gapH = static_cast<int16_t>(PillarGapMax - (tickCount / 200));
	if(gapH < PillarGapMin) gapH = PillarGapMin;

	pillarX[idx]      = newX;
	pillarGapH[idx]   = gapH;
	pillarGapY[idx]   = nextGapY(gapH);
	pillarActive[idx] = true;
}

void PhoneHelicopter::resetPillars() {
	// Lay pillars out starting just past the right edge of the field
	// so the first one slides in naturally.
	const int16_t baseX = FieldX + FieldW + 4;
	for(uint8_t i = 0; i < kPillarCount; ++i) {
		recyclePillarTo(i, static_cast<int16_t>(baseX + i * PillarSpacing));
	}
	renderPillars();
}

// ===========================================================================
// collision
// ===========================================================================

bool PhoneHelicopter::checkCollision() {
	const int16_t cpxY = static_cast<int16_t>(copterY / 16);
	const int16_t cx0  = CopterX;
	const int16_t cx1  = static_cast<int16_t>(CopterX + CopterW - 1);
	const int16_t cy0  = cpxY;
	const int16_t cy1  = static_cast<int16_t>(cpxY + CopterH - 1);

	for(uint8_t i = 0; i < kPillarCount; ++i) {
		if(!pillarActive[i]) continue;
		const int16_t px0 = pillarX[i];
		const int16_t px1 = static_cast<int16_t>(pillarX[i] + PillarW - 1);
		// Bounding-box overlap on x?
		if(cx1 < px0 || cx0 > px1) continue;

		// Overlap on x; check if chopper's y-range exits the gap.
		const int16_t gy0 = pillarGapY[i];
		const int16_t gy1 = static_cast<int16_t>(pillarGapY[i] + pillarGapH[i] - 1);
		if(cy0 < gy0 || cy1 > gy1) {
			return true;
		}
	}
	return false;
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneHelicopter::renderCopter() {
	if(copterRoot == nullptr) return;
	const int16_t pxY = static_cast<int16_t>(copterY / 16);
	lv_obj_set_pos(copterRoot, CopterX, pxY);
}

void PhoneHelicopter::renderPillars() {
	for(uint8_t i = 0; i < kPillarCount; ++i) {
		if(!pillarActive[i]) {
			if(pillarTop[i])    lv_obj_add_flag(pillarTop[i],    LV_OBJ_FLAG_HIDDEN);
			if(pillarBottom[i]) lv_obj_add_flag(pillarBottom[i], LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		// Top piece: from ceiling-bottom down to gap top.
		const int16_t topY    = static_cast<int16_t>(FieldY + CeilingH);
		const int16_t topH    = static_cast<int16_t>(pillarGapY[i] - topY);
		// Bottom piece: from gap-bottom down to floor top.
		const int16_t botY    = static_cast<int16_t>(pillarGapY[i] + pillarGapH[i]);
		const int16_t botH    = static_cast<int16_t>(
			(FieldY + FieldH - FloorH) - botY);

		if(pillarTop[i] != nullptr) {
			if(topH > 0) {
				lv_obj_set_size(pillarTop[i], PillarW, topH);
				lv_obj_set_pos(pillarTop[i], pillarX[i], topY);
				lv_obj_clear_flag(pillarTop[i], LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_obj_add_flag(pillarTop[i], LV_OBJ_FLAG_HIDDEN);
			}
		}
		if(pillarBottom[i] != nullptr) {
			if(botH > 0) {
				lv_obj_set_size(pillarBottom[i], PillarW, botH);
				lv_obj_set_pos(pillarBottom[i], pillarX[i], botY);
				lv_obj_clear_flag(pillarBottom[i], LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_obj_add_flag(pillarBottom[i], LV_OBJ_FLAG_HIDDEN);
			}
		}
	}
}

void PhoneHelicopter::refreshHud() {
	if(hudDistLabel != nullptr) {
		char buf[16];
		snprintf(buf, sizeof(buf), "DIST %04u",
		         static_cast<unsigned>(distance > kDistMax ? kDistMax : distance));
		lv_label_set_text(hudDistLabel, buf);
	}
	if(hudHiLabel != nullptr) {
		char buf[16];
		snprintf(buf, sizeof(buf), "HI %u",
		         static_cast<unsigned>(highScore > kDistMax ? kDistMax : highScore));
		lv_label_set_text(hudHiLabel, buf);
	}
}

void PhoneHelicopter::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("FLY");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			softKeys->setLeft("THRUST");
			softKeys->setRight("BACK");
			break;
		case GameState::Crashed:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneHelicopter::refreshOverlay() {
	if(overlayPanel == nullptr) return;
	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayTitle, "COPTER");
			lv_obj_set_style_text_color(overlayTitle, MP_ACCENT, 0);
			lv_label_set_text(overlayLines,
			                  "HOLD A TO CLIMB\nDODGE THE PILLARS");
			lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayPanel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayPanel);
			break;

		case GameState::Playing:
			lv_obj_add_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			break;

		case GameState::Crashed: {
			char body[64];
			snprintf(body, sizeof(body),
			         "DOWN AT %u\nHI %u\nA TO TRY AGAIN",
			         static_cast<unsigned>(distance),
			         static_cast<unsigned>(highScore));
			lv_label_set_text(overlayTitle, "CRASHED");
			lv_obj_set_style_text_color(overlayTitle, MP_ACCENT, 0);
			lv_label_set_text(overlayLines, body);
			lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayPanel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayPanel);
			break;
		}
	}
}

void PhoneHelicopter::refreshRotor() {
	if(copterRotor == nullptr) return;
	if(state != GameState::Playing) {
		// Static idle rotor.
		lv_obj_set_size(copterRotor, 9, 1);
		lv_obj_set_pos(copterRotor, -1, 0);
		lv_obj_clear_flag(copterRotor, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// Spinning rotor: alternates between long-thin and short-tall to
	// suggest motion blur on a 1-px-tall budget.
	switch(rotorPhase & 0x03u) {
		case 0:
			lv_obj_set_size(copterRotor, 9, 1);
			lv_obj_set_pos(copterRotor, -1, 0);
			break;
		case 1:
			lv_obj_set_size(copterRotor, 5, 1);
			lv_obj_set_pos(copterRotor, 1, 0);
			break;
		case 2:
			lv_obj_set_size(copterRotor, 9, 1);
			lv_obj_set_pos(copterRotor, -1, 0);
			break;
		case 3:
			lv_obj_set_size(copterRotor, 7, 1);
			lv_obj_set_pos(copterRotor, 0, 0);
			break;
	}
	lv_obj_clear_flag(copterRotor, LV_OBJ_FLAG_HIDDEN);
}

// ===========================================================================
// timer helpers
// ===========================================================================

void PhoneHelicopter::startTickTimer() {
	stopTickTimer();
	tickTimer = lv_timer_create(&PhoneHelicopter::onTickStatic,
	                            kTickMs, this);
}

void PhoneHelicopter::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneHelicopter::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneHelicopter*>(timer->user_data);
	if(self == nullptr) return;
	self->tick();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneHelicopter::buttonPressed(uint i) {
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}
	if(i == BTN_R) {
		startRound();
		return;
	}

	switch(state) {
		case GameState::Idle:
			if(i == BTN_2 || i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startRound();
			}
			return;

		case GameState::Playing:
			if(i == BTN_2 || i == BTN_5 || i == BTN_ENTER) {
				thrustHeld = true;
				if(softKeys) softKeys->flashLeft();
			}
			return;

		case GameState::Crashed:
			if(i == BTN_2 || i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startRound();
			}
			return;
	}
}

void PhoneHelicopter::buttonReleased(uint i) {
	if(state != GameState::Playing) return;
	if(i == BTN_2 || i == BTN_5 || i == BTN_ENTER) {
		thrustHeld = false;
	}
}
