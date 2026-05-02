#ifndef MAKERPHONE_PHONEHELICOPTER_H
#define MAKERPHONE_PHONEHELICOPTER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneHelicopter (S92)
 *
 * Phase-N arcade entry: an endless side-scrolling avoidance game in
 * the spirit of the early-2000s "Helicopter Game" Flash classic. The
 * player flies a tiny chopper that hovers at a fixed x while the
 * world scrolls past from the right. Hold the thrust key to climb,
 * release to fall. The cave narrows over time and pillars rise out
 * of the floor, leaving a gap the chopper has to thread.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar (10 px)
 *   |   DIST 0123              HI 4567   | <- HUD strip      (10 px)
 *   |####################################|  <- ceiling band
 *   |                                    |
 *   |   <==o>                            | <- chopper sprite (12x6)
 *   |                  ##                |    + pillars sliding left
 *   |                  ##                |
 *   |                  ##                |
 *   |####################################|  <- floor band
 *   |   THRUST              BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 / BTN_5 / BTN_ENTER : thrust up (cancels and reverses
 *                                 gravity while held).
 *   - BTN_R                     : restart the current attempt.
 *   - BTN_BACK (B)              : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      - "A TO FLY" overlay. Soft-keys: FLY / BACK.
 *   Playing   - tick driver active, chopper under physics + scroll.
 *               Soft-keys: THRUST / BACK.
 *   Crashed   - hit the ceiling, floor, or a pillar. Soft-keys:
 *               AGAIN / BACK.
 *
 * Implementation notes:
 *  - 100% code-only - the cave is two horizontal bars (`ceiling` /
 *    `floor`), the pillars are a small fixed pool of `lv_obj`
 *    rectangles that recycle off-screen, and the chopper is a tiny
 *    container with body + rotor + tail children. No SPIFFS asset
 *    cost (the data partition is precious).
 *  - Physics runs in Q4 fixed-point (16 = 1 px) so the chopper can
 *    drift sub-pixel between renders without juddering. A 50 ms
 *    tick keeps the LVGL scheduler comfortable while still feeling
 *    responsive on the 160x128 panel.
 *  - The pillar pool size is fixed at compile time - we only ever
 *    need enough on-screen pillars to span the playfield given
 *    PillarSpacing, so a single-digit pool keeps memory small and
 *    rendering cheap.
 *  - Pillar gap positions use a tiny LCG seeded from `attempt` so
 *    successive rounds vary while staying fully deterministic per
 *    screen-life - we never call rand() to keep behaviour stable
 *    across CI runs and predictable for QA.
 *  - Tick driver is a single repeating `lv_timer` started in
 *    onStart()/startRound() and stopped in onStop()/crashRound() so
 *    the screen survives being pushed and re-popped without leaking
 *    timers.
 */
class PhoneHelicopter : public LVScreen, private InputListener {
public:
	PhoneHelicopter();
	virtual ~PhoneHelicopter() override;

	void onStart() override;
	void onStop() override;

	// ---- Layout constants (160 x 128 panel) ---------------------------
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudH       = 10;

	static constexpr lv_coord_t FieldX = 0;
	static constexpr lv_coord_t FieldY = StatusBarH + HudH;        // 20
	static constexpr lv_coord_t FieldW = 160;
	static constexpr lv_coord_t FieldH = 128 - FieldY - SoftKeyH;  // 98

	// Cave walls take a 2-px slice off the top and bottom of the
	// playfield. Anything that touches them is a crash.
	static constexpr lv_coord_t CeilingH = 2;
	static constexpr lv_coord_t FloorH   = 2;

	// ---- Helicopter dimensions ---------------------------------------
	static constexpr lv_coord_t CopterX = 28;       // pinned in x
	static constexpr lv_coord_t CopterW = 12;
	static constexpr lv_coord_t CopterH = 6;

	// ---- Physics tuning (Q4 fixed-point: 16 units = 1 px) ------------
	static constexpr uint16_t kTickMs   = 50;
	static constexpr int16_t  kGravity  = 3;    // 3  = 0.1875 px/tick^2
	static constexpr int16_t  kThrust   = 7;    // 7  = 0.4375 px/tick^2 up
	static constexpr int16_t  kVMax     = 36;   // 36 = 2.25  px/tick

