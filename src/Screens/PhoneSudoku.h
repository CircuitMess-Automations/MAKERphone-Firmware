#ifndef MAKERPHONE_PHONESUDOKU_H
#define MAKERPHONE_PHONESUDOKU_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSudoku (S95)
 *
 * Phase-N arcade entry: a code-only 9x9 sudoku with three difficulty
 * packs (Easy, Medium, Hard). Every puzzle is generated on demand from
 * a known-good Latin-square base + a chain of grid-symmetry shuffles
 * (digit relabelling, row-within-band swaps, column-within-stack swaps,
 * band/stack swaps, optional transpose). Cells are then carved out at
 * a difficulty-dependent rate to produce the starting position. The
 * generator does not enforce uniqueness in the strict mathematical
 * sense -- a couple-of-second coffee-break game does not need it -- but
 * the carving is symmetric enough that solvable puzzles are the rule
 * rather than the exception.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | EASY  TIME 02:13   ERR 00         | <- HUD strip       (10 px)
 *   |     +-----------------------+      |
 *   |     |1 .|. .|. .|. 6 .|. .|9      |
 *   |     |. .|. .|. .|. .|. .|. .      | <- 9x9 board
 *   |     |. .|. .|. .|. .|. .|. .      |    10 px cells, 90x90 px
 *   |     |---+---+---|---+---+---|     |    centred horizontally
 *   |     |. .|. .|. .|. .|. .|. .      |    (35 px margin) and
 *   |     |. .|. .|. .|. .|. .|. .      |    pinned just under the HUD
 *   |     |. .|. .|. .|. .|. .|. .      |    (top edge at y = 20).
 *   |     |---+---+---|---+---+---|     |
 *   |     |. .|. .|. .|. .|. .|. .      |
 *   |     |. .|. .|. .|. .|. .|. .      |
 *   |     |. .|. .|. .|. .|. .|. .      |
 *   |     +-----------------------+      |
 *   |   PLACE              BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls (Playing state):
 *   - BTN_2 / BTN_8         : cursor up / down (wraps)
 *   - BTN_4 / BTN_6         : cursor left / right (wraps)
 *   - BTN_LEFT / BTN_RIGHT  : cursor left / right (alias)
 *   - BTN_1 .. BTN_9        : place that digit in the highlighted cell.
 *                             Locked "given" cells reject the input.
 *                             Wrong digits still go in (so the player
 *                             can change their mind), but flag the cell
 *                             with a sunset-orange tint and tick the
 *                             session error counter once per wrong
 *                             placement. Replacing a wrong digit with
 *                             the correct one clears the tint.
 *   - BTN_0 / BTN_ENTER     : clear the highlighted cell (no-op for
 *                             givens). BTN_ENTER also confirms a digit
 *                             when the picker is open (see Picker).
 *   - BTN_R                 : start a new puzzle at the current
 *                             difficulty. Useful when you want a fresh
 *                             grid without going back to the picker.
 *   - BTN_BACK (B)          : pop back to PhoneGamesScreen (Playing /
 *                             Solved) or the games screen (Picker).
 *
 * Controls (Picker state):
 *   - BTN_LEFT / BTN_RIGHT  : cycle Easy / Medium / Hard.
 *   - BTN_4 / BTN_6         : alias for cycling difficulty.
 *   - BTN_1 / BTN_2 / BTN_3 : jump straight to that difficulty.
 *   - BTN_5 / BTN_ENTER     : start a new puzzle at the chosen
 *                             difficulty.
 *   - BTN_BACK              : pop the screen.
 *
 * State machine:
 *   Picker        -- difficulty-pack selector overlay; the empty board
 *                    is visible behind it so the visual transition into
 *                    Playing feels instant rather than abrupt.
 *   Playing       -- the standard fill-the-board state. Timer ticks.
 *   Solved        -- every cell filled and matches the solution. The
 *                    HUD freezes the timer + a "SOLVED" overlay slides
 *                    in. The player can hit BTN_5 / BTN_ENTER to start
 *                    a fresh puzzle at the same difficulty, or BTN_R
 *                    to reroll, or BACK to pop.
 *
 * Implementation notes:
 *   - 100% code-only -- every cell is a plain lv_obj rectangle with a
 *     pixelbasic7 label so a 7x7 digit fits into a 10x10 cell with a
 *     single-pixel breathing border. No SPIFFS asset cost.
 *   - Sub-box dividers are drawn as four extra 1 px highlight bars
 *     stacked on top of the cell grid -- two horizontals at y=30/60
 *     and two verticals at x=30/60. The outer box border is the
 *     cyan-edged container that wraps the whole grid.
 *   - Generator: starts from the canonical "((row*3 + row/3 + col) %
 *     9) + 1" Latin-square pattern. Every transformation we apply is
 *     a sudoku-symmetry-preserving op, so the resulting grid is
 *     guaranteed to satisfy the row / column / box constraints. The
 *     transformations are each applied a handful of times so the same
 *     rng seed does not always produce the same grid.
 *   - Difficulty -> hole-count: Easy 40 / Medium 48 / Hard 54 cells
 *     blanked. We never blank more than 60 cells (which would leave
 *     fewer than the empirical 17-clue minimum that makes uniqueness
 *     even theoretically possible) -- and Hard at 54 leaves 27 clues,
 *     plenty to keep the puzzle solvable in the strong majority of
 *     cases.
 *   - Error tinting + counter: the player can place any digit in a
 *     non-given cell. If the digit does not match the solution, the
 *     cell tints sunset orange and the error counter ticks. Replacing
 *     it with the correct digit (or BTN_0 / clear) drops the tint.
 *     The counter is informational only -- the puzzle is still
 *     "solved" if the final board matches the solution, regardless of
 *     how many wrong attempts were made along the way.
 *   - Timer: a one-second LVGL repeating timer keeps the HUD ticking.
 *     Cancelled in onStop() and on screen pop so a stray fire does not
 *     race the destructor.
 */
