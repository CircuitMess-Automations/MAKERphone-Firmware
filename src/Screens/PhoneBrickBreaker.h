#ifndef MAKERPHONE_PHONEBRICKBREAKER_H
#define MAKERPHONE_PHONEBRICKBREAKER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneBrickBreaker (S75)
 *
 * Phase-N arcade entry: the classic brick-breaker (a.k.a. Arkanoid /
 * Breakout) cast in the MAKERphone palette. A paddle sits at the
 * bottom of the playfield, a ball bounces around the well, and the
 * player has to destroy a 5x8 grid of coloured bricks while keeping
 * the ball in play. A handful of bricks drop **power-ups** when
 * destroyed -- catch one with the paddle to trigger the effect.
 *
 * Layout (160 x 128):
 *   - PhoneStatusBar at the top  (10 px).
 *   - HUD strip                  (10 px) -- "L1   BRICK   *** 042"
 *   - Playfield                  (98 px tall, 160 px wide).
 *   - PhoneSoftKeyBar at bottom  (10 px).
 *
 * Bricks: 8 columns x 5 rows of 18 x 5 px bricks (with 1 px gaps),
 * pinned to the top of the playfield. Five colour rows so the visual
 * read is clean even on the 160 px panel. A subset of bricks are
 * marked as power-up bricks at level start (deterministic per level
 * seed, so each restart of level 1 gives the same drops). Destroying
 * a power-up brick spawns a falling power-up token at the brick's
 * position; catching the token with the paddle activates the effect.
 *
 * Power-ups (three flavours, all timed or one-shot):
 *   - WIDE  (W) -- paddle widens to 36 px for 8 seconds, then snaps
 *                  back to 24 px on the next tick.
 *   - SLOW  (S) -- ball speed scaled to 60% for 8 seconds.
 *   - LIFE  (L) -- +1 life (capped at 5; over-cap pickups award a
 *                  small score bonus instead).
 *
 * Power-ups never overlap: catching a second WIDE while the first
 * is active resets the timer; catching a SLOW while WIDE is active
 * runs both effects in parallel via independent timer counters.
 *
 * Controls (mirrors the chord every other Phone* arcade game uses):
 *   - BTN_4 / BTN_LEFT  : slide paddle left.
 *   - BTN_6 / BTN_RIGHT : slide paddle right.
 *   - BTN_ENTER (A)     : launch ball / pause / resume / restart.
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- "PRESS START" overlay, ball parked above paddle.
 *                ENTER -> Playing (ball stuck until next ENTER).
 *   Playing   -- physics tick (30 Hz). Ball stuck to paddle until
 *                first launch, after life-loss respawn, etc.
 *   Paused    -- tick suspended, "PAUSED" overlay.
 *                ENTER -> Playing.
 *   GameOver  -- "GAME OVER" overlay (lives ran out).
 *                ENTER -> Idle (full reset).
 *   Cleared   -- "WELL DONE!" overlay (every brick gone).
 *                ENTER -> Idle (full reset).
 *
 * Scoring:
 *   - Destroying a brick: 10 points * (1 + rowFromTop). Top rows
 *     are worth more, classic-Breakout style.
 *   - Catching a LIFE token while at the cap: +50 points consolation.
 *
 * Implementation notes:
 *   - 100% code-only -- no SPIFFS asset cost. Everything is a tiny
 *     lv_obj rectangle.
 *   - Ball position / velocity in Q8 fixed-point (pixels << 8) so
 *     collision bookkeeping stays integer-only on the ESP32.
 *   - Brick visuals are recycled lv_obj rectangles, one per brick
 *     slot. Visibility toggles instead of allocating/freeing.
 *   - Power-up token sprites pool size = MaxActivePowerUps, hidden
 *     when not in use.
 */
class PhoneBrickBreaker : public LVScreen, private InputListener {
public:
	PhoneBrickBreaker();
	virtual ~PhoneBrickBreaker() override;

	void onStart() override;
	void onStop() override;

	// ---- public constants used by the .cpp's static tables -------------

	// Brick grid.
	static constexpr uint8_t  BrickCols     = 8;
	static constexpr uint8_t  BrickRows     = 5;
	static constexpr uint8_t  BrickCount    = BrickCols * BrickRows;  // 40
	static constexpr lv_coord_t BrickW      = 18;
	static constexpr lv_coord_t BrickH      = 5;
	static constexpr lv_coord_t BrickGapX   = 1;
	static constexpr lv_coord_t BrickGapY   = 1;

	// Geometry (matches the rest of the arcade screens).
	static constexpr lv_coord_t StatusBarH  = 10;
	static constexpr lv_coord_t SoftKeyH    = 10;
	static constexpr lv_coord_t HudY        = 10;
	static constexpr lv_coord_t HudH        = 10;
	static constexpr lv_coord_t PlayfieldY  = 20;
	static constexpr lv_coord_t PlayfieldH  = 98;     // 128 - 10 - 10 - 10
	static constexpr lv_coord_t PlayfieldX  = 0;
	static constexpr lv_coord_t PlayfieldW  = 160;

	static constexpr lv_coord_t BrickAreaX  = 8;      // left edge of grid
	static constexpr lv_coord_t BrickAreaY  = 26;     // top edge of grid

