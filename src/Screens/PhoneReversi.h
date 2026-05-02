#ifndef MAKERPHONE_PHONEREVERSI_H
#define MAKERPHONE_PHONEREVERSI_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneReversi (S89)
 *
 * Phase-N arcade entry: classic 8x8 Reversi (a.k.a. Othello) versus
 * a single CPU opponent. The player is YOU (cyan disc), the CPU is
 * CPU (sunset orange disc). Each turn the active side places one
 * disc on an empty square such that it sandwiches at least one
 * straight, contiguous run of opposing discs between the new disc
 * and another disc of the same colour. Every sandwiched opponent
 * disc flips to the active colour. Whoever has the most discs on
 * the board when neither side has a legal move wins.
 *
 *   +------------------------------------+
 *   | ||||  12:34                 ##### | <- PhoneStatusBar  (10 px)
 *   | YOU 02   YOUR TURN     CPU 02     | <- HUD strip       (12 px)
 *   |       . . . . . . . .             |
 *   |       . . . . . . . .             |
 *   |       . . . . . . . .             |
 *   |       . . . O X . . .             |    <- 8x8 board, 11 px cells
 *   |       . . . X O . . .             |       centred horizontally
 *   |       . . . . . . . .             |       at X = 36, Y = 24
 *   |       . . . . . . . .             |
 *   |       . . . . . . . .             |
 *   |   PLACE              BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_4 / BTN_LEFT    : cursor left  (wraps).
 *   - BTN_6 / BTN_RIGHT   : cursor right (wraps).
 *   - BTN_2               : cursor up    (wraps).
 *   - BTN_8               : cursor down  (wraps).
 *   - BTN_5 / BTN_ENTER   : place a disc on the cursor cell. Only
 *                           legal if the move flips at least one
 *                           opposing disc. Illegal moves are a soft
 *                           no-op (left soft-key flashes only).
 *   - BTN_R               : start a new round (resets the board, keeps
 *                           the win/loss/tie tally for the session).
 *   - BTN_BACK (B)        : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   PlayerTurn    -- waiting for the player to place a disc.
 *   CpuPending    -- CPU "thinking" delay; one-shot timer fires the
 *                    move and transitions to PlayerTurn or a finished
 *                    state. PassPending shares this delay timer when
 *                    the active side has no legal moves and the turn
 *                    has to bounce silently to the other side.
 *   PlayerWon     -- terminal: more YOU discs than CPU discs.
 *   CpuWon        -- terminal: more CPU discs than YOU discs.
 *   Tie           -- terminal: equal disc counts (rare but possible).
 *
 * Implementation notes:
 *   - 100% code-only -- the board renders as one rounded background
 *     panel plus 64 small disc widgets (one per cell). Discs are the
 *     same circles re-tinted to MP_HIGHLIGHT (cyan) or MP_ACCENT
 *     (orange) and the empty cells render as the dim panel showing
 *     through. No SPIFFS asset cost.
 *   - Board indexing: row 0 is the top row, col 0 is the left column.
 *     Standard Reversi opening: (3,3)=Cpu (white), (3,4)=You (black),
 *     (4,3)=You (black), (4,4)=Cpu (white). Player (YOU = "black") moves
 *     first by convention.
 *   - Move legality: scan the eight straight-line directions from the
 *     candidate cell. A direction is "captures" if it walks one or
 *     more contiguous opponent discs and lands on a same-colour disc
 *     (without leaving the board or hitting an empty cell first). A
 *     move is legal iff at least one direction captures.
 *   - The cursor is rendered as a 1-px highlight border on the focused
 *     cell. Cyan if the move is legal, dim purple if it is not -- the
 *     player can scan the board visually for legal squares without
 *     having to mash ENTER.
 *   - Silent passes: if after a move the opponent has zero legal moves
 *     but the active side does, control bounces back to the active
 *     side and a small "YOU/CPU PASSED" caption appears in the HUD.
 *     The caption clears when the next move resolves so a streak of
 *     passes still reads correctly. If neither side has a legal move,
 *     the game ends -- which can happen even before all 64 squares
 *     are filled in pathological positions.
 *   - CPU strategy ("kid's algorithm"):
 *       For every legal CPU move, score it as:
 *         + 100  if the cell is a corner.
 *         -  20  if the cell is an "X-square" (diagonally adjacent to
 *                an empty corner) -- almost always a free corner gift.
 *         -  10  if the cell is a "C-square" (orthogonally adjacent
 *                to an empty corner).
 *         +   2  per piece flipped.
 *         +   3  if the cell is on an edge (and not penalised above).
 *       Then pick the highest-scoring candidate with a small random
 *       jitter so the CPU's openings don't feel scripted. This is
 *       intentionally not minimax -- a careless player will lose, an
 *       attentive one will win, which is exactly the right feel for
 *       a 30-second coffee-break game on a feature phone.
 *   - The CPU move is delayed via lv_timer (~450 ms) so the player
 *     doesn't feel they're watching themselves play. The state during
 *     the delay is CpuPending -- input is ignored except BACK and the
 *     R reshuffle so the player can't double-place.
 *   - In-memory tally (wins / losses / ties) survives "new round" but
 *     resets when the screen is popped, matching every other Phone*
 *     game in the v1.0 + Phase-N roadmap.
 */
