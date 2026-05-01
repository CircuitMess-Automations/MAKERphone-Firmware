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
 * PhoneBounce (S73 + S74)
 *
 * S73 shipped a single side-scrolling level with one bouncing ball.
 * S74 expands that into a four-level campaign with collectible rings:
 *
 *   - Four hand-built levels (VALLEY, RIDGE, CANYON, SUMMIT) selected
 *     in order. Winning level N auto-advances to level N+1; clearing
 *     the last level shows an ALL CLEAR! screen with the campaign total.
 *   - Each level seeds 5-7 collectible rings hovering above the path.
 *     Touching a ring with the ball banks +10 points and removes it.
 *     Rings reset on retry, but campaign score persists across levels
 *     within a single run so a strong start can pay off late.
 *
 * Constraints kept identical to S73:
 *   - Code-only -- no SPIFFS asset cost. Levels are uint8_t arrays of
 *     column heights and ring tables baked into the .cpp.
 *   - Tile pool sized to one screen-width worth of columns; recycled
 *     per render pass.
 *   - Ring sprite pool fixed at MaxRingsPerLevel (8) with the same
 *     "show / hide / position" recycling pattern.
 *
 * Layout (160 x 128):
 *   - PhoneStatusBar at the top  (10 px).
 *   - HUD strip                  (10 px) -- "L1/4   BOUNCE   42"
 *   - Playfield                  (98 px tall, 160 px wide).
 *   - PhoneSoftKeyBar at bottom  (10 px).
 *
 * Controls (mirrors the chord every other Phone* game uses):
 *   - BTN_4 / BTN_LEFT  : brake / reverse thrust.
 *   - BTN_6 / BTN_RIGHT : forward thrust.
 *   - BTN_2 / BTN_5     : jump (only if grounded).
 *   - BTN_ENTER (A)     : start / pause / resume / next-level / restart.
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- "PRESS START" overlay, ball parked at level start.
 *                ENTER -> Playing (currentLevelIdx = 0).
 *   Playing   -- physics tick (30 Hz), input drives thrust + jump.
 *                Reaching the goal column flips to Won. Falling below
 *                the bottom of the playfield flips to GameOver.
 *   Paused    -- tick suspended, "PAUSED" overlay, ENTER -> Playing.
 *   GameOver  -- "GAME OVER" overlay, ENTER -> retry the same level.
 *   Won       -- "GOAL!" overlay (per level), ENTER -> next level.
 *   Cleared   -- "ALL CLEAR!" overlay (after last level),
 *                ENTER -> Idle (reset to level 0).
 *
 * Scoring:
 *   - Distance: 1 point per tile of horizontal progress on each level.
 *   - Ring pickup: +RingScore (10) per ring.
 *   - Time bonus: on per-level Won, +max(0, 600 - elapsed_seconds * 10).
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

	// Maximum number of columns any single level can have. Each level's
	// actual length is stored in its LevelDef and may be smaller. The
	// constant is exposed publicly because static asserts in the .cpp
	// pin every per-level array to it.
	static constexpr uint16_t   LevelTiles  = 80;
	static constexpr uint8_t    LevelCount  = 4;
	static constexpr uint8_t    MaxRingsPerLevel = 8;

	// Geometry constants (unchanged from S73).
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t TitleH     = 10;
	static constexpr lv_coord_t PlayfieldY = 20;
	static constexpr lv_coord_t PlayfieldH = 98;

	// Tick / physics constants (unchanged from S73 -- gameplay feels the
	// same; only the level set changed). Q8 fixed-point: positions and
	// velocities are stored as `pixels * 256` so a single 30 Hz tick can
	// apply a sub-px gravity without using floats.
	static constexpr uint32_t   TickMs       = 33;
	static constexpr int16_t    Q8           = 256;
	static constexpr int16_t    GravityQ8    = 24;
	static constexpr int16_t    ThrustQ8     = 28;
	static constexpr int16_t    DragNumQ8    = 240;
	static constexpr int16_t    MaxVxQ8      = 6 * 256;
	static constexpr int16_t    MaxVyQ8      = 8 * 256;
	static constexpr int16_t    JumpVyQ8     = -7 * 256;
	static constexpr int16_t    BounceLossN  = 180;
	static constexpr int16_t    BounceCutoff = 1 * 256;
	static constexpr int16_t    BallRadius   = 3;

	// Ring graphical & gameplay constants.
	static constexpr int16_t    RingSize     = 7;        // px (square bbox, drawn round)
	static constexpr int16_t    RingPickupR  = 6;        // pickup half-extent
	static constexpr uint16_t   RingScore    = 10;       // points per pickup

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudLabel    = nullptr;     // "L1/4" left side of HUD strip
	lv_obj_t* titleLabel  = nullptr;     // "BOUNCE" centered
	lv_obj_t* scoreLabel  = nullptr;     // numeric score on the right

	// Playfield is its own clipped sub-container so we can pin platform
	// rectangles to absolute screen-space coordinates without leaking
	// outside the play area when they scroll past the edges.
	lv_obj_t* playfield   = nullptr;

	// Pool of recycled tile rectangles -- one per visible column. Sized
	// (ViewCols + 1) so a partially-scrolled-out column at the left
	// keeps a sprite while the next column scrolls in on the right.
	static constexpr uint8_t TileSpritePoolSize = ViewCols + 1;
	lv_obj_t* tileSprites[TileSpritePoolSize];

	// Pool of recycled ring sprites. Always MaxRingsPerLevel; rings the
	// current level doesn't use are simply hidden.
	lv_obj_t* ringSprites[MaxRingsPerLevel];

	// Goal flag (drawn at the right edge of the active level).
	lv_obj_t* goalFlag    = nullptr;

	// Ball -- a single round lv_obj_t.
	lv_obj_t* ball        = nullptr;

	// Overlay label used for "PRESS START", "PAUSED", "GAME OVER",
	// "GOAL!", "ALL CLEAR!".
	lv_obj_t* overlayLabel = nullptr;

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Playing,
		Paused,
		GameOver,
		Won,
		Cleared,
	};
	GameState state = GameState::Idle;

	// Ball position / velocity in Q8 pixel-space, expressed in *world*
	// coords. ballYQ8 is in absolute screen Y (so columnTopY can return
	// absolute screen Y too without a coordinate-space conversion in the
	// physics step).
	int32_t ballXQ8 = 0;
	int32_t ballYQ8 = 0;
	int16_t ballVxQ8 = 0;
	int16_t ballVyQ8 = 0;
	bool    grounded = false;

	// Camera (left edge of the playfield, in world px). Always >= 0 and
	// <= currentLength*TileSize - 160.
	int16_t cameraX = 0;

	// Score / progress.
	uint32_t score          = 0;
	uint16_t furthestColumn = 0;   // highest tile column the ball cleared
	uint32_t startTickMs    = 0;   // millis() when the current level started

	// Campaign / level progression.
	uint8_t  currentLevelIdx = 0;       // 0..LevelCount-1
	uint16_t ringsCollectedMask = 0;    // bit i = ring i picked up this level
	uint8_t  ringsThisLevel = 0;        // count of rings collected on this level

	// Held-key flags so thrust is continuous while a key is held.
	bool holdLeft  = false;
	bool holdRight = false;

	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildPlayfield();
	void buildBall();
	void buildOverlay();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startGame();             // fresh campaign run, level 0
	void startCurrentLevel();     // (re)spawn at currentLevelIdx
	void pauseGame();
	void resumeGame();
	void endGame();
	void winLevel();
	void clearedAll();

	// ---- physics + level helpers --------------------------------------
	void resetBall();
	void physicsStep();
	uint16_t   currentLevelLength() const;
	uint8_t    currentTileAt(uint16_t column) const;
	lv_coord_t columnTopY(uint16_t col) const;
	uint8_t    currentRingCount() const;
	void       checkRingPickup();

	// ---- timers -------------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickTimerStatic(lv_timer_t* timer);

	// ---- rendering ----------------------------------------------------
	void render();           // sync ball + camera + tile sprites + rings + goal
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONEBOUNCE_H
