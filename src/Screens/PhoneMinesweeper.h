#ifndef MAKERPHONE_PHONEMINESWEEPER_H
#define MAKERPHONE_PHONEMINESWEEPER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneMinesweeper (S79)
 *
 * Phase-N arcade entry: a keypad-driven Minesweeper with three
 * difficulty packs sized to the 160 x 128 panel.
 *
 *   Difficulty   Grid    Cell    Mines   Field WxH
 *   ----------- ------- ------- ------- ----------
 *   EASY         8 x 6   12 px     6      96 x 72
 *   MEDIUM      10 x 7   10 px    12     100 x 70
 *   HARD        12 x 8    8 px    18      96 x 64
 *
 * Each difficulty keeps the playfield centred horizontally between the
 * status bar (top) and soft-key bar (bottom), with a 10 px HUD strip
 * above showing the difficulty + mine counter + timer.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar (10 px)
 *   |  EASY    *06    012s              | <- HUD strip (10 px)
 *   |     +--------------------------+   |
 *   |     | . . . . . . . .          |   | <- 8 x 6 grid (96 x 72)
 *   |     | . [3] . . . . . .        |   |
 *   |     | . . X . . . . .          |   |
 *   |     | . . . . . . . .          |   |
 *   |     | . . . . . . . .          |   |
 *   |     | . . . . . . . .          |   |
 *   |     +--------------------------+   |
 *   |   DIG               BACK           | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 / BTN_8       : cursor up / down  (also BTN_LEFT for "rotate" feel)
 *   - BTN_4 / BTN_6       : cursor left / right (also BTN_LEFT / BTN_RIGHT)
 *   - BTN_LEFT / BTN_RIGHT: cursor left / right (alias for d-pad)
 *   - BTN_5               : reveal the cell under the cursor (a.k.a. "dig")
 *   - BTN_ENTER (A)       : reveal the cell under the cursor (alias for 5)
 *   - BTN_0               : toggle a flag on the cell under the cursor
 *   - BTN_R               : cycle difficulty (only on Idle / GameOver / Won)
 *   - BTN_BACK (B)        : pop back to PhoneGamesScreen
 *
 * State machine:
 *   Idle      -- "PRESS DIG" overlay, board reset, mines NOT yet placed
 *                so the player's first dig is always safe.
 *                BTN_5 / BTN_ENTER on a cell -> place mines around the
 *                first-pick safe square, transition -> Playing, reveal.
 *   Playing   -- normal gameplay. Cursor + dig + flag.
 *   Won       -- every non-mine cell is revealed. All mines auto-flag,
 *                overlay shows "CLEAR!" + final time.
 *   GameOver  -- player dug a mine. The struck mine flashes accent, the
 *                rest reveal as silhouettes, overlay shows "BOOM" +
 *                final time.
 *
 * Implementation notes:
 *   - 100% code-only -- every cell is a plain lv_obj rectangle with a
 *     pixelbasic7 label (or no label for blank / unrevealed cells).
 *     No SPIFFS asset cost.
 *   - Adjacent-mine numbers are coloured by classic Minesweeper hues
 *     (1 cyan, 2 green, 3 orange, 4 magenta, 5+ accent). The colours
 *     ride the MAKERphone palette so the screen still slots in beside
 *     PhoneTetris / PhoneBubbleSmile without a visual seam.
 *   - The flood-fill that opens up zero-adjacent regions runs
 *     iteratively over a small fixed-size queue (kMaxQueue) so we can
 *     handle the 12x8 = 96-cell HARD board without recursion blowing
 *     the (small) Arduino stack.
 *   - First-click safety: mines are placed *after* the first dig and
 *     are guaranteed not to overlap the first-pick cell or any of its
 *     8 neighbours, so the first reveal always opens a meaningful
 *     region. This matches modern Minesweeper variants (and the Nokia
 *     classic).
 *   - The mine counter shown in the HUD is "remaining mines" =
 *     totalMines - flaggedCells, not "mines left to find". Flagging a
 *     blank cell still decrements the counter (classic semantics).
 */
class PhoneMinesweeper : public LVScreen, private InputListener {
public:
	PhoneMinesweeper();
	virtual ~PhoneMinesweeper() override;

	void onStart() override;
	void onStop() override;