	// Paddle.
	static constexpr lv_coord_t PaddleY        = 110;  // top edge of paddle
	static constexpr lv_coord_t PaddleH        = 3;
	static constexpr lv_coord_t PaddleNormalW  = 24;
	static constexpr lv_coord_t PaddleWideW    = 36;
	static constexpr int16_t    PaddleSpeed    = 3;    // px per tick

	// Ball (3x3 square -- circular fudge is wasted at this size).
	static constexpr lv_coord_t BallSize       = 3;

	// Tick / physics constants.
	static constexpr uint32_t TickMs           = 33;
	static constexpr int16_t  Q8               = 256;
	static constexpr int16_t  BallSpeedQ8      = 384;  // ~1.5 px/tick total
	static constexpr int16_t  BallMaxComponent = 512;  // clamp magnitude

	// Power-up tokens.
	static constexpr uint8_t    MaxActivePowerUps = 4;
	static constexpr lv_coord_t TokenSize         = 8;   // 8x6 px token
	static constexpr lv_coord_t TokenH            = 6;
	static constexpr int16_t    TokenFallQ8       = 256; // 1 px/tick
	static constexpr uint8_t    PowerUpDropPct    = 18;  // % chance per brick

	// Effect durations (in ticks, 30 Hz so 240 = 8 s).
	static constexpr uint16_t WideTicks  = 240;
	static constexpr uint16_t SlowTicks  = 240;

	static constexpr uint8_t  StartLives = 3;
	static constexpr uint8_t  MaxLives   = 5;

	enum class PowerUpKind : uint8_t {
		None = 0,
		Wide,
		Slow,
		Life,
	};

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudLeftLabel  = nullptr;   // "L1"
	lv_obj_t* titleLabel    = nullptr;   // "BRICK"
	lv_obj_t* livesLabel    = nullptr;   // dim hearts
	lv_obj_t* scoreLabel    = nullptr;   // numeric score

	// Playfield wraps the gameplay area in a 1-px MP_DIM border so the
	// ball reads as "in a chamber" against the synthwave wallpaper.
	lv_obj_t* playfield     = nullptr;

	// Bricks: pool of BrickCount rectangles, one per slot. Hidden when
	// the brick has been destroyed.
	lv_obj_t* brickSprites[BrickCount];

	lv_obj_t* paddle        = nullptr;
	lv_obj_t* ball          = nullptr;

	// Power-up token sprite pool.
	lv_obj_t* tokenSprites[MaxActivePowerUps];
	lv_obj_t* tokenLabels[MaxActivePowerUps];

	// Overlay used for "PRESS START" / "PAUSED" / "GAME OVER" / "WELL
	// DONE!".
	lv_obj_t* overlayLabel  = nullptr;

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Playing,
		Paused,
		GameOver,
		Cleared,
	};
	GameState state = GameState::Idle;

	// Brick alive bitmap + power-up plan. brickAlive[i] = true if brick
	// i is still on the board. brickPowerUp[i] != None if destroying
	// brick i should drop a token. The plan is computed once per level
	// (deterministically from `levelSeed`).
	bool        brickAlive[BrickCount];
	PowerUpKind brickPowerUp[BrickCount];

	// Paddle (centre X in pixels; width follows currentPaddleW).
	int16_t paddleX        = 80;          // centre X
	int16_t currentPaddleW = PaddleNormalW;

	// Ball (Q8 fixed-point in screen coords).
	int32_t ballXQ8  = 0;
	int32_t ballYQ8  = 0;
	int16_t ballVxQ8 = 0;
	int16_t ballVyQ8 = 0;
	bool    ballStuck = true;            // true when waiting on ENTER to launch

	// Power-up tokens currently in flight.
	struct Token {
		bool        active = false;
		PowerUpKind kind   = PowerUpKind::None;
		int32_t     xQ8    = 0;
		int32_t     yQ8    = 0;
	};
	Token tokens[MaxActivePowerUps];

	// Active effects (tick countdowns; 0 = inactive).
	uint16_t wideTicksLeft = 0;
	uint16_t slowTicksLeft = 0;

	// Score / progress.
	uint32_t score           = 0;
	uint16_t bricksRemaining = 0;
	uint8_t  lives           = StartLives;
	uint16_t levelSeed       = 0xC0DEu;   // deterministic power-up plan

	bool holdLeft  = false;
	bool holdRight = false;

	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildPlayfield();
	void buildBricks();
	void buildPaddle();
	void buildBall();
	void buildTokens();
	void buildOverlay();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startGame();
	void pauseGame();
	void resumeGame();
	void endGame();
	void clearedAll();

	// ---- core game ops ------------------------------------------------
	void planLevel();              // populate brickAlive + brickPowerUp
	void resetPaddleAndBall();     // park ball above paddle, ball stuck
	void launchBall();             // give the stuck ball an initial velocity
	void physicsStep();
	void updatePaddle();
	void updateBall();
	void updateTokens();
	void tickEffects();
	void destroyBrick(uint8_t idx);
	void spawnToken(PowerUpKind kind, lv_coord_t x, lv_coord_t y);
	void applyPowerUp(PowerUpKind kind);
	void loseLife();

	// ---- rendering ----------------------------------------------------
	void render();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();
	void renderBrick(uint8_t idx);
	void renderToken(uint8_t idx);
	lv_color_t brickColor(uint8_t row) const;
	lv_color_t tokenColor(PowerUpKind kind) const;

	// ---- timers -------------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickTimerStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONEBRICKBREAKER_H