class PhoneReversi : public LVScreen, private InputListener {
public:
	PhoneReversi();
	virtual ~PhoneReversi() override;

	void onStart() override;
	void onStop() override;

	// Screen layout - matches the diagram above. 160 x 128 panel.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	// Board geometry: 8 columns x 8 rows at 11 px per cell. The board
	// sits centred under the HUD and leaves a 5-6 px gap above the
	// softkey bar so the layout doesn't feel cramped.
	static constexpr uint8_t   BoardCols  = 8;
	static constexpr uint8_t   BoardRows  = 8;
	static constexpr uint8_t   CellCount  = BoardCols * BoardRows;
	static constexpr lv_coord_t CellPx    = 11;
	static constexpr lv_coord_t BoardW    = CellPx * BoardCols; // 88
	static constexpr lv_coord_t BoardH    = CellPx * BoardRows; // 88
	static constexpr lv_coord_t BoardOriginX = (160 - BoardW) / 2; // 36
	static constexpr lv_coord_t BoardOriginY = 24;

	// CPU "thinking" delay before its move resolves, and the same
	// timer is re-used for "pass" announcements so the screen has
	// exactly one outstanding lv_timer to manage. 450 ms reads as
	// deliberation without feeling laggy on the snappy ESP32 stack.
	static constexpr uint16_t kCpuThinkMs  = 450;
	static constexpr uint16_t kPassFlashMs = 700;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudYouLabel  = nullptr;
	lv_obj_t* hudCpuLabel  = nullptr;
	lv_obj_t* hudTurnLabel = nullptr; // centre slot: YOUR TURN / CPU TURN / passed
	lv_obj_t* overlayLabel = nullptr;

	// Single rounded rectangle behind every cell -- gives the board a
	// real "frame" instead of a pile of free-floating circles.
	lv_obj_t* boardPanel = nullptr;

	// One cell obj per board square. The cell hosts a faint border
	// (the grid line) and the disc itself is a child circle that is
	// shown / hidden / re-tinted depending on the contents.
	lv_obj_t* cellFrames[CellCount];
	lv_obj_t* cellDiscs[CellCount];

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

	enum class PassNote : uint8_t {
		None,
		YouPassed,
		CpuPassed,
	};

	GameState state = GameState::PlayerTurn;
	Disc      board[CellCount];

	// 2D cursor in column / row coordinates.
	uint8_t cursorCol = BoardCols / 2 - 1;  // start near the centre opening
	uint8_t cursorRow = BoardRows / 2 - 1;

	// "Opponent passed" caption that sits in the HUD's centre slot
	// until the next move resolves.
	PassNote passNote = PassNote::None;

	// Session tally.
	uint16_t winsYou = 0;
	uint16_t winsCpu = 0;
	uint16_t ties    = 0;

	// CPU "thinking" / pass delay one-shot. We only ever have one in
	// flight, so a single pointer is enough.
	lv_timer_t* cpuTimer = nullptr;

	// First mover alternates between rounds so the player doesn't always
	// open. true = player first, false = CPU.
	bool playerStartsNext = true;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildBoard();

	// ---- state transitions --------------------------------------------
	void newRound();
	bool placeDisc(uint8_t col, uint8_t row, Disc who);
	void afterMove(Disc who);
	void scheduleCpuMove();
	void schedulePassThenContinue();
	void doCpuMove();
	int8_t pickCpuMove() const; // -1 if no legal move

	// ---- board queries ------------------------------------------------
	bool   isInBounds(int8_t col, int8_t row) const;
	bool   hasAnyLegalMove(Disc who) const;
	bool   isLegalMove(uint8_t col, uint8_t row, Disc who,
	                   uint8_t* outFlipsByDir = nullptr) const;
	uint8_t flipsForMove(uint8_t col, uint8_t row, Disc who) const;
	void   countDiscs(uint16_t* outYou, uint16_t* outCpu) const;
	bool   isCorner(uint8_t col, uint8_t row) const;
	bool   isXSquare(uint8_t col, uint8_t row) const;
	bool   isCSquare(uint8_t col, uint8_t row) const;
	bool   isEdge(uint8_t col, uint8_t row) const;

	// ---- helpers ------------------------------------------------------
	uint8_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint8_t>(row * BoardCols + col);
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
};

#endif // MAKERPHONE_PHONEREVERSI_H
