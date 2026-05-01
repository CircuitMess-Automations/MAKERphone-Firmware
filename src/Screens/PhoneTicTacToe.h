#ifndef MAKERPHONE_PHONETICTACTOE_H
#define MAKERPHONE_PHONETICTACTOE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneTicTacToe (S81)
 *
 * Phase-N arcade entry: classic 3x3 noughts-and-crosses against a
 * single CPU opponent. The player is always X (cyan), the CPU is
 * always O (sunset orange). The CPU uses a tiny "competent but not
 * perfect" rule list -- win-if-possible, block-if-threatened, prefer
 * centre/corners/edges -- so a mindful player can win, but a careless
 * one will lose, which is exactly the right feel for a 30-second
 * coffee-break game on a feature phone.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | YOU 02   CPU 01   TIE 03          | <- HUD strip       (12 px)
 *   |          +------+------+------+    |
 *   |          |  X   |      |      |    |
 *   |          +------+------+------+    |    <- 3x3 board, 30x30 px
 *   |          |      |  O   |      |    |       cells, total 90x90 px
 *   |          +------+------+------+    |       centred horizontally
 *   |          |      |      |      |    |       (35 px margin).
 *   |          +------+------+------+    |
 *   |   PLACE              BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 / BTN_8       : cursor up / down
 *   - BTN_4 / BTN_6       : cursor left / right
 *   - BTN_LEFT / BTN_RIGHT: cursor left / right (alias)
 *   - BTN_5 / BTN_ENTER   : place X in the highlighted cell. Only
 *                           legal if the cell is empty and it's the
 *                           player's turn.
 *   - BTN_R               : start a new round (resets the board, keeps
 *                           the win/loss/tie tally for the session).
 *   - BTN_BACK (B)        : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   PlayerTurn    -- waiting for the player to place an X.
 *   CpuPending    -- CPU "thinking" delay; one-shot timer fires the
 *                    move and transitions to PlayerTurn or a finished
 *                    state.
 *   PlayerWon     -- three Xs in a line. Overlay shows "YOU WIN".
 *   CpuWon        -- three Os in a line. Overlay shows "CPU WINS".
 *   Tie           -- board full with no winner. Overlay shows "TIE".
 *
 * Implementation notes:
 *   - 100% code-only -- every cell is a plain lv_obj rectangle with a
 *     pixelbasic16 label that draws either "X", "O" or "" (empty).
 *     No SPIFFS asset cost.
 *   - Win-line highlight: when somebody wins, the three cells that
 *     formed the winning line get an accent border so the result
 *     reads at a glance even before the player notices the overlay.
 *   - CPU strategy:
 *       1. If the CPU can win this move, take it.
 *       2. Else if the player threatens to win next move, block it.
 *       3. Else play centre if free.
 *       4. Else play a free corner.
 *       5. Else play a free edge.
 *     This is the "kid's algorithm" -- not minimax, but enough of a
 *     game to feel like a real opponent.
 *   - The CPU move is delayed via lv_timer (~350 ms) so the player
 *     doesn't feel they're watching themselves play. The state during
 *     the delay is CpuPending -- input is ignored except BACK and the
 *     R reshuffle so the player can't double-place.
 *   - In-memory tally (wins / losses / ties) survives "new round" but
 *     resets when the screen is popped, matching every other Phone*
 *     game in the v1.0 roadmap.
 */
class PhoneTicTacToe : public LVScreen, private InputListener {
public:
	PhoneTicTacToe();
	virtual ~PhoneTicTacToe() override;

	void onStart() override;
	void onStop() override;

	// Screen layout - matches the diagram above. 160 x 128 panel.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	// 3x3 board with 30 px tiles totals 90x90. Centred horizontally on
	// the 160 px panel (35 px margin) and tucked just under the HUD
	// (top edge at y = 22, bottom edge at y = 112, leaving a 6 px
	// breathing space above the soft-key bar at y = 118).
	static constexpr uint8_t BoardSize = 3;
	static constexpr uint8_t CellCount = BoardSize * BoardSize;
	static constexpr lv_coord_t CellPx = 30;
	static constexpr lv_coord_t BoardPx = CellPx * BoardSize;       // 90
	static constexpr lv_coord_t BoardOriginX = (160 - BoardPx) / 2; // 35
	static constexpr lv_coord_t BoardOriginY = StatusBarH + HudH;   // 22

	// CPU "thinking" delay before its move resolves. 350 ms is long
	// enough to read as deliberation, short enough not to feel laggy.
	static constexpr uint16_t kCpuThinkMs = 350;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudYouLabel = nullptr;
	lv_obj_t* hudCpuLabel = nullptr;
	lv_obj_t* hudTieLabel = nullptr;
	lv_obj_t* overlayLabel = nullptr;

	// One rectangle + one mark label per cell. Indexing is row-major:
	// cell[i] is row (i / 3), col (i % 3).
	lv_obj_t* cellSprites[CellCount];
	lv_obj_t* cellLabels[CellCount];

	// ---- game state ---------------------------------------------------
	enum class Mark : uint8_t {
		Empty = 0,
		X     = 1,   // player
		O     = 2,   // CPU
	};

	enum class GameState : uint8_t {
		PlayerTurn,
		CpuPending,
		PlayerWon,
		CpuWon,
		Tie,
	};

	GameState state = GameState::PlayerTurn;
	Mark      board[CellCount];
	uint8_t   winLine[3] = { 0xFF, 0xFF, 0xFF }; // valid when state == *Won

	// Cursor in cell coordinates (0..BoardSize-1).
	uint8_t cursorCol = 1;
	uint8_t cursorRow = 1;

	// Session tally.
	uint16_t winsYou = 0;
	uint16_t winsCpu = 0;
	uint16_t ties    = 0;

	// CPU "thinking" one-shot.
	lv_timer_t* cpuTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildCells();

	// ---- state transitions --------------------------------------------
	void newRound();                  // clears the board, alternates first move
	void placeMark(uint8_t cell, Mark m);
	void afterMove();                 // checks win/tie + advances state
	void scheduleCpuMove();
	void doCpuMove();
	uint8_t pickCpuMove() const;      // returns 0..8 of the chosen cell
	bool   isBoardFull() const;
	bool   findWinningLine(Mark m, uint8_t out[3]) const;
	int8_t findWinningCellFor(Mark m) const; // -1 if none

	// ---- helpers ------------------------------------------------------
	uint8_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint8_t>(row * BoardSize + col);
	}

	// ---- rendering ----------------------------------------------------
	void renderAllCells();
	void renderCell(uint8_t cell);
	void renderCursor();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	// ---- timer helpers ------------------------------------------------
	void cancelCpuTimer();
	static void onCpuTimerStatic(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;

	// First mover alternates between rounds so the player doesn't always
	// have the X-goes-first advantage. true = player first, false = CPU.
	bool playerStartsNext = true;
};

#endif // MAKERPHONE_PHONETICTACTOE_H
