#include "PhoneLunarLander.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside PhoneTetris (S71/72), PhoneTicTacToe (S81),
// PhoneMemoryMatch (S82), PhoneReversi (S89), PhoneWhackAMole (S90)
// without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions
#define MP_FLAME       lv_color_make(255, 200, 80)   // warm flame yellow

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneLunarLander::PhoneLunarLander()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < kTerrainPoints; ++i) {
		terrainPts[i].x = 0;
		terrainPts[i].y = 0;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildTerrain();
	buildShip();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("LAUNCH");
	softKeys->setRight("BACK");

	// Pick the first round's pad layout once on construction so the
	// idle-state preview shows where the player will be aiming.
	regenerateTerrain();
	refreshPadMarker();

	// Park the ship at its launch position so the Idle preview reads
	// like a real "before launch" still rather than a blank panel.
	shipX = (FieldX + FieldW / 2 - ShipW / 2) * 16;
	shipY = (FieldY + 4) * 16;
	renderShip();

	// Start in Idle: rules overlay visible, no ticking yet.
	enterIdle();
}

PhoneLunarLander::~PhoneLunarLander() {
	stopTickTimer();
	// All children are parented to obj; LVScreen frees them recursively.
}

void PhoneLunarLander::onStart() {
	Input::getInstance()->addListener(this);
	// Tick timer is started in startRound(), not here -- Idle doesn't
	// need to spend cycles updating physics on a stationary ship.
}

