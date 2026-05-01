#ifndef MAKERPHONE_PHONETETRIS_H
#define MAKERPHONE_PHONETETRIS_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneTetris (S71)
 *
 * Phase-N kick-off: the first proper retro arcade game in MAKERphone's
 * `PhoneGames` carousel. Pure code-only Tetris -- no SPIFFS asset
 * cost, no game-engine dependency, just an LVScreen with a 10x16
 * playfield and the seven canonical tetrominoes.
 *
 * Why a 10x16 well (not the canonical 10x20)?
 *   The 160x128 panel only has ~96 px of clean vertical space between
 *   the 10 px PhoneStatusBar and the 10 px PhoneSoftKeyBar. A 5 px
 *   cell -- the smallest size where the seven tetromino silhouettes
 *   are still legibly distinct on this screen -- caps the height at
 *   16 rows. The narrower well makes early lines slightly easier; the
 *   level-up curve is tuned to compensate (S72 will tighten it).
 *
 * State machine:
 *   Idle      -- "PRESS START" overlay, board cleared. ENTER -> Playing.
 *   Playing   -- piece ticking down, input drives moves and rotation.
 *   Paused    -- "PAUSED" overlay, tick stopped. ENTER -> Playing.
 *   LineClear -- rows flash white for ~150 ms, no input. -> Playing.
 *   GameOver  -- "GAME OVER" overlay, ENTER -> Idle (restart).
 *
 * Controls (the same chord every other Phone* screen uses):
 *   - BTN_4 / BTN_LEFT  : move piece left (one cell).
 *   - BTN_6 / BTN_RIGHT : move piece right (one cell).
 *   - BTN_8             : soft drop (one cell down on each press).
 *   - BTN_2             : rotate clockwise (no wall kicks; rejected
 *                         silently if the rotated piece would collide).
 *   - BTN_5             : hard drop (slam piece to the bottom and lock).
 *   - BTN_ENTER (A)     : start / pause / resume / restart.
 *   - BTN_L (L bumper)  : same as BTN_2 (rotate) -- ergonomic alias.
 *   - BTN_R (R bumper)  : same as BTN_5 (hard drop) -- ergonomic alias.
 *   - BTN_BACK (B)      : exit the screen back to PhoneGamesScreen.
 *
 * Scoring (classic NES rule of thumb):
 *   - 1 line  = 100 * (level + 1)
 *   - 2 lines = 300 * (level + 1)
 *   - 3 lines = 500 * (level + 1)
 *   - 4 lines = 800 * (level + 1)
 * Level advances every 10 cleared lines. Drop interval is multiplied
 * by 0.85 per level (clamped at 80 ms).
 *
 * Implementation notes:
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar.
 *   - The board view is rendered as a 10x16 grid of 5x5 lv_obj
 *     rectangles, allocated once in the constructor. Each cell is
 *     toggled visible/coloured per render() call.
 *   - The falling piece is composited into the same 10x16 cell grid
 *     during render() -- the locked board sits in `board[r][c]` and
 *     the active piece is stamped on top each frame.
 *   - Drop ticks use a single lv_timer (TickPeriodMs scaled to level).
 *   - Piece-bag generator (7-bag): the next-piece queue is filled by
 *     shuffling all seven tetrominoes once and dispensing them in
 *     order, then re-shuffling.
 */
class PhoneTetris : public LVScreen, private InputListener {
public:
	PhoneTetris();
	virtual ~PhoneTetris() override;

	void onStart() override;
	void onStop() override;

	// ---- public constants used by the .cpp's static tables --------------
	static constexpr uint8_t  Cols           = 10;   // playfield width
	static constexpr uint8_t  Rows           = 16;   // playfield height
	static constexpr uint8_t  CellPx         = 5;    // px per board cell
	static constexpr uint8_t  PreviewCellPx  = 4;    // px per next-preview cell
	static constexpr uint8_t  PieceCount     = 7;    // I, O, T, S, Z, J, L
	static constexpr uint8_t  PieceCells     = 4;    // every tetromino has 4 blocks
	static constexpr uint8_t  Rotations      = 4;    // 4 rotation states each

	// Geometry constants.
	static constexpr lv_coord_t StatusBarH   = 10;
	static constexpr lv_coord_t SoftKeyH     = 10;
	static constexpr lv_coord_t TitleY       = 12;
	static constexpr lv_coord_t BoardX       = 4;
	static constexpr lv_coord_t BoardY       = 24;
	static constexpr lv_coord_t BoardW       = Cols * CellPx;          // 50
	static constexpr lv_coord_t BoardH       = Rows * CellPx;          // 80

	// Sidebar (right of the playfield).
	static constexpr lv_coord_t SideX        = 58;
	static constexpr lv_coord_t SideW        = 160 - SideX - 2;        // 100

