#ifndef MAKERPHONE_PHONESLIDINGPUZZLE_H
#define MAKERPHONE_PHONESLIDINGPUZZLE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSlidingPuzzle (S80)
 *
 * Phase-N arcade entry: the classic 4x4 "fifteen-puzzle". The player
 * scrambles a 4x4 board of numbered tiles (1..15) plus one blank cell
 * and slides tiles into the blank to restore them to ascending order.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | MOVES 042   BEST 067   01:23      | <- HUD strip       (12 px)
 *   |        +------+------+------+------+
 *   |        |  1   |  2   |  3   |  4   |
 *   |        +------+------+------+------+
 *   |        |  5   |  6   |  7   |  8   |
 *   |        +------+------+------+------+    <- 4x4 board, 24x24 px
 *   |        |  9   | 10   | 11   | 12   |       cells, total 96x96
 *   |        +------+------+------+------+
 *   |        | 13   | 14   | 15   |      |
 *   |        +------+------+------+------+
 *   |   SLIDE              BACK          | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 / BTN_8       : move cursor up / down
 *   - BTN_4 / BTN_6       : move cursor left / right
 *   - BTN_LEFT / BTN_RIGHT: cursor left / right (alias)
 *   - BTN_5               : slide the highlighted tile into the blank
 *                           (only legal if the cursor sits on a tile
 *                           orthogonally adjacent to the blank)
 *   - BTN_ENTER (A)       : alias for BTN_5
 *   - BTN_R               : reshuffle the board (re-scramble at any
 *                           time -- cheap "I give up" reset)
 *   - BTN_BACK (B)        : pop back to PhoneGamesScreen
 *
 * State machine:
 *   Playing  -- normal gameplay. Cursor + slide.
 *   Won      -- the board is solved (tiles 1..15 in row-major order,
 *               blank at the bottom-right). Overlay shows "SOLVED!"
 *               + final move count + final time. BTN_5 / BTN_ENTER
 *               reshuffles for another go.
 *
 * Implementation notes:
 *   - 100% code-only -- every tile is a plain lv_obj rectangle with a
 *     pixelbasic7 label showing the tile number. No SPIFFS asset cost.
 *   - The shuffle is **always solvable**: instead of placing tiles
 *     randomly (which yields 50% unsolvable boards), we start from the
 *     solved state and apply 200 random legal slides. This guarantees
 *     reachability of the solved state by construction.
 *   - The cursor is a 1 px highlight border that lives on the tile at
 *     (cursorCol, cursorRow). Moving the cursor is decoupled from
 *     sliding -- the player frames the move first, then commits with
 *     BTN_5. This makes the game playable on a numpad-style keypad
 *     where holding directionals is awkward.
 *   - A "best moves" record is kept across plays in-memory (not
 *     persisted); resetting the device clears it, which matches every
 *     other Phone* game in the v1.0 roadmap.
 */
class PhoneSlidingPuzzle : public LVScreen, private InputListener {
public:
	PhoneSlidingPuzzle();
	virtual ~PhoneSlidingPuzzle() override;

	void onStart() override;
	void onStop() override;

	// Screen layout - matches the diagram above. 160 x 128 panel.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	// 4x4 board with 24 px tiles totals 96x96. Centred horizontally on
	// the 160 px panel (32 px margin) and vertically inside the
	// 128 - 32 = 96 px play band.
	static constexpr uint8_t BoardSize = 4;
	static constexpr uint8_t TileCount = BoardSize * BoardSize;
	static constexpr lv_coord_t TilePx = 24;
	static constexpr lv_coord_t BoardPx = TilePx * BoardSize;       // 96
	static constexpr lv_coord_t BoardOriginX = (160 - BoardPx) / 2; // 32

	// FieldYTop = StatusBarH + HudH = 22; FieldYBot = 128 - SoftKeyH = 118.
	// Available 96 px == BoardPx -> board sits flush below the HUD.
	static constexpr lv_coord_t BoardOriginY = StatusBarH + HudH;   // 22

	// Number of random legal slides used to scramble the solved board.
	// 200 is more than enough for a 4x4 to feel fully randomised while
	// keeping the scramble cost negligible (<1 ms on the ESP32).
	static constexpr uint16_t kScrambleSteps = 200;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudMovesLabel = nullptr;
	lv_obj_t* hudBestLabel  = nullptr;
	lv_obj_t* hudTimerLabel = nullptr;
	lv_obj_t* overlayLabel  = nullptr;

	// One rectangle + one label per tile slot. Both arrays index by
	// **slot** (= row * 4 + col), so [0] is the top-left slot. The
	// blank slot's tile sprite is hidden via LV_OBJ_FLAG_HIDDEN rather
	// than deleted, which keeps the rebuild path single-shot.
	lv_obj_t* tileSprites[TileCount];
	lv_obj_t* tileLabels[TileCount];

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Playing,
		Won,
	};
	GameState state = GameState::Playing;

	// board[i] = which tile number lives in slot i, or 0 for the blank.
	// Tile numbers are 1..15. The solved layout is:
	//   [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0 ].
	uint8_t board[TileCount];

	// Slot index of the blank cell, kept in sync with board[].
	uint8_t blankSlot = TileCount - 1;

	// Cursor position in tile coordinates (0..BoardSize-1).
	uint8_t cursorCol = 0;
	uint8_t cursorRow = 0;

	// Bookkeeping.
	uint16_t moves        = 0;
	uint16_t bestMoves    = 0;    // 0 = no record yet
	uint32_t startMillis  = 0;
	uint32_t finishMillis = 0;    // captured on Won
	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildTiles();

	// ---- state transitions --------------------------------------------
	void resetSolved();
	void scramble();
	void newGame();
	void slideAt(uint8_t col, uint8_t row);
	bool isSolved() const;
	void winMatch();

	// ---- helpers ------------------------------------------------------
	uint8_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint8_t>(row * BoardSize + col);
	}
	uint8_t colOf(uint8_t slot) const {
		return static_cast<uint8_t>(slot % BoardSize);
	}
	uint8_t rowOf(uint8_t slot) const {
		return static_cast<uint8_t>(slot / BoardSize);
	}
	bool isAdjacent(uint8_t a, uint8_t b) const;

	// ---- rendering ----------------------------------------------------
	void renderAllTiles();
	void renderTile(uint8_t slot);
	void renderCursor();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	// ---- timer --------------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESLIDINGPUZZLE_H