void PhoneLunarLander::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
	// Clear input latches so a held key doesn't bleed into the next
	// time the screen is pushed.
	thrustUpHeld = thrustLeftHeld = thrustRightHeld = false;
	thrustUpFlash = thrustSideFlash = 0;
	sideFlashDir = 0;
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneLunarLander::buildHud() {
	// Fuel caption (cyan "FUEL").
	hudFuelLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudFuelLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudFuelLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudFuelLabel, "FUEL");
	lv_obj_set_pos(hudFuelLabel, 2, HudY + 2);

	// Fuel bar background (1 px outline, 32 px wide, 6 px tall).
	hudFuelBar = lv_obj_create(obj);
	lv_obj_remove_style_all(hudFuelBar);
	lv_obj_set_size(hudFuelBar, 32, 6);
	lv_obj_set_pos(hudFuelBar, 22, HudY + 3);
	lv_obj_set_style_bg_color(hudFuelBar, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(hudFuelBar, LV_OPA_70, 0);
	lv_obj_set_style_border_color(hudFuelBar, MP_DIM, 0);
	lv_obj_set_style_border_width(hudFuelBar, 1, 0);
	lv_obj_set_style_radius(hudFuelBar, 1, 0);
	lv_obj_set_style_pad_all(hudFuelBar, 0, 0);
	lv_obj_clear_flag(hudFuelBar, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(hudFuelBar, LV_OBJ_FLAG_CLICKABLE);

	// Fuel fill (an inner rect we resize per-tick).
	hudFuelFill = lv_obj_create(hudFuelBar);
	lv_obj_remove_style_all(hudFuelFill);
	lv_obj_set_size(hudFuelFill, 30, 4);
	lv_obj_set_pos(hudFuelFill, 0, 0);
	lv_obj_set_style_bg_color(hudFuelFill, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(hudFuelFill, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(hudFuelFill, 0, 0);
	lv_obj_set_style_radius(hudFuelFill, 0, 0);
	lv_obj_clear_flag(hudFuelFill, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(hudFuelFill, LV_OBJ_FLAG_CLICKABLE);

	// Altitude readout (cream).
	hudAltLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudAltLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudAltLabel, MP_TEXT, 0);
	lv_label_set_text(hudAltLabel, "ALT 000");
	lv_obj_set_pos(hudAltLabel, 60, HudY + 2);

	// Vertical speed readout (sunset accent so it stands out when
	// it goes red-zone fast on descent).
	hudVsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudVsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudVsLabel, MP_ACCENT, 0);
	lv_label_set_text(hudVsLabel, "VS +00");
	lv_obj_set_pos(hudVsLabel, 96, HudY + 2);

	// Score badge (bottom-right corner of the HUD, dim purple caption
	// + cyan number once a round has been played).
	hudScoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudScoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudScoreLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudScoreLabel, "HI 0");
	lv_obj_set_pos(hudScoreLabel, 134, HudY + 2);
}

void PhoneLunarLander::buildOverlay() {
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
	lv_label_set_text(overlayTitle, "LANDER");
	lv_obj_set_align(overlayTitle, LV_ALIGN_TOP_MID);
	lv_obj_set_y(overlayTitle, -1);

	overlayLines = lv_label_create(overlayPanel);
	lv_obj_set_style_text_font(overlayLines, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLines, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLines,
	                  "2 THRUST  4/6 STEER\nA TO LAUNCH");
	lv_obj_set_align(overlayLines, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(overlayLines, -1);
}

void PhoneLunarLander::buildTerrain() {
	// Pad highlight FIRST so the polyline draws on top of it: the pad
	// marker is a dim rectangle pinned to the flat-pad's vertical span
	// and the polyline edge slices across its top.
	padMarker = lv_obj_create(obj);
	lv_obj_remove_style_all(padMarker);
	lv_obj_set_size(padMarker, 18, 4);
	lv_obj_set_pos(padMarker, FieldX, FieldY + FieldH - 4);
	lv_obj_set_style_bg_color(padMarker, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(padMarker, LV_OPA_50, 0);
	lv_obj_set_style_border_color(padMarker, MP_ACCENT, 0);
	lv_obj_set_style_border_width(padMarker, 1, 0);
	lv_obj_set_style_radius(padMarker, 1, 0);
	lv_obj_clear_flag(padMarker, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(padMarker, LV_OBJ_FLAG_CLICKABLE);

	terrainLine = lv_line_create(obj);
	// Default points so LVGL has something to draw before the first
	// regenerateTerrain() call. regenerateTerrain() rewrites them.
	for(uint8_t i = 0; i < kTerrainPoints; ++i) {
		terrainPts[i].x = static_cast<int16_t>(FieldX + i * (FieldW / kTerrainSegments));
		terrainPts[i].y = static_cast<int16_t>(FieldY + FieldH - 6);
	}
	lv_line_set_points(terrainLine, terrainPts, kTerrainPoints);
	lv_obj_set_style_line_color(terrainLine, MP_HIGHLIGHT, 0);
	lv_obj_set_style_line_width(terrainLine, 1, 0);
	lv_obj_set_style_line_opa(terrainLine, LV_OPA_COVER, 0);
}

void PhoneLunarLander::buildShip() {
	// Container -- positioned per-tick. We size it to the ship's
	// pixel footprint and let the children draw inside.
	shipRoot = lv_obj_create(obj);
	lv_obj_remove_style_all(shipRoot);
	lv_obj_set_size(shipRoot, ShipW, ShipH + 4);    // +4 for flame slot
	lv_obj_set_pos(shipRoot, FieldX + FieldW / 2 - ShipW / 2,
	                          FieldY + 4);
	lv_obj_set_style_pad_all(shipRoot, 0, 0);
	lv_obj_set_style_bg_opa(shipRoot, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(shipRoot, 0, 0);
	lv_obj_clear_flag(shipRoot, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(shipRoot, LV_OBJ_FLAG_CLICKABLE);

	// Body: cream rounded rectangle. The legs project a touch below
	// so the foot pixels read as separate from the body.
	shipBody = lv_obj_create(shipRoot);
	lv_obj_remove_style_all(shipBody);
	lv_obj_set_size(shipBody, ShipW, ShipH - 2);
	lv_obj_set_pos(shipBody, 0, 0);
	lv_obj_set_style_bg_color(shipBody, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(shipBody, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(shipBody, MP_ACCENT, 0);
	lv_obj_set_style_border_width(shipBody, 1, 0);
	lv_obj_set_style_radius(shipBody, 2, 0);
	lv_obj_clear_flag(shipBody, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(shipBody, LV_OBJ_FLAG_CLICKABLE);

	// Legs: a thin orange bar across the bottom of the body, to give
	// the silhouette a clear "feet" line so the player knows what
	// pixel actually touches the surface.
	shipLegs = lv_obj_create(shipRoot);
	lv_obj_remove_style_all(shipLegs);
	lv_obj_set_size(shipLegs, ShipW, 2);
	lv_obj_set_pos(shipLegs, 0, ShipH - 2);
	lv_obj_set_style_bg_color(shipLegs, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(shipLegs, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(shipLegs, 0, 0);
	lv_obj_set_style_radius(shipLegs, 0, 0);
	lv_obj_clear_flag(shipLegs, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(shipLegs, LV_OBJ_FLAG_CLICKABLE);

	// Main thruster flame (down): a short tapered yellow rectangle
	// hanging off the ship's underside, hidden by default.
	shipFlame = lv_obj_create(shipRoot);
	lv_obj_remove_style_all(shipFlame);
	lv_obj_set_size(shipFlame, 2, 4);
	lv_obj_set_pos(shipFlame, ShipW / 2 - 1, ShipH);
	lv_obj_set_style_bg_color(shipFlame, MP_FLAME, 0);
	lv_obj_set_style_bg_opa(shipFlame, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(shipFlame, 0, 0);
	lv_obj_set_style_radius(shipFlame, 1, 0);
	lv_obj_clear_flag(shipFlame, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(shipFlame, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(shipFlame, LV_OBJ_FLAG_HIDDEN);

	// Side-thrust puff: a tiny rectangle that briefly appears on the
	// opposite side of the direction of travel after a steer key.
	shipFlameSide = lv_obj_create(shipRoot);
	lv_obj_remove_style_all(shipFlameSide);
	lv_obj_set_size(shipFlameSide, 2, 2);
	lv_obj_set_pos(shipFlameSide, 0, ShipH - 4);
	lv_obj_set_style_bg_color(shipFlameSide, MP_FLAME, 0);
	lv_obj_set_style_bg_opa(shipFlameSide, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(shipFlameSide, 0, 0);
	lv_obj_set_style_radius(shipFlameSide, 1, 0);
	lv_obj_clear_flag(shipFlameSide, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(shipFlameSide, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(shipFlameSide, LV_OBJ_FLAG_HIDDEN);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneLunarLander::enterIdle() {
	stopTickTimer();
	state = GameState::Idle;
	crashReason = CrashReason::None;

	// Don't reset highScore -- it survives "play again". Fuel + ship
	// position are reset in startRound() so the Idle overlay reads
	// against the most recent attempt's score.
	thrustUpHeld = thrustLeftHeld = thrustRightHeld = false;
	thrustUpFlash = thrustSideFlash = 0;
	sideFlashDir = 0;

	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshFlame();
}

void PhoneLunarLander::startRound() {
	stopTickTimer();
	state = GameState::Playing;
	crashReason = CrashReason::None;
	++attempt;

	// Fresh terrain each round so the player isn't memorising a single
	// approach -- variation comes from the pad x-position.
	regenerateTerrain();
	refreshPadMarker();

	// Initial ship state: parked at the top centre, slight starting
	// drift to make the first second feel alive (gravity is doing
	// most of the work, but a tiny rightward velocity gives the
	// player something to immediately correct).
	shipX  = (FieldX + FieldW / 2 - ShipW / 2) * 16;
	shipY  = (FieldY + 2) * 16;
	shipVx = 4;     // 4 = 0.25 px/tick to the right
	shipVy = 0;

	fuel  = kFuelStart;
	score = 0;

	thrustUpHeld = thrustLeftHeld = thrustRightHeld = false;
	thrustUpFlash = thrustSideFlash = 0;
	sideFlashDir = 0;

	renderShip();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshFlame();
	startTickTimer();
}

void PhoneLunarLander::landRound() {
	stopTickTimer();
	state = GameState::Landed;
	crashReason = CrashReason::None;

	// Score it: leftover fuel pays out 10 pts each, plus a bonus for
	// touching down nearly still in both axes.
	const uint32_t base = static_cast<uint32_t>(fuel) * kFuelScoreMul;
	uint32_t bonus = 0;
	const int16_t absVy = static_cast<int16_t>(shipVy < 0 ? -shipVy : shipVy);
	const int16_t absVx = static_cast<int16_t>(shipVx < 0 ? -shipVx : shipVx);
	if(absVy < (kSafeVy / 2) && absVx < (kSafeVx / 2)) {
		bonus = kSoftBonus;
	}
	const uint32_t total = base + bonus;
	score = static_cast<uint16_t>(total > 9999 ? 9999 : total);
	if(score > highScore) highScore = score;

	thrustUpHeld = thrustLeftHeld = thrustRightHeld = false;
	thrustUpFlash = thrustSideFlash = 0;
	sideFlashDir = 0;

	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshFlame();
}

void PhoneLunarLander::crashRound(CrashReason reason) {
	stopTickTimer();
	state = GameState::Crashed;
	crashReason = reason;
	score = 0;     // crashes pay nothing -- only the high-score persists.

	thrustUpHeld = thrustLeftHeld = thrustRightHeld = false;
	thrustUpFlash = thrustSideFlash = 0;
	sideFlashDir = 0;

	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshFlame();
}

// ===========================================================================
// game loop
// ===========================================================================

void PhoneLunarLander::tick() {
	if(state != GameState::Playing) return;

	// 1. Apply thrust from latched keys (only if there's fuel to burn).
	if(thrustUpHeld && fuel > 0) {
		shipVy -= kThrustUp;
		--fuel;
		thrustUpFlash = 2;     // keep the flame visible for 2 ticks
	}
	if(thrustLeftHeld && fuel > 0) {
		shipVx -= kThrustSide;
		--fuel;
		thrustSideFlash = 2;
		sideFlashDir = -1;
	}
	if(thrustRightHeld && fuel > 0) {
		shipVx += kThrustSide;
		--fuel;
		thrustSideFlash = 2;
		sideFlashDir = +1;
	}

	// 2. Gravity always pulls.
	shipVy += kGravity;

	// 3. Clamp velocities so an inattentive descent eventually plateaus
	//    (the terminal-velocity cap is what lets a careful pilot
	//    correct a bad approach).
	if(shipVx >  kVMaxX) shipVx =  kVMaxX;
	if(shipVx < -kVMaxX) shipVx = -kVMaxX;
	if(shipVy >  kVMaxY) shipVy =  kVMaxY;
	if(shipVy < -kVMaxY) shipVy = -kVMaxY;

	// 4. Integrate position.
	shipX += shipVx;
	shipY += shipVy;

	// 5. Wrap horizontally so the player can't fly out of the playfield
	//    -- classic Lunar Lander behaviour. The vertical edge is hard:
	//    bumping the top is not a crash but the ship can't escape it.
	const int32_t leftLimitQ4  = static_cast<int32_t>(FieldX) * 16;
	const int32_t rightLimitQ4 = static_cast<int32_t>(FieldX + FieldW - ShipW) * 16;
	if(shipX < leftLimitQ4) {
		shipX = rightLimitQ4;
	} else if(shipX > rightLimitQ4) {
		shipX = leftLimitQ4;
	}
	if(shipY < static_cast<int32_t>(FieldY) * 16) {
		shipY = static_cast<int32_t>(FieldY) * 16;
		if(shipVy < 0) shipVy = 0;
	}

	// 6. Decrement post-press flame flashes so the visual fades off
	//    even when the key is held.
	if(thrustUpFlash > 0)   --thrustUpFlash;
	if(thrustSideFlash > 0) --thrustSideFlash;

	// 7. Resolve collisions / landing / crashes.
	const int16_t pxX = static_cast<int16_t>(shipX / 16);
	const int16_t pxY = static_cast<int16_t>(shipY / 16);
	if(collidesWithTerrain(pxX, pxY)) {
		// Touchdown! Decide whether it's a clean landing or a crash.
		const int16_t absVy = static_cast<int16_t>(shipVy < 0 ? -shipVy : shipVy);
		const int16_t absVx = static_cast<int16_t>(shipVx < 0 ? -shipVx : shipVx);

		if(!footOnPad(pxX, pxY)) {
			crashRound(CrashReason::HitTerrain);
		} else if(absVy >= kSafeVy) {
			crashRound(CrashReason::TooFastV);
		} else if(absVx >= kSafeVx) {
			crashRound(CrashReason::TooFastH);
		} else {
			// Snap ship to the pad's exact y so the rendered touchdown
			// reads cleanly.
			const int16_t padY = terrainYAt(pxX + ShipW / 2);
			shipY = static_cast<int32_t>(padY - ShipH) * 16;
			shipVx = 0;
			shipVy = 0;
			renderShip();
			refreshHud();
			refreshFlame();
			landRound();
			return;
		}
		renderShip();
		refreshHud();
		refreshFlame();
		return;
	}

	// 8. Out-of-fuel mid-flight: not an immediate crash, but the player
	//    is now under pure gravity with no recovery. We let physics
	//    play it out -- the eventual collision will be classified.
	(void)CrashReason::OutOfFuel;     // currently unused; preserved for UX

	// 9. Render.
	renderShip();
	refreshHud();
	refreshFlame();
}

// ===========================================================================
// physics helpers
// ===========================================================================

int16_t PhoneLunarLander::terrainYAt(int16_t px) const {
	// Linear interpolation between the two polyline vertices that
	// straddle `px`. Out-of-range x-values clamp to the nearest edge.
	if(kTerrainPoints == 0) return FieldY + FieldH;
	if(px <= terrainPts[0].x) return terrainPts[0].y;
	if(px >= terrainPts[kTerrainPoints - 1].x) {
		return terrainPts[kTerrainPoints - 1].y;
	}
	for(uint8_t i = 0; i + 1 < kTerrainPoints; ++i) {
		const int16_t x0 = terrainPts[i].x;
		const int16_t x1 = terrainPts[i + 1].x;
		if(px >= x0 && px <= x1) {
			const int16_t y0 = terrainPts[i].y;
			const int16_t y1 = terrainPts[i + 1].y;
			const int16_t span = (x1 - x0) == 0 ? 1 : (x1 - x0);
			return static_cast<int16_t>(
				y0 + ((static_cast<int32_t>(y1 - y0) *
				       static_cast<int32_t>(px - x0)) / span));
		}
	}
	return FieldY + FieldH;
}

bool PhoneLunarLander::collidesWithTerrain(int16_t shipPxX, int16_t shipPxY) const {
	// Sample the terrain at three points across the ship's foot line:
	// left edge, centre, right edge. If the foot pixel y is at or below
	// the terrain y at any of those samples, the ship has touched down.
	const int16_t footY = static_cast<int16_t>(shipPxY + ShipH);
	const int16_t samples[3] = {
		shipPxX,
		static_cast<int16_t>(shipPxX + ShipW / 2),
		static_cast<int16_t>(shipPxX + ShipW - 1),
	};
	for(uint8_t i = 0; i < 3; ++i) {
		const int16_t terrainY = terrainYAt(samples[i]);
		if(footY >= terrainY) return true;
	}
	return false;
}

bool PhoneLunarLander::footOnPad(int16_t shipPxX, int16_t shipPxY) const {
	(void)shipPxY;
	// The pad spans terrainPts[padLeftIdx].x .. terrainPts[padLeftIdx+1].x
	// and is the only horizontal segment in the polyline. The whole
	// ship-foot footprint must lie inside that range to count.
	if(padLeftIdx + 1 >= kTerrainPoints) return false;
	const int16_t padX0 = terrainPts[padLeftIdx].x;
	const int16_t padX1 = terrainPts[padLeftIdx + 1].x;
	return shipPxX >= padX0 && (shipPxX + ShipW - 1) <= padX1;
}

void PhoneLunarLander::regeneratePadIndex() {
	// Pick a pad index in the inner half of the terrain so the player
	// never starts with the ship already over the pad. Each attempt
	// advances the index by a coprime stride so the sequence visibly
	// varies but is fully deterministic per screen-life.
	const uint8_t middleStart = 3;
	const uint8_t middleEnd   = static_cast<uint8_t>(kTerrainSegments - 4);
	const uint8_t span        = (middleEnd >= middleStart)
	                              ? static_cast<uint8_t>(middleEnd - middleStart + 1)
	                              : 1;
	const uint8_t stride = 5;     // coprime with most reasonable spans
	padLeftIdx = static_cast<uint8_t>(
		middleStart + ((attempt * stride) % span));
}

void PhoneLunarLander::regenerateTerrain() {
	regeneratePadIndex();

	// Walk the polyline from left to right, alternating up/down jagged
	// y-offsets to give the lunar surface its broken silhouette.
	// Heights are picked from a fixed table so the result feels hand-
	// crafted rather than too-uniform-noisy. The pad segment forces
	// both endpoints to share the same y-value.
	static const int8_t hTable[kTerrainPoints] = {
		 6,  4,  9,  3, 11,  5,  8,  2,
		10,  4,  7,  3,  9,  5,  6,  4, 8
	};

	// Terrain hugs the bottom 22 px of the playfield (FieldH = 96, so
	// y ranges from FieldY+74 .. FieldY+92, leaving room for the pad
	// marker just below the lowest peak). The pad's y is fixed near
	// the bottom so the silhouette reads as "ground level".
	const int16_t baseY = static_cast<int16_t>(FieldY + FieldH - 8);
	const int16_t padY  = static_cast<int16_t>(FieldY + FieldH - 5);

	for(uint8_t i = 0; i < kTerrainPoints; ++i) {
		terrainPts[i].x = static_cast<int16_t>(
			FieldX + (static_cast<int32_t>(i) *
			          static_cast<int32_t>(FieldW)) / kTerrainSegments);
		terrainPts[i].y = static_cast<int16_t>(baseY - hTable[i]);
	}

	// Pad: two adjacent vertices share padY.
	if(padLeftIdx + 1 < kTerrainPoints) {
		terrainPts[padLeftIdx].y     = padY;
		terrainPts[padLeftIdx + 1].y = padY;
	}

	if(terrainLine != nullptr) {
		// LVGL re-evaluates the polyline whenever set_points is called
		// even with the same pointer, which is what we want here.
		lv_line_set_points(terrainLine, terrainPts, kTerrainPoints);
		lv_obj_invalidate(terrainLine);
	}
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneLunarLander::renderShip() {
	if(shipRoot == nullptr) return;
	const int16_t pxX = static_cast<int16_t>(shipX / 16);
	const int16_t pxY = static_cast<int16_t>(shipY / 16);
	lv_obj_set_pos(shipRoot, pxX, pxY);
}

void PhoneLunarLander::refreshHud() {
	// Fuel bar: 30 px wide inner rect representing kFuelStart units.
	if(hudFuelFill != nullptr) {
		const uint16_t cap = kFuelStart == 0 ? 1 : kFuelStart;
		const uint16_t f   = fuel > cap ? cap : fuel;
		const lv_coord_t w = static_cast<lv_coord_t>(
			(static_cast<uint32_t>(f) * 30u) / cap);
		lv_obj_set_size(hudFuelFill, w, 4);
		// Tint shifts to sunset orange when the tank dips below a
		// quarter so the player feels the panic.
		lv_obj_set_style_bg_color(hudFuelFill,
			(f < (cap / 4)) ? MP_ACCENT : MP_HIGHLIGHT, 0);
	}

	// Altitude: pixels from the ship's foot to the highest terrain peak
	// directly under it. We sample once at the centre.
	if(hudAltLabel != nullptr) {
		const int16_t pxX = static_cast<int16_t>(shipX / 16);
		const int16_t pxY = static_cast<int16_t>(shipY / 16);
		const int16_t terrainY = terrainYAt(static_cast<int16_t>(pxX + ShipW / 2));
		int16_t alt = static_cast<int16_t>(terrainY - (pxY + ShipH));
		if(alt < 0) alt = 0;
		char buf[16];
		snprintf(buf, sizeof(buf), "ALT %03d", alt);
		lv_label_set_text(hudAltLabel, buf);
	}

	// Vertical speed (in tenths of a px/tick to keep the HUD from
	// flickering between 0 and 1 every frame).
	if(hudVsLabel != nullptr) {
		const int16_t vsTenths = static_cast<int16_t>((shipVy * 10) / 16);
		char buf[16];
		snprintf(buf, sizeof(buf), "VS %+03d", vsTenths);
		lv_label_set_text(hudVsLabel, buf);
		lv_obj_set_style_text_color(hudVsLabel,
			(vsTenths > 5) ? MP_ACCENT : MP_TEXT, 0);
	}

	if(hudScoreLabel != nullptr) {
		char buf[16];
		const unsigned h = highScore > 9999 ? 9999 : highScore;
		snprintf(buf, sizeof(buf), "HI %u", h);
		lv_label_set_text(hudScoreLabel, buf);
	}
}

void PhoneLunarLander::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("LAUNCH");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			softKeys->setLeft("THRUST");
			softKeys->setRight("BACK");
			break;
		case GameState::Landed:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
		case GameState::Crashed:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneLunarLander::refreshOverlay() {
	if(overlayPanel == nullptr) return;
	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayTitle, "LANDER");
			lv_obj_set_style_text_color(overlayTitle, MP_ACCENT, 0);
			lv_label_set_text(overlayLines,
			                  "2 THRUST  4/6 STEER\nA TO LAUNCH");
			lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayPanel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayPanel);
			break;

		case GameState::Playing:
			lv_obj_add_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			break;

		case GameState::Landed: {
			char body[48];
			const unsigned s = score;
			const unsigned h = highScore;
			snprintf(body, sizeof(body),
			         "TOUCHDOWN!\nSCORE %u  HI %u\nA TO PLAY AGAIN",
			         s, h);
			lv_label_set_text(overlayTitle, "EAGLE LANDED");
			lv_obj_set_style_text_color(overlayTitle, MP_HIGHLIGHT, 0);
			lv_label_set_text(overlayLines, body);
			lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayPanel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayPanel);
			break;
		}

		case GameState::Crashed: {
			const char* reason = "CRASHED";
			switch(crashReason) {
				case CrashReason::HitTerrain: reason = "HIT TERRAIN";    break;
				case CrashReason::TooFastV:   reason = "DESCENT TOO FAST"; break;
				case CrashReason::TooFastH:   reason = "DRIFT TOO HIGH"; break;
				case CrashReason::MissedPad:  reason = "MISSED THE PAD"; break;
				case CrashReason::OutOfFuel:  reason = "OUT OF FUEL";    break;
				case CrashReason::None:       break;
			}
			char body[64];
			snprintf(body, sizeof(body),
			         "%s\nHI %u\nA TO TRY AGAIN",
			         reason, highScore);
			lv_label_set_text(overlayTitle, "CRASH!");
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

void PhoneLunarLander::refreshFlame() {
	if(shipFlame == nullptr) return;
	if(state == GameState::Playing && thrustUpFlash > 0 && fuel > 0) {
		lv_obj_clear_flag(shipFlame, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(shipFlame, LV_OBJ_FLAG_HIDDEN);
	}
	if(shipFlameSide != nullptr) {
		if(state == GameState::Playing && thrustSideFlash > 0 && fuel > 0) {
			// The puff appears on the side OPPOSITE to acceleration so
			// it reads as exhaust.
			if(sideFlashDir < 0) {
				lv_obj_set_pos(shipFlameSide, ShipW, ShipH - 4);
			} else {
				lv_obj_set_pos(shipFlameSide, -2, ShipH - 4);
			}
			lv_obj_clear_flag(shipFlameSide, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(shipFlameSide, LV_OBJ_FLAG_HIDDEN);
		}
	}
}

void PhoneLunarLander::refreshPadMarker() {
	if(padMarker == nullptr) return;
	if(padLeftIdx + 1 >= kTerrainPoints) {
		lv_obj_add_flag(padMarker, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	const int16_t x0 = terrainPts[padLeftIdx].x;
	const int16_t x1 = terrainPts[padLeftIdx + 1].x;
	const int16_t y  = terrainPts[padLeftIdx].y;
	const int16_t w  = static_cast<int16_t>(x1 - x0);
	if(w <= 0) {
		lv_obj_add_flag(padMarker, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_set_size(padMarker, w, 3);
	lv_obj_set_pos(padMarker, x0, y);
	lv_obj_clear_flag(padMarker, LV_OBJ_FLAG_HIDDEN);
}

// ===========================================================================
// timer helpers
// ===========================================================================

void PhoneLunarLander::startTickTimer() {
	stopTickTimer();
	tickTimer = lv_timer_create(&PhoneLunarLander::onTickStatic,
	                            kTickMs, this);
}

void PhoneLunarLander::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneLunarLander::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneLunarLander*>(timer->user_data);
	if(self == nullptr) return;
	self->tick();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneLunarLander::buttonPressed(uint i) {
	// BACK always pops, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// R restarts the round (any state). Useful both for "give up
	// mid-game" and "go again after game over".
	if(i == BTN_R) {
		startRound();
		return;
	}

	switch(state) {
		case GameState::Idle:
			if(i == BTN_5 || i == BTN_ENTER || i == BTN_2) {
				if(softKeys) softKeys->flashLeft();
				startRound();
			}
			return;

		case GameState::Playing: {
			// Latch thrust inputs. Either of the "centre" keys plus the
			// engine d-pad up gives us a wide finger-target for the
			// main thruster -- BTN_2 is the dialer-up position, BTN_5
			// is the dialer-centre, and BTN_ENTER is the explicit A
			// button so right-handed and left-handed grips both work.
			if(i == BTN_2 || i == BTN_5 || i == BTN_ENTER) {
				thrustUpHeld = true;
				if(softKeys) softKeys->flashLeft();
			}
			if(i == BTN_4 || i == BTN_LEFT)  thrustLeftHeld  = true;
			if(i == BTN_6 || i == BTN_RIGHT) thrustRightHeld = true;
			return;
		}

		case GameState::Landed:
		case GameState::Crashed:
			if(i == BTN_5 || i == BTN_ENTER || i == BTN_2) {
				if(softKeys) softKeys->flashLeft();
				startRound();
			}
			return;
	}
}

void PhoneLunarLander::buttonReleased(uint i) {
	// Only Playing cares about releases -- the other states have no
	// continuous inputs.
	if(state != GameState::Playing) return;

	if(i == BTN_2 || i == BTN_5 || i == BTN_ENTER) thrustUpHeld    = false;
	if(i == BTN_4 || i == BTN_LEFT)                thrustLeftHeld  = false;
	if(i == BTN_6 || i == BTN_RIGHT)               thrustRightHeld = false;
}