	// Tick / animation constants.
	static constexpr uint32_t   StartTickMs   = 600;   // level 0 drop interval
	static constexpr uint32_t   MinTickMs     = 80;    // floor at high levels
	static constexpr uint32_t   LineClearMs   = 150;   // row-flash duration
	static constexpr float      LevelScale    = 0.85f; // per-level decay
	static constexpr uint16_t   LinesPerLevel = 10;

private:
	// ---- LVGL node graph -----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Title + side-panel labels.
	lv_obj_t* titleLabel    = nullptr;
	lv_obj_t* nextCaption   = nullptr;
	lv_obj_t* scoreCaption  = nullptr;
	lv_obj_t* scoreLabel    = nullptr;
	lv_obj_t* linesCaption  = nullptr;
	lv_obj_t* linesLabel    = nullptr;
	lv_obj_t* levelCaption  = nullptr;
	lv_obj_t* levelLabel    = nullptr;

	// Board border + cells (10 cols x 16 rows of 5x5 rectangles).
	lv_obj_t* boardFrame    = nullptr;
	lv_obj_t* cells[Rows][Cols];

	// Next-piece preview frame + 4x4 preview cells (4 px each).
	lv_obj_t* previewFrame  = nullptr;
	lv_obj_t* previewCells[PieceCells][PieceCells];

	// Overlay label used for "PRESS START", "PAUSED", "GAME OVER".
	lv_obj_t* overlayLabel  = nullptr;

	// ---- game state ----------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Playing,
		Paused,
		LineClear,
		GameOver,
	};
	GameState state = GameState::Idle;

	// `0` = empty, `1..PieceCount` = piece colour index (1-based so 0 reads
	// as "empty" without a separate flag). Indexed [row][col] with row 0
	// at the top of the well.
	uint8_t board[Rows][Cols];

	// Falling piece descriptor.
	uint8_t pieceType  = 0;       // 0..6 index into kPieces
	uint8_t pieceRot   = 0;       // 0..3
	int8_t  pieceX     = 0;       // top-left of 4x4 piece box (col)
	int8_t  pieceY     = 0;       // top-left of 4x4 piece box (row, can be negative)
	uint8_t nextPiece  = 0;       // 0..6, the queued tetromino

	// 7-bag generator state.
	uint8_t bag[PieceCount];
	uint8_t bagIndex = PieceCount; // PieceCount == "bag empty, refill on next pull"

	// Score / progress.
	uint32_t score    = 0;
	uint32_t lines    = 0;
	uint8_t  level    = 0;

	// (S72) Last successful piece action was a rotation -- gating
	// condition for T-spin detection. Reset on spawn / move / drop,
	// set on a successful rotateCW. Survives a lock-without-movement
	// so T-spins triggered by a natural drop tick still register.
	bool lastActionRotation = false;
	// (S72) Latched T-spin verdict for the most recently locked piece.
	// Set in lockPiece(), consumed by awardLineScore via either the
	// no-lines path or the line-clear timer callback, then cleared.
	bool pendingTSpin       = false;

	// Line-clear animation: which rows are flashing.
	bool clearedRows[Rows];

	// LVGL timers.
	lv_timer_t* dropTimer       = nullptr; // Playing-only drop tick
	lv_timer_t* lineClearTimer  = nullptr; // one-shot line-flash collapse
	lv_timer_t* levelUpTimer    = nullptr; // (S72) one-shot HUD flash on level-up

	// ---- build helpers --------------------------------------------------
	void buildTitle();
	void buildBoard();
	void buildSidebar();
	void buildOverlay();

	// ---- state transitions ---------------------------------------------
	void enterIdle();         // post-construct + post-game-over reset
	void startGame();         // Idle -> Playing
	void pauseGame();         // Playing -> Paused
	void resumeGame();        // Paused -> Playing
	void endGame();           // Playing -> GameOver

	// ---- core game ops --------------------------------------------------
	void resetBoard();
	void refillBag();
	uint8_t pullFromBag();
	void spawnPiece();        // sets pieceType/Rot/X/Y from `nextPiece`
	bool collides(int8_t nx, int8_t ny, uint8_t nrot) const;
	void lockPiece();         // stamp piece into `board` and lock
	uint8_t findFullRows();   // populate clearedRows[], return count
	void awardLineScore(uint8_t cleared, bool tSpin);
	void collapseClearedRows(); // remove flashed rows + drop above

	void moveLeft();
	void moveRight();
	void rotateCW();
	bool softDrop();          // returns false if it locked
	void hardDrop();

	// (S72) T-spin detection (3-corner rule). Caller is expected to
	// invoke this from lockPiece() right after stamping the piece.
	bool detectTSpin() const;

	// (S72) Brief MP_ACCENT highlight on the LEVEL caption when the
	// player crosses a 10-line milestone.
	void startLevelUpFlash();
	void stopLevelUpTimer();
	static void onLevelUpTimerStatic(lv_timer_t* timer);

	// ---- timers --------------------------------------------------------
	void startDropTimer();
	void stopDropTimer();
	uint32_t currentDropMs() const;

	void startLineClearTimer();
	void stopLineClearTimer();

	static void onDropTimerStatic(lv_timer_t* timer);
	static void onLineClearTimerStatic(lv_timer_t* timer);

	// ---- rendering -----------------------------------------------------
	void render();            // full sync of board+piece -> cells[]
	void renderPreview();     // sync next-piece -> previewCells[]
	void refreshHud();        // score / lines / level labels
	void refreshSoftKeys();   // L/R captions follow the state machine
	void refreshOverlay();    // overlay label tracks the state machine

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONETETRIS_H