	// ---- Pillar pool --------------------------------------------------
	// PillarCount * PillarSpacing must be > FieldW + PillarW so we
	// always have a recyclable pillar off-screen at the right.
	static constexpr uint8_t   kPillarCount  = 5;
	static constexpr lv_coord_t PillarW      = 8;
	static constexpr lv_coord_t PillarSpacing = 38;   // x stride between pillars
	static constexpr lv_coord_t PillarGapMin  = 28;   // narrowest tunnel gap
	static constexpr lv_coord_t PillarGapMax  = 44;   // widest tunnel gap
	static constexpr lv_coord_t PillarMargin  = 6;    // keep gap off the walls

	// ---- Scroll ramp --------------------------------------------------
	// Pillars + pattern shift `scrollSpeed` px to the left per tick. We
	// start at kScrollMin and crank it up by 1 every kScrollRamp ticks
	// (capped at kScrollMax) so the game gets harder the longer you
	// fly. Helicopter velocity ramps mean a careful pilot still has a
	// fighting chance well past the first speed-up.
	static constexpr int16_t  kScrollMin  = 1;
	static constexpr int16_t  kScrollMax  = 3;
	static constexpr uint16_t kScrollRamp = 400;   // ticks per +1 step

	// ---- Score --------------------------------------------------------
	// Distance ticks up by `scrollSpeed` each frame so the score reads
	// roughly as "pixels travelled".
	static constexpr uint16_t kDistMax = 9999;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// HUD
	lv_obj_t* hudDistLabel = nullptr;
	lv_obj_t* hudHiLabel   = nullptr;

	// Cave borders (cyan ceiling, sunset-orange floor).
	lv_obj_t* ceilingObj = nullptr;
	lv_obj_t* floorObj   = nullptr;

	// Helicopter container + parts. We move the container per-tick.
	lv_obj_t* copterRoot  = nullptr;
	lv_obj_t* copterBody  = nullptr;
	lv_obj_t* copterDome  = nullptr;     // cyan cockpit bubble
	lv_obj_t* copterTail  = nullptr;     // back boom
	lv_obj_t* copterRotor = nullptr;     // top blur bar
	lv_obj_t* copterSkids = nullptr;     // landing skids
	lv_obj_t* copterTailRotor = nullptr; // tiny rear rotor

	// Pillar pool. Each pillar is two stacked rectangles -- top
	// half hangs from the ceiling, bottom half rises from the floor,
	// and the gap between them is what the chopper threads.
	lv_obj_t* pillarTop[kPillarCount]    = {nullptr};
	lv_obj_t* pillarBottom[kPillarCount] = {nullptr};
	int16_t   pillarX[kPillarCount]       = {0};
	int16_t   pillarGapY[kPillarCount]    = {0};   // y of gap top edge
	int16_t   pillarGapH[kPillarCount]    = {0};   // gap height
	bool      pillarActive[kPillarCount]  = {false};

	// Overlay
	lv_obj_t* overlayPanel = nullptr;
	lv_obj_t* overlayTitle = nullptr;
	lv_obj_t* overlayLines = nullptr;

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t { Idle, Playing, Crashed };
	GameState state = GameState::Idle;

	// Helicopter physics in Q4 fixed-point.
	int32_t copterY  = 0;
	int32_t copterVy = 0;

	bool     thrustHeld = false;
	uint8_t  rotorPhase = 0;        // 0..3, animates the rotor blur

	uint16_t distance   = 0;
	uint16_t highScore  = 0;
	uint16_t tickCount  = 0;
	uint16_t attempt    = 0;
	int16_t  scrollSpeed = kScrollMin;

	// LCG state for pillar gap positions (deterministic per round).
	uint32_t rngState   = 0x1234ABCDu;

	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildBorders();
	void buildCopter();
	void buildPillars();
	void buildOverlay();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startRound();
	void crashRound();

	// Game-loop step.
	void tick();

	// ---- pillar helpers -----------------------------------------------
	void resetPillars();
	void recyclePillarTo(uint8_t idx, int16_t newX);
	int16_t nextGapY(int16_t gapH);
	uint16_t lcg();

	// ---- collision ----------------------------------------------------
	bool checkCollision();

	// ---- rendering ----------------------------------------------------
	void renderCopter();
	void renderPillars();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();
	void refreshRotor();

	// ---- timer helpers ------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONEHELICOPTER_H
