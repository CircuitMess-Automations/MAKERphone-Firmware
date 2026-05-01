#ifndef MAKERPHONE_PHONEBOUNCE_H
#define MAKERPHONE_PHONEBOUNCE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneBounce (S73)
 *
 * A bouncy-ball platformer in the spirit of the Nokia "Bounce" classic
 * the roadmap calls out. The ball is pulled down by gravity, the player
 * thrusts it sideways, and a single tap jumps if (and only if) the ball
 * is in contact with a platform. The goal is to reach the right edge of
 * a single short side-scrolling level; falling into a gap kills the run.
 *
 * Why this fits in one session:
 *   - Code-only, no SPIFFS asset cost. The level is a 1-D array of tile
 *     heights (uint8_t per column) baked into the .cpp. The ball is a
 *     single round lv_obj. Platforms are recycled lv_obj rectangles
 *     painted from whichever level columns are currently on-screen.
 *   - No animations beyond the per-tick repaint. Everything else (camera,
 *     gravity, jump arc) is just integer math at 30 Hz.
 *
 * Layout (160 x 128):
 *   - PhoneStatusBar at the top  (10 px).
 *   - Title "BOUNCE"             (10 px).
 *   - Playfield                  (98 px tall, 160 px wide).
 *   - PhoneSoftKeyBar at bottom  (10 px).
 *
 * The playfield is laid out in 8 px tiles, so the visible window is 20
 * tiles wide (160 / 8) and 12 tiles tall (96 / 8). The level itself is
 * `LevelTiles` columns long; the camera scrolls so the ball stays in
 * the central third of the screen.
 *
 * Controls (mirrors the chord every other Phone* game uses):
 *   - BTN_4 / BTN_LEFT  : brake / reverse thrust.
 *   - BTN_6 / BTN_RIGHT : forward thrust.
 *   - BTN_2 / BTN_5     : jump (only if grounded).
 *   - BTN_ENTER (A)     : start / pause / resume / restart after death.
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- "PRESS START" overlay, ball parked at level start.
 *                ENTER -> Playing.
 *   Playing   -- physics tick (30 Hz), input drives thrust + jump.
 *                Reaching the goal column flips to Won. Falling below
 *                the bottom of the playfield flips to GameOver.
 *   Paused    -- tick suspended, "PAUSED" overlay, ENTER -> Playing.
 *   GameOver  -- "GAME OVER" overlay, ENTER -> Idle (restart).
 *   Won       -- "GOAL!" overlay + final score, ENTER -> Idle.
 *
 * Scoring:
 *   - Distance: 1 point per tile of horizontal progress recorded as the
 *     ball clears a new column for the first time. Stays earned even if
 *     the ball later moves backwards.
 *   - Time bonus: on Won, +max(0, 600 - elapsed_seconds * 10).
 */
class PhoneBounce : public LVScreen, private InputListener {
public:
	PhoneBounce();
	virtual ~PhoneBounce() override;

	void onStart() override;
	void onStop() override;

	// ---- public constants used by the .cpp's static tables -------------
	static constexpr lv_coord_t TileSize  = 8;     // pixels per tile
	static constexpr uint8_t    ViewCols  = 20;    // 160 / 8
	static constexpr uint8_t    ViewRows  = 12;    // 96  / 8
	static constexpr uint16_t   LevelTiles = 56;   // total columns in the level

	// Geometry constants.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t TitleH     = 10;
	static constexpr lv_coord_t PlayfieldY = 20;          // status + title
	static constexpr lv_coord_t PlayfieldH = 98;          // 128 - 20 - 10

	// Tick / physics constants. Q8 fixed-point: positions/velocities are
	// stored as `pixels * 256` so a single 30 Hz tick can apply a sub-px
	// gravity without using floats. Using fixed-point also keeps the
	// physics deterministic across compilers.
	static constexpr uint32_t   TickMs       = 33;        // ~30 Hz
	static constexpr int16_t    Q8           = 256;
	static constexpr int16_t    GravityQ8    = 24;        // px/tick^2 in Q8
	static constexpr int16_t    ThrustQ8     = 28;        // accel per tick
	static constexpr int16_t    DragNumQ8    = 240;       // *240/256 friction
	static constexpr int16_t    MaxVxQ8      = 6 * 256;   // 6 px/tick
	static constexpr int16_t    MaxVyQ8      = 8 * 256;   // 8 px/tick
	static constexpr int16_t    JumpVyQ8     = -7 * 256;  // upward kick
	static constexpr int16_t    BounceLossN  = 180;       // *180/256 on bounce
	static constexpr int16_t    BounceCutoff = 1 * 256;   // |vy| under this == rest
	static constexpr int16_t    BallRadius   = 3;         // px

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* titleLabel  = nullptr;
	lv_obj_t* scoreLabel  = nullptr;

	// Playfield is its own clipped sub-container so we can pin platform
	// rectangles to absolute screen-space coordinates without leaking
	// outside the play area when they scroll past the edges.
	lv_obj_t* playfield   = nullptr;

	// Pool of recycled tile rectangles -- one per visible column. Sized
	// (ViewCols + 1) so a partially-scrolled-out column at the left
	// keeps a sprite while the next column scrolls in on the right.
	static constexpr uint8_t TileSpritePoolSize = ViewCols + 1;
	lv_obj_t* tileSprites[TileSpritePoolSize];

	// Goal flag (drawn at the right edge of the level).
	lv_obj_t* goalFlag    = nullptr;

	// Ball -- a single round lv_obj_t.
	lv_obj_t* ball        = nullptr;

	// Overlay label used for "PRESS START", "PAUSED", "GAME OVER", "GOAL!".
	lv_obj_t* overlayLabel = nullptr;

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Playing,
		Paused,
		GameOver,
		Won,
	};
	GameState state = GameState::Idle;

	// Ball position / velocity in Q8 pixel-space, expressed in *world*
	// coords (i.e. relative to the level's left edge, not the screen).
	int32_t ballXQ8 = 0;
	int32_t ballYQ8 = 0;
	int16_t ballVxQ8 = 0;
	int16_t ballVyQ8 = 0;
	bool    grounded = false;

	// Camera (left edge of the playfield, in world px). Always >= 0 and
	// <= LevelTiles*TileSize - 160.
	int16_t cameraX = 0;

	// Score / progress.
	uint32_t score          = 0;
	uint16_t furthestColumn = 0;   // highest tile column the ball cleared
	uint32_t startTickMs    = 0;   // millis() when this run started

	// Held-key flags so thrust is continuous while a key is held. The
	// Chatter input layer fires both press and release events; we mirror
	// them here so the per-tick physics step can apply thrust each tick.
	bool holdLeft  = false;
	bool holdRight = false;

	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildTitle();
	void buildPlayfield();
	void buildBall();
	void buildOverlay();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startGame();
	void pauseGame();
	void resumeGame();
	void endGame();
	void winGame();

	// ---- physics ------------------------------------------------------
	void resetBall();
	void physicsStep();
	uint8_t levelHeightAt(uint16_t column) const;  // height in tiles, 0..ViewRows
	bool    columnSolid(uint16_t column, uint8_t row) const;

	// ---- timers -------------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickTimerStatic(lv_timer_t* timer);

	// ---- rendering ----------------------------------------------------
	void render();           // sync ball + camera + tile sprites + goal
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONEBOUNCE_H
