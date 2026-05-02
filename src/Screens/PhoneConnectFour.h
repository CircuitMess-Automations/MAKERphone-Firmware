#ifndef MAKERPHONE_PHONECONNECTFOUR_H
#define MAKERPHONE_PHONECONNECTFOUR_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneConnectFour (S88)
 *
 * Phase-N arcade entry: classic 7-column-by-6-row "drop the disc" game
 * versus a single CPU opponent. The player is YOU (cyan disc), the CPU
 * is CPU (sunset orange disc). Pieces drop into the lowest empty slot
 * of the chosen column under simulated gravity, and the first side to
 * line up four discs in a row -- horizontally, vertically, or
 * diagonally -- wins the round.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | YOU 02   CPU 01   TIE 03          | <- HUD strip       (12 px)
 *   |              [v]                  | <- column cursor   ( 6 px)
 *   |   . . . . . . .                   |
 *   |   . . . . . . .                   |
 *   |   . . . O . . .                   |    <- 7x6 board, 14 px cells
 *   |   . . . X . . .                   |       centred horizontally
 *   |   . . . X O . .                   |       (origin X = 31, Y = 28)
 *   |   . . X X O . .                   |
 *   |   DROP               BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_4 / BTN_LEFT    : column cursor left (wraps).
 *   - BTN_6 / BTN_RIGHT   : column cursor right (wraps).
 *   - BTN_5 / BTN_ENTER   : drop the player's disc into the chosen
 *                           column. Only legal if the column has at
 *                           least one empty row and it's the player's
 *                           turn.
 *   - BTN_R               : start a new round (resets the board, keeps
 *                           the win/loss/tie tally for the session).
 *   - BTN_BACK (B)        : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   PlayerTurn    -- waiting for the player to drop a disc.
 *   CpuPending    -- CPU "thinking" delay; one-shot timer fires the
 *                    move and transitions to PlayerTurn or a finished
 *                    state.
 *   PlayerWon     -- the player completed a 4-in-a-row.
 *   CpuWon        -- the CPU completed a 4-in-a-row.
 *   Tie           -- board full with no winner. (Rare but possible.)
 *
 * Implementation notes:
 *   - 100% code-only -- the board renders as one rounded background
 *     panel plus 42 small circle widgets (one per cell). Discs are the
 *     same circles re-tinted to MP_HIGHLIGHT (cyan) or MP_ACCENT
 *     (orange). No SPIFFS asset cost.
 *   - Board indexing: row 0 is the top row, row 5 is the bottom. The
 *     drop loop scans bottom-up to find the first empty row in the
 *     chosen column, matching real Connect Four physics.
 *   - Win-line highlight: when somebody wins, the four cells that
 *     formed the winning line get an accent border so the result reads
 *     at a glance even before the player notices the overlay.
 *   - CPU strategy ("kid's algorithm"):
 *       1. If the CPU has a winning drop, take it.
 *       2. Else if the player threatens to win on their next move,
 *          block it.
 *       3. Else try to avoid columns that would hand the player a win
 *          on top of our piece.
 *       4. Else prefer the column closest to centre (centre > the two
 *          either side > the next pair > the edges) with a small
 *          random jitter so the CPU's openings don't feel scripted.
 *     This is intentionally not minimax -- a careless player will
 *     lose, an attentive one will win, which is exactly the right feel
 *     for a 30-second coffee-break game on a feature phone.
 *   - The CPU move is delayed via lv_timer (~400 ms) so the player
 *     doesn't feel they're watching themselves play. The state during
 *     the delay is CpuPending -- input is ignored except BACK and the
 *     R reshuffle so the player can't double-drop.
 *   - In-memory tally (wins / losses / ties) survives "new round" but
 *     resets when the screen is popped, matching every other Phone*
 *     game in the v1.0 + Phase-N roadmap.
 */
class PhoneConnectFour : public LVScreen, private InputListener {
public:
	PhoneConnectFour();
	virtual ~PhoneConnectFour() override;

	void onStart() override;
	void onStop() override;

	// Screen layout - matches the diagram above. 160 x 128 panel.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;
	static constexpr lv_coord_t CursorStripY = 22;
	static constexpr lv_coord_t CursorStripH = 6;