class PhoneSudoku : public LVScreen, private InputListener {
public:
	PhoneSudoku();
	virtual ~PhoneSudoku() override;

	void onStart() override;
	void onStop() override;

	// 160x128 panel layout. Status bar 10 px, HUD 10 px, soft keys 10 px,
	// board 90x90 just under the HUD (y = 20 .. 110), with an 8 px gap
	// to the soft-key bar at the bottom.
	static constexpr lv_coord_t StatusBarH   = 10;
	static constexpr lv_coord_t SoftKeyH     = 10;
	static constexpr lv_coord_t HudY         = 10;
	static constexpr lv_coord_t HudH         = 10;

	static constexpr uint8_t    BoardSize    = 9;
	static constexpr uint8_t    CellCount    = BoardSize * BoardSize;
	static constexpr lv_coord_t CellPx       = 10;
	static constexpr lv_coord_t BoardPx      = CellPx * BoardSize;          // 90
	static constexpr lv_coord_t BoardOriginX = (160 - BoardPx) / 2;          // 35
	static constexpr lv_coord_t BoardOriginY = StatusBarH + HudH;            // 20

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudDifficultyLabel = nullptr;
	lv_obj_t* hudTimerLabel      = nullptr;
	lv_obj_t* hudErrLabel        = nullptr;

	lv_obj_t* boardContainer     = nullptr;
	lv_obj_t* cellSprites[CellCount];
	lv_obj_t* cellLabels[CellCount];

	lv_obj_t* pickerPanel        = nullptr;
	lv_obj_t* pickerEasyLabel    = nullptr;
	lv_obj_t* pickerMediumLabel  = nullptr;
	lv_obj_t* pickerHardLabel    = nullptr;
	lv_obj_t* pickerHintLabel    = nullptr;

	lv_obj_t* overlayLabel       = nullptr;

	// ---- game state ---------------------------------------------------
	enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2 };
	enum class State      : uint8_t { Picker, Playing, Solved };

	State      state          = State::Picker;
	Difficulty difficulty     = Difficulty::Easy;
	Difficulty pickerChoice   = Difficulty::Easy;

	// Solution[i] holds the canonical correct digit (1..9) for cell i.
	// puzzle[i] holds the current player-visible digit (0..9 where 0 is
	// blank). given[i] is true if this cell was a starting clue and so
	// is not editable.
	uint8_t solution[CellCount];
	uint8_t puzzle[CellCount];
	bool    given[CellCount];

	uint8_t  cursor      = 40;   // 0..80, defaults to centre cell
	uint16_t errors      = 0;
	uint32_t startMs     = 0;
	uint32_t solvedMs    = 0;

	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildBoard();
	void buildPicker();
	void buildOverlay();

	// ---- generator ----------------------------------------------------
	void generateSolution();         // fills `solution` with a valid grid
	void carvePuzzle();              // copies solution into puzzle and
	                                  // blanks cells based on difficulty

	// ---- state transitions --------------------------------------------
	void enterPicker();
	void startGame(Difficulty d);
	void onTickStatic();
	static void onTickStaticTrampoline(lv_timer_t* t);

	// ---- rendering ----------------------------------------------------
	void renderAllCells();
	void renderCell(uint8_t cell);
	void renderCursor();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();
	void refreshPicker();

	// ---- helpers ------------------------------------------------------
	bool        boardComplete() const;
	bool        boardMatchesSolution() const;
	void        cancelTickTimer();
	void        scheduleTickTimer();
	static const char* difficultyName(Difficulty d);
	static uint8_t     holesFor(Difficulty d);

	uint8_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint8_t>(row * BoardSize + col);
	}

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESUDOKU_H
