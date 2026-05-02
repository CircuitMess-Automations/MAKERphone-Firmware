#ifndef MAKERPHONE_PHONEPINBALL_H
#define MAKERPHONE_PHONEPINBALL_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhonePinball (S85)
 *
 * Phase-N arcade entry: a single-table pinball game with a pair of
 * flippers at the bottom and a cluster of bumpers above. Cast in the
 * MAKERphone palette so it slots in beside the rest of the Phone*
 * arcade carousel without a visual seam.
 *
 * Layout (160 x 128):
 *   - PhoneStatusBar at the top   (10 px)
 *   - HUD strip                   (10 px) -- "PINBALL  *** 0042"
 *   - Playfield                   (98 px tall, 160 px wide)
 *   - PhoneSoftKeyBar at bottom   (10 px)
 *
 * Table layout:
 *   - Closed top + sides + bottom (the bottom is broken by a drain
 *     gap between the two flippers).
 *   - 5 round bumpers in the upper third / centre arranged so the
 *     ball naturally bounces between them after launch.
 *   - Two flippers near the bottom, each on a fixed pivot and with
 *     two states: REST (tip drops 8 px) and ACTIVE (tip raises 8 px).
 *   - Drain gap of ~12 px between the flipper tips at rest. Both
 *     flippers raised closes the drain entirely (the "save" reflex).
 *
 * Controls:
 *   - BTN_4 / BTN_LEFT  : hold = left flipper raised. Release = drop.
 *   - BTN_6 / BTN_RIGHT : hold = right flipper raised. Release = drop.
 *   - BTN_ENTER (A)     : launch ball / pause / resume / restart.
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- "PRESS START" overlay, ball parked above bumpers.
 *                ENTER -> Playing (ball stuck until next ENTER).
 *   Playing   -- physics tick (30 Hz). Ball stuck above bumpers
 *                until the player presses ENTER to launch (or after
 *                a respawn following a drain).
 *   Paused    -- tick suspended, "PAUSED" overlay.
 *                ENTER -> Playing.
 *   GameOver  -- "GAME OVER" overlay (lives ran out).
 *                ENTER -> Idle (full reset).
 *
 * Scoring:
 *   - Bumper hit:    100 points. Tiny halo flash for ~6 ticks.
 *   - Top wall hit:   10 points.
 *   - Flipper hit:     5 points (encourages active play).
 *
 * Implementation notes:
 *   - 100% code-only -- no SPIFFS asset cost. Bumpers are circular
 *     `lv_obj` rectangles with `lv_obj_set_style_radius(LV_RADIUS_CIRCLE)`
 *     so they read as discs. Flippers are rendered as a short string
 *     of 3x3 dot sprites positioned along the pivot->tip line, which
 *     side-steps LVGL 8.x's lack of cheap rotated-rectangle support
 *     and still looks crisp on the 160x128 panel.
 *   - Ball position / velocity in Q8 fixed-point so collision
 *     bookkeeping stays integer-only on the ESP32. Gravity is a
 *     small constant Vy delta applied per physics tick.
 *   - Bumper / flipper collision uses circle-vs-circle and
 *     circle-vs-segment math, both performed in integer pixel space
 *     with a Newton's-method `isqrt` for the rare normalisation.
 */
class PhonePinball : public LVScreen, private InputListener {
public:
	PhonePinball();
	virtual ~PhonePinball() override;

	void onStart() override;
	void onStop() override;

	// ---- public constants for static tables ---------------------------

	// Geometry (matches the rest of the arcade screens).
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 10;
	static constexpr lv_coord_t PlayfieldX = 0;
	static constexpr lv_coord_t PlayfieldY = 20;
	static constexpr lv_coord_t PlayfieldW = 160;
	static constexpr lv_coord_t PlayfieldH = 98;

	// Tick / physics constants.
	static constexpr uint32_t TickMs           = 33;
	static constexpr int16_t  Q8               = 256;
	static constexpr int16_t  GravityQ8        = 18;     // ~0.07 px/tick^2
	static constexpr int16_t  MaxComponentQ8  = 640;    // 2.5 px/tick clamp

	// Ball.
	static constexpr lv_coord_t BallSize   = 4;
	static constexpr int16_t    BallRadius = 2;          // BallSize / 2