	// Screen layout - matches the diagram above. 160 x 128 panel.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;
	static constexpr lv_coord_t FieldYTop  = 22;     // first row of cells
	static constexpr lv_coord_t FieldYBot  = 118;    // last row + 1 (= 96 px)

	// Maximum board dimensions (HARD = 12 x 8 = 96 cells). Sized so we
	// can statically allocate the cell + label arrays without churning
	// the heap every reset. Bumping this beyond 12 x 8 will overflow
	// the 160 px width with the smallest practical 8 px cells.
	static constexpr uint8_t MaxCols = 12;
	static constexpr uint8_t MaxRows = 8;
	static constexpr uint8_t MaxCells = MaxCols * MaxRows;

	// Maximum BFS queue size for the flood-fill. Bounded by the largest
	// possible board.
	static constexpr uint8_t kMaxQueue = MaxCells;

	// Difficulty descriptor (small, value-type).
	struct DifficultyInfo {
		const char* name;
		uint8_t     cols;
		uint8_t     rows;
		uint8_t     cell;     // pixel size of one cell
		uint8_t     mines;
	};
	static constexpr uint8_t kDifficultyCount = 3;
	static const DifficultyInfo Difficulties[kDifficultyCount];

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudDiffLabel  = nullptr;
	lv_obj_t* hudMineLabel  = nullptr;
	lv_obj_t* hudTimerLabel = nullptr;
	lv_obj_t* overlayLabel  = nullptr;

	// One rectangle + one label per cell. Both arrays are MaxCells long;
	// only the first (cols * rows) entries are populated for the active
	// difficulty.
	lv_obj_t* cellSprites[MaxCells];
	lv_obj_t* cellLabels[MaxCells];

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Playing,
		Won,
		GameOver,
	};
	GameState state = GameState::Idle;

	// Difficulty index into Difficulties[].
	uint8_t difficulty = 0;

	// Per-cell flat arrays. cellMine[i] = is the cell a mine.
	// cellRevealed[i] = is the cell revealed. cellFlagged[i] = is the
	// cell flagged. cellAdjacent[i] = number of adjacent mines (0..8).
	uint8_t cellMine[MaxCells];
	uint8_t cellRevealed[MaxCells];
	uint8_t cellFlagged[MaxCells];
	uint8_t cellAdjacent[MaxCells];

	// Cursor position in cell coordinates.
	uint8_t cursorCol = 0;
	uint8_t cursorRow = 0;

	// First-click guard: while true (Idle), the next dig will place
	// mines and transition -> Playing.
	bool needsMinePlacement = true;

	// Bookkeeping.
	uint8_t  totalMines       = 0;
	uint8_t  flagsPlaced      = 0;
	uint16_t revealedCount    = 0;
	uint8_t  explodedIndex    = 0xFF;

	// Timing - the timer ticks each second once Playing starts.
	uint32_t startMillis      = 0;
	uint32_t finishMillis     = 0;     // captured on Won / GameOver
	lv_timer_t* tickTimer     = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void rebuildField();   // wipes + rebuilds cell sprites for current difficulty

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void cycleDifficulty();
	void placeMinesAfterFirstDig(uint8_t safeCol, uint8_t safeRow);
	void digCell(uint8_t col, uint8_t row);
	void floodReveal(uint8_t col, uint8_t row);
	void toggleFlag(uint8_t col, uint8_t row);
	void revealAllMines();
	void winMatch();
	void loseMatch(uint8_t struckIndex);

	// ---- helpers ------------------------------------------------------
	uint8_t cols() const { return Difficulties[difficulty].cols; }
	uint8_t rows() const { return Difficulties[difficulty].rows; }
	uint8_t cellPx() const { return Difficulties[difficulty].cell; }
	uint8_t cellCount() const {
		return Difficulties[difficulty].cols * Difficulties[difficulty].rows;
	}
	uint16_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint16_t>(row) * cols() + col;
	}
	bool inBounds(int8_t col, int8_t row) const {
		return col >= 0 && col < static_cast<int8_t>(cols())
		    && row >= 0 && row < static_cast<int8_t>(rows());
	}
	uint8_t countAdjacentMines(uint8_t col, uint8_t row) const;
	uint16_t fieldOriginX() const;
	uint16_t fieldOriginY() const;

	// ---- rendering ----------------------------------------------------
	void renderAllCells();
	void renderCell(uint8_t col, uint8_t row);
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

#endif // MAKERPHONE_PHONEMINESWEEPER_H