	// Board geometry: 7 columns x 6 rows at 14 px per cell. The board
	// sits below the cursor strip so the column-arrow indicator has its
	// own vertical band that doesn't overlap with the drop zone.
	static constexpr uint8_t BoardCols  = 7;
	static constexpr uint8_t BoardRows  = 6;
	static constexpr uint8_t CellCount  = BoardCols * BoardRows;
	static constexpr lv_coord_t CellPx  = 14;
	static constexpr lv_coord_t BoardW  = CellPx * BoardCols; // 98
	static constexpr lv_coord_t BoardH  = CellPx * BoardRows; // 84
	static constexpr lv_coord_t BoardOriginX = (160 - BoardW) / 2;          // 31
	static constexpr lv_coord_t BoardOriginY = CursorStripY + CursorStripH; // 28

	// CPU "thinking" delay before its move resolves. 400 ms reads as
	// deliberation without feeling laggy on a snappy ESP32 LVGL stack.
	static constexpr uint16_t kCpuThinkMs = 400;

	// How many discs in a row are needed to win. Connect Four uses 4
	// across all of horizontal, vertical, and both diagonals. Kept as a
	// named constant so the win-scan code reads at a glance.
	static constexpr uint8_t WinRun = 4;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudYouLabel = nullptr;
	lv_obj_t* hudCpuLabel = nullptr;
	lv_obj_t* hudTieLabel = nullptr;
	lv_obj_t* overlayLabel = nullptr;

	// Single rounded rectangle behind every cell, gives the board a
	// real "frame" instead of a pile of free-floating circles.
	lv_obj_t* boardPanel = nullptr;

	// One disc obj per cell -- 11x11 rounded square that LVGL renders
	// as a circle (radius 6 == half the side). Indexing is row-major:
	// cellDiscs[row * BoardCols + col].
	lv_obj_t* cellDiscs[CellCount];

	// Column-cursor arrow that rides above the board, telegraphing
	// which column a drop will land in.
	lv_obj_t* columnCursor = nullptr;

	// ---- game state ---------------------------------------------------
	enum class Disc : uint8_t {
		Empty = 0,
		You   = 1,   // player (cyan)
		Cpu   = 2,   // CPU (orange)
	};

	enum class GameState : uint8_t {
		PlayerTurn,
		CpuPending,
		PlayerWon,
		CpuWon,
		Tie,
	};

	GameState state = GameState::PlayerTurn;
	Disc      board[CellCount];
	uint8_t   winLine[WinRun];   // valid when state == *Won

	// Cursor in column coordinates (0..BoardCols-1).
	uint8_t cursorCol = BoardCols / 2; // start in the middle column

	// Session tally.
	uint16_t winsYou = 0;
	uint16_t winsCpu = 0;
	uint16_t ties    = 0;

	// CPU "thinking" one-shot.
	lv_timer_t* cpuTimer = nullptr;

	// First mover alternates between rounds so the player doesn't always
	// have the first-move advantage. true = player first, false = CPU.
	bool playerStartsNext = true;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildBoard();
	void buildColumnCursor();

	// ---- state transitions --------------------------------------------
	void newRound();
	bool dropDisc(uint8_t col, Disc who, uint8_t* outCell);
	void afterMove(uint8_t lastCell, Disc who);
	void scheduleCpuMove();
	void doCpuMove();
	uint8_t pickCpuMove();

	// ---- board queries ------------------------------------------------
	bool   columnHasRoom(uint8_t col) const;
	int8_t lowestEmptyRow(uint8_t col) const;
	bool   isBoardFull() const;
	// Scans the four straight-line directions from `cell` for a run of
	// `who` of length WinRun. If found, fills `outLine` with the four
	// participating cell indices and returns true.
	bool   scanWinFromCell(uint8_t cell, Disc who, uint8_t outLine[WinRun]) const;
	// Convenience wrapper: returns the column of a winning move for
	// `who` if one exists immediately, or -1 otherwise.
	int8_t findWinningColFor(Disc who) const;

	// ---- helpers ------------------------------------------------------
	uint8_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint8_t>(row * BoardCols + col);
	}

	// ---- rendering ----------------------------------------------------
	void renderAllCells();
	void renderCell(uint8_t cell);
	void renderColumnCursor();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	// ---- timer helpers ------------------------------------------------
	void cancelCpuTimer();
	static void onCpuTimerStatic(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONECONNECTFOUR_H
