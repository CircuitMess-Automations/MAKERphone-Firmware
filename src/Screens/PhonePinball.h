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
 * PhonePinball (S85 + S86)
 *
 * Phase-N arcade entry: a pinball game with a pair of flippers at the
 * bottom and bumpers above. Cast in the MAKERphone palette so it slots
 * in beside the rest of the Phone* arcade carousel without a visual
 * seam.
 *
 * S86 — PhonePinball+ adds:
 *   - A second table layout (Classic 5-bumper trio vs. Cluster 7-bumper
 *     dense field) selectable from the Idle screen with L/R bumpers.
 *   - A per-table leaderboard (top 3 scores, RAM-only -- survives game
 *     restarts during the same power cycle, resets on reboot).
 *
 * Layout (160 x 128):
 *   - PhoneStatusBar at the top   (10 px)
 *   - HUD strip                   (10 px) -- "PINBALL  *** 0042"
 *   - Playfield                   (98 px tall, 160 px wide)
 *   - PhoneSoftKeyBar at bottom   (10 px)
 *
 * Table layouts:
 *   - Classic (S85) -- 5 bumpers in a Williams-style top trio plus a
 *     pair of lower side bumpers. Forgiving, classic feel.
 *   - Cluster (S86) -- 7 bumpers in a denser arrangement with a centred
 *     lower "kicker" bumper that pushes drained shots back into play.
 *     Higher scoring potential, harder to predict.
 *
 * Controls:
 *   - BTN_4 / BTN_LEFT  : (Playing) hold = left flipper raised.
 *                         (Idle)    select previous table.
 *   - BTN_6 / BTN_RIGHT : (Playing) hold = right flipper raised.
 *                         (Idle)    select next table.
 *   - BTN_ENTER (A)     : launch ball / start / pause / resume / restart.
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- "PRESS START" overlay, ball parked above bumpers.
 *                L/R cycles through tables. ENTER -> Playing.
 *   Playing   -- physics tick (30 Hz). Ball stuck above bumpers
 *                until the player presses ENTER to launch (or after
 *                a respawn following a drain).
 *   Paused    -- tick suspended, "PAUSED" overlay.
 *                ENTER -> Playing.
 *   GameOver  -- overlay shows score + leaderboard best.
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
 *   - Bumper sprite pool is sized for the largest table; sprites past
 *     the active table's bumper count are hidden via `LV_OBJ_FLAG_HIDDEN`.
 *   - Ball position / velocity in Q8 fixed-point so collision
 *     bookkeeping stays integer-only on the ESP32. Gravity is a
 *     small constant Vy delta applied per physics tick.
 *   - Bumper / flipper collision uses circle-vs-circle and
 *     circle-vs-segment math, both performed in integer pixel space
 *     with a Newton's-method `isqrt` for the rare normalisation.
 *   - Leaderboard is a static array on the class so it persists across
 *     re-entries from PhoneGamesScreen during one power cycle.
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

	// Bumper sprite pool (sized to fit the largest table layout).
	static constexpr uint8_t BumperCount = 7;

	// Tables (S86).
	static constexpr uint8_t TableCount      = 2;
	static constexpr uint8_t LeaderboardSize = 3;

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

	enum class TableId : uint8_t {
		Classic = 0,
		Cluster = 1,
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

	// S86 -- table label ("TABLE: CLASSIC <>") on the Idle screen and
	// leaderboard preview ("TOP 1234 / 800 / 500").
	lv_obj_t* tableLabel       = nullptr;
	lv_obj_t* leaderboardLabel = nullptr;

	// ---- game state ---------------------------------------------------
	GameState state = GameState::Idle;
	TableId   currentTable = TableId::Classic;

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

	// S86 -- per-table top-LeaderboardSize scores. Static so the
	// leaderboard survives across re-entries to the screen during the
	// same power cycle. Cleared to zero at first use.
	static uint32_t leaderboard[TableCount][LeaderboardSize];

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

	// Bumper geometry for the active table. Centre + radius live in
	// per-table tables in the .cpp.
	int16_t bumperCx(uint8_t i) const;
	int16_t bumperCy(uint8_t i) const;
	int16_t bumperRadius(uint8_t i) const;
	uint8_t activeBumperCount() const;

	// ---- S86 helpers --------------------------------------------------
	static const char* tableName(TableId t);
	void cycleTable(int8_t delta);
	// Apply the current table layout to the bumper sprite pool: hide
	// unused, reposition active. Also refreshes the table + leaderboard
	// labels on the Idle screen.
	void applyTable();
	void refreshTableLabel();
	void refreshLeaderboardLabel();
	// Insert the supplied score into the leaderboard for the active
	// table, preserving descending order.
	void recordScore(uint32_t finalScore);
	uint32_t bestScoreForCurrentTable() const;

	// ---- timers -------------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickTimerStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONEPINBALL_H
