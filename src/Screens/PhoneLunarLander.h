#ifndef MAKERPHONE_PHONELUNARLANDER_H
#define MAKERPHONE_PHONELUNARLANDER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneLunarLander (S91)
 *
 * Phase-N arcade entry: a fuel/thrust physics game in the spirit of
 * the 1979 Atari original. The player descends from the top of the
 * 160x128 panel toward a hand-crafted lunar surface, and must land
 * softly on a flat pad before fuel runs out -- gravity does the rest.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar (10 px)
 *   | FUEL [######  ] ALT 056  VS -02   | <- HUD strip      (12 px)
 *   |                                    |
 *   |              .                     |
 *   |             /^\                    | <- 6x8 ship sprite, flame
 *   |              v                     |    rectangle when thrusting
 *   |                                    |
 *   |       ___                          |
 *   |      /   \____                     | <- jagged terrain, single
 *   |     /         \___ XXX __          |    flat pad marked with X
 *   |____/             \____/  \____     |
 *   |   THRUST              BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 / BTN_5 / BTN_ENTER : main downward-pointing thruster
 *                                 (cancels gravity).
 *   - BTN_4 / BTN_LEFT          : side thruster pushing the ship left.
 *   - BTN_6 / BTN_RIGHT         : side thruster pushing the ship right.
 *   - BTN_R                     : restart current attempt.
 *   - BTN_BACK (B)              : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- rules + "A TO LAUNCH" overlay. Soft-keys: LAUNCH / BACK.
 *   Playing   -- ticks running; ship under physics. Soft-keys: THRUST / BACK.
 *   Landed    -- successful touch-down overlay with score. AGAIN / BACK.
 *   Crashed   -- crash overlay with the failure reason. AGAIN / BACK.
 *
 * Implementation notes:
 *  - 100% code-only -- the lunar surface is rendered as a single
 *    `lv_line` polyline that the screen owns; the ship is a tiny
 *    container with three rounded rectangles for the body, the legs,
 *    and the flame. No SPIFFS asset cost (the data partition is
 *    precious).
 *  - Physics runs in Q4 fixed-point (16 = 1 px) so the ship can drift
 *    sub-pixel between renders without juddering. A 50 ms tick keeps
 *    the LVGL scheduler comfortable while still feeling responsive.
 *  - The terrain points stay in stable instance-owned arrays because
 *    `lv_line_set_points` keeps the pointer rather than copying.
 *  - There is exactly one flat landing pad; any other contact with
 *    the surface is a crash. A vertical/horizontal speed budget gates
 *    "soft landing" vs "crash on the pad" so the player has to
 *    actually slow down rather than just aim correctly.
 *  - Score = remaining fuel * 10 + bonus for a perfectly-still
 *    landing. Persists across "play again" (highScore) but resets
 *    when the screen is popped, matching every other Phone* game.
 *  - Tick driver is a single repeating `lv_timer` started in
 *    onStart() and stopped in onStop() so the screen survives being
 *    pushed and re-popped without leaking timers.
 */
class PhoneLunarLander : public LVScreen, private InputListener {
public:
	PhoneLunarLander();
	virtual ~PhoneLunarLander() override;

	void onStart() override;
	void onStop() override;

	// ---- Layout constants (160 x 128 panel) ---------------------------
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	// Playfield is the rectangle between the HUD and the soft-key bar.
	static constexpr lv_coord_t FieldX = 0;
	static constexpr lv_coord_t FieldY = StatusBarH + HudH;          // 22
	static constexpr lv_coord_t FieldW = 160;
	static constexpr lv_coord_t FieldH = 128 - FieldY - SoftKeyH;    // 96

	// ---- Ship dimensions ---------------------------------------------
	static constexpr lv_coord_t ShipW = 6;
	static constexpr lv_coord_t ShipH = 8;

	// ---- Terrain ------------------------------------------------------
	// The terrain is a polyline with kTerrainSegments + 1 vertices
	// strung horizontally across the bottom of the playfield. One pair
	// of adjacent vertices share the same `y` value -- that's the
	// landing pad. The pad index varies between rounds so successive
	// attempts don't repeat the exact same approach.
	static constexpr uint8_t   kTerrainSegments = 16;
	static constexpr uint8_t   kTerrainPoints   = kTerrainSegments + 1;

	// ---- Physics tuning (Q4 fixed-point: 16 units = 1 px) ------------
	// Gravity accelerates vy by +kGravity per tick.
	// Main thruster decelerates vy by kThrustUp per tick (still slower
	// than gravity-canceling so the player feels the descent).
	// Side thrusters add +/- kThrustSide to vx per tick.
	// Tick is fixed at kTickMs.
	static constexpr uint16_t kTickMs       = 50;
	static constexpr int16_t  kGravity      = 2;     // 2 = 0.125 px/tick^2
	static constexpr int16_t  kThrustUp     = 6;     // 6 = 0.375 px/tick^2
	static constexpr int16_t  kThrustSide   = 3;     // 3 = 0.1875 px/tick^2
	static constexpr int16_t  kVMaxX        = 24;    // 24 = 1.5 px/tick
	static constexpr int16_t  kVMaxY        = 32;    // 32 = 2.0 px/tick

	// Landing budget: vertical speed must be < kSafeVy (Q4 px/tick),
	// horizontal speed < kSafeVx, and the ship's foot pixels must lie
	// fully within the flat pad's x range.
	static constexpr int16_t  kSafeVy       = 12;    // 12 = 0.75 px/tick
	static constexpr int16_t  kSafeVx       = 8;     // 8  = 0.5  px/tick

	// Fuel: each main-thrust tick costs 1, each side-thrust tick 1.
	// Tank capacity is generous enough for a careful descent and
	// stingy enough to make panic-thrust spirals fail.
	static constexpr uint16_t kFuelStart    = 200;

	// Score-screen tunables.
	static constexpr uint16_t kFuelScoreMul = 10;     // pts per fuel unit
	static constexpr uint16_t kSoftBonus    = 250;    // perfect-touch bonus

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// HUD strip: fuel bar + numeric ALT + numeric VS readouts.
	lv_obj_t* hudFuelLabel  = nullptr;
	lv_obj_t* hudFuelBar    = nullptr;
	lv_obj_t* hudFuelFill   = nullptr;
	lv_obj_t* hudAltLabel   = nullptr;
	lv_obj_t* hudVsLabel    = nullptr;
	lv_obj_t* hudScoreLabel = nullptr;

	// Terrain polyline.
	lv_obj_t* terrainLine   = nullptr;
	lv_point_t terrainPts[kTerrainPoints];
	lv_obj_t* padMarker     = nullptr;   // sunset-orange flat-pad highlight

	// Ship: a small container with body + legs + flame children. We
	// move the container, never the children, so rendering is one
	// position update per tick.
	lv_obj_t* shipRoot      = nullptr;
	lv_obj_t* shipBody      = nullptr;
	lv_obj_t* shipLegs      = nullptr;
	lv_obj_t* shipFlame     = nullptr;
	lv_obj_t* shipFlameSide = nullptr;   // brief side-thrust puff sprite

	// Centred rules / outcome card.
	lv_obj_t* overlayPanel  = nullptr;
	lv_obj_t* overlayTitle  = nullptr;
	lv_obj_t* overlayLines  = nullptr;

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Playing,
		Landed,
		Crashed,
	};

	enum class CrashReason : uint8_t {
		None       = 0,
		HitTerrain = 1,
		TooFastV   = 2,
		TooFastH   = 3,
		MissedPad  = 4,
		OutOfFuel  = 5,    // ran out mid-flight, then crashed; minor flavour
	};

	GameState   state       = GameState::Idle;
	CrashReason crashReason = CrashReason::None;

	// Position in Q4 fixed-point (16 = 1 px), measured from the top-left
	// of the LVGL panel. Velocity in Q4-per-tick.
	int32_t shipX  = 0;
	int32_t shipY  = 0;
	int32_t shipVx = 0;
	int32_t shipVy = 0;

	uint16_t fuel       = kFuelStart;
	uint16_t score      = 0;
	uint16_t highScore  = 0;
	uint16_t attempt    = 0;     // counts plays in this screen lifetime

	// Latched input flags -- set by buttonPressed, consumed by tick.
	// We use latches so a single press still produces at least one
	// thrust tick even if the player taps faster than kTickMs.
	bool thrustUpHeld    = false;
	bool thrustLeftHeld  = false;
	bool thrustRightHeld = false;
	uint8_t thrustUpFlash   = 0;   // ticks remaining of "just pressed" flame
	uint8_t thrustSideFlash = 0;
	int8_t  sideFlashDir    = 0;   // -1 = left, +1 = right

	// Index of the flat-pad's left vertex inside terrainPts.
	uint8_t padLeftIdx = 4;

	// Running tick driver.
	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildTerrain();
	void buildShip();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startRound();
	void landRound();
	void crashRound(CrashReason reason);

	// Game-loop step (called from the tick timer).
	void tick();

	// ---- physics helpers ---------------------------------------------
	bool collidesWithTerrain(int16_t shipPxX, int16_t shipPxY) const;
	int16_t terrainYAt(int16_t px) const;
	bool footOnPad(int16_t shipPxX, int16_t shipPxY) const;

	void regenerateTerrain();
	void regeneratePadIndex();

	// ---- rendering ----------------------------------------------------
	void renderShip();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();
	void refreshFlame();
	void refreshPadMarker();

	// ---- timer helpers ------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONELUNARLANDER_H