	// Bumpers.
	static constexpr uint8_t BumperCount = 5;
	// Flipper geometry (pivots are immutable; tip y delta defines angle).
	static constexpr int16_t LeftPivotX  = 50;
	static constexpr int16_t RightPivotX = 110;
	static constexpr int16_t FlipperPivotY = 108;
	static constexpr int16_t FlipperLen    = 24;
	static constexpr int16_t FlipperRise   = 8;          // vertical tip delta

	static constexpr uint8_t FlipperDots   = 5;          // visual segments

	static constexpr uint8_t StartLives    = 3;
	static constexpr uint8_t MaxLives      = 5;

	// Score awards (kept in the header so unit-style invariants and
	// the implementation stay in lock-step).
	static constexpr uint16_t BumperScore  = 100;
	static constexpr uint16_t TopWallScore = 10;
	static constexpr uint16_t FlipperScore = 5;

	enum class GameState : uint8_t {
		Idle,
		Playing,
		Paused,
		GameOver,
	};

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudLeftLabel = nullptr;   // "BALL 1"
	lv_obj_t* titleLabel   = nullptr;   // "PINBALL"
	lv_obj_t* livesLabel   = nullptr;   // dim cream stars
	lv_obj_t* scoreLabel   = nullptr;   // numeric score

	// Playfield wraps the gameplay area in a 1-px MP_DIM border so the
	// ball reads as "in a chamber" against the synthwave wallpaper.
	lv_obj_t* playfield = nullptr;

	// Decorative floor pieces flanking the drain. They are visual only
	// (no extra collision -- the side walls handle the geometry).
	lv_obj_t* leftFloor  = nullptr;
	lv_obj_t* rightFloor = nullptr;

	// Bumpers: round disc + halo overlay used for the hit flash.
	lv_obj_t* bumperSprites[BumperCount];
	lv_obj_t* bumperHalos[BumperCount];

	// Flipper visual: a string of 3x3 dot sprites positioned along the
	// pivot->tip line.
	lv_obj_t* leftFlipperDots[FlipperDots];
	lv_obj_t* rightFlipperDots[FlipperDots];

	lv_obj_t* ball = nullptr;

	// Overlay used for "PRESS START" / "LAUNCH!" / "PAUSED" / "GAME OVER".
	lv_obj_t* overlayLabel = nullptr;

	// ---- game state ---------------------------------------------------
	GameState state = GameState::Idle;

	// Ball state (Q8 fixed-point). ballStuck = true when waiting on a
	// player launch (true at game start and after every life-loss).
	int32_t ballXQ8  = 0;
	int32_t ballYQ8  = 0;
	int16_t ballVxQ8 = 0;
	int16_t ballVyQ8 = 0;
	bool    ballStuck = true;

	// Flipper state. While "active" the tip is in the up position.
	bool leftActive  = false;
	bool rightActive = false;

	// Per-bumper hit-flash countdowns (ticks remaining; 0 = idle).
	uint8_t bumperFlashTicks[BumperCount];

	uint32_t score = 0;
	uint8_t  lives = StartLives;

	// Tiny xorshift PRNG so the launch nudge is repeatable per session
	// without dragging in `random()` from the platform.
	uint16_t rngState = 0xBEEFu;

	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildPlayfield();
	void buildBumpers();
	void buildFlippers();
	void buildBall();
	void buildOverlay();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startGame();
	void pauseGame();
	void resumeGame();
	void endGame();

	// ---- core game ops ------------------------------------------------
	void resetBall();              // park ball above bumpers, ballStuck
	void launchBall();             // give the parked ball an initial Vy
	void physicsStep();
	void integrate();
	void collideWalls();
	void collideBumpers();
	void collideFlippers();
	void clampSpeed();
	void loseLife();

	// Single circle-vs-line-segment collision used by both flippers.
	// Returns true if a collision was processed (so the caller can stop
	// looking once a flipper has reflected the ball this tick).
	bool collideOneFlipper(bool isLeft);

	// ---- rendering ----------------------------------------------------
	void render();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();
	void renderBumpers();
	void renderFlipper(bool isLeft);

	// Compute the current flipper tip in integer pixels, given the
	// flipper side and its active/rest state.
	void getFlipperTip(bool isLeft, int16_t& tipX, int16_t& tipY) const;

	// Static bumper geometry. Centre + radius live in the .cpp's table.
	static int16_t bumperCx(uint8_t i);
	static int16_t bumperCy(uint8_t i);
	static int16_t bumperRadius(uint8_t i);

	// ---- timers -------------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickTimerStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONEPINBALL_H
