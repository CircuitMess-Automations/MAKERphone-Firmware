#ifndef MAKERPHONE_PHONESNAKESLADDERS_H
#define MAKERPHONE_PHONESNAKESLADDERS_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSnakesLadders (S98)
 *
 * Phase-N arcade entry: classic 10x10 Snakes & Ladders against a
 * single CPU opponent. The player is the cyan pawn, the CPU is the
 * sunset-orange pawn. Each turn rolls a six-sided die; the pawn
 * advances that many cells, and if it lands on a ladder foot it
 * climbs to the top, if it lands on a snake head it slides to the
 * tail. First pawn to land EXACTLY on cell 100 wins -- if the roll
 * would overshoot 100 the pawn stays put for that turn.
 *
 *   +----------------------------------------+
 *   | ||||  12:34                      ##### | <- PhoneStatusBar  (10 px)
 *   | YOU 23   CPU 17       TURN: YOU        | <- HUD strip       (12 px)
 *   | +----------------+  +----------+       |
 *   | |  10x10 board   |  |   DICE   |       |
 *   | |  with snakes   |  |   3 . 5  |       | <- side panel: dice
 *   | |  and ladders   |  +----------+       |    + status text
 *   | |                |   ROLLED 4         |
 *   | |   pawns drawn  |   YOU  23           |
 *   | |   on top       |   CPU  17           |
 *   | |                |   TURN  YOU         |
 *   | +----------------+                     |
 *   |   ROLL                  BACK           | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Controls:
 *   - BTN_5 / BTN_ENTER : roll the die during PlayerRoll. During CPU
 *                         turns, animation states, or game-over the
 *                         press is ignored (or restarts on game-over).
 *   - BTN_R             : start a new round (resets the board, keeps
 *                         the win/loss tally for the session).
 *   - BTN_BACK (B)      : pop back to PhoneGamesScreen.
 *
 * State machine (phases inside one "turn"):
 *   Idle          -- intro overlay; soft-keys: ROLL / BACK.
 *   PlayerRoll    -- waiting for the player to press roll.
 *   AnimDice      -- dice flickers through values for ~600 ms.
 *   ShowDice      -- final value held for ~300 ms.
 *   PawnMove      -- pawn teleports to landing cell, held ~400 ms.
 *   PawnJump      -- if the landing cell is a snake/ladder, the pawn
 *                    jumps to the destination after a brief pause.
 *   Handoff       -- 500 ms beat before the next turn begins.
 *   CpuRoll       -- starts CPU dice automatically (no input).
 *   PlayerWon     -- player landed on 100. Soft-keys: AGAIN / BACK.
 *   CpuWon        -- CPU landed on 100. Soft-keys: AGAIN / BACK.
 *
 * Implementation notes:
 *  - 100% code-only -- the board is one rounded panel + per-cell
 *    accent dots for snake/ladder endpoints; an `lv_line` connects
 *    each pair so the player can see the trajectory at a glance.
 *    No SPIFFS asset cost.
 *  - The board is 10x10 with 9 px cells, anchored at (4, 22) on the
 *    160x128 panel. Cells are not drawn as separate lv_objects --
 *    that would cost 100 LVGL nodes for a screen that only needs
 *    ~25 visible elements. Pawns are 5x5 coloured squares we
 *    reposition on each move.
 *  - Snakes / ladders are fixed boards (the same trajectories every
 *    round). Six snakes and seven ladders, picked so the game
 *    actually completes in 30-50 rolls without dragging.
 *  - Dice animation: a single repeating lv_timer ticks at 60 ms
 *    intervals; phase + remaining ms drive the state transitions
 *    (see `tick()`). Same pattern as PhoneSimon's tickTimer.
 *  - Session tally (wins / losses) survives "play again" but resets
 *    when the screen is popped, matching every other Phone* game.
 */
class PhoneSnakesLadders : public LVScreen, private InputListener {
public:
	PhoneSnakesLadders();
	virtual ~PhoneSnakesLadders() override;

	void onStart() override;
	void onStop() override;

	// ---- Layout constants (160 x 128 panel) -------------------------
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	// 10 x 10 board with 9 px cells = 90x90 px. Anchored on the left
	// of the 160 px panel so the 60-px-wide side panel can sit on
	// the right with the dice + score readout.
	static constexpr uint8_t   BoardN      = 10;
	static constexpr lv_coord_t CellPx     = 9;
	static constexpr lv_coord_t BoardW     = CellPx * BoardN;          // 90
	static constexpr lv_coord_t BoardH     = CellPx * BoardN;          // 90
	static constexpr lv_coord_t BoardX     = 4;
	static constexpr lv_coord_t BoardY     = StatusBarH + HudH;        // 22

	// Side panel. Sits to the right of the board with the dice cube
	// at the top + the score + "TURN" caption stacked beneath it.
	static constexpr lv_coord_t SideX      = BoardX + BoardW + 4;       // 98
	static constexpr lv_coord_t SideW      = 160 - SideX - 4;           // 58

	// Dice cube geometry. Pip positions are derived from the cube
	// origin -- see buildDice() / setDiceFace().
	static constexpr lv_coord_t DiceW      = 32;
	static constexpr lv_coord_t DiceH      = 32;
	static constexpr lv_coord_t DiceX      = SideX + (SideW - DiceW) / 2;
	static constexpr lv_coord_t DiceY      = BoardY + 2;

	// ---- Game tuning -------------------------------------------------
	// Tick period of the animation loop. 60 ms is enough resolution to
	// drive the dice flicker + pawn flash without straining LVGL.
	static constexpr uint16_t kTickMs        = 60;

	// Dice flicker total + per-frame interval during AnimDice.
	static constexpr uint16_t kAnimDiceMs    = 600;
	static constexpr uint16_t kAnimFrameMs   = 80;

	// How long the final dice value lingers before the pawn moves.
	static constexpr uint16_t kShowDiceMs    = 300;

	// Pause after the pawn arrives at the rolled cell, before any
	// snake/ladder jump kicks in.
	static constexpr uint16_t kPawnMoveMs    = 400;

	// Pause showing the snake/ladder in flight before the pawn snaps
	// to the destination cell.
	static constexpr uint16_t kPawnJumpMs    = 500;

	// Beat between turns so the player has time to read the result.
	static constexpr uint16_t kHandoffMs     = 500;

	// CPU "thinking" pause before its die starts spinning.
	static constexpr uint16_t kCpuThinkMs    = 350;

	// ---- Board features ---------------------------------------------
	// Number of snakes + ladders. Tuned so the game routinely
	// resolves inside 30-50 turns on the 100-cell board.
	static constexpr uint8_t  kSnakeCount   = 6;
	static constexpr uint8_t  kLadderCount  = 7;

private:
	struct Jump {
		uint8_t from;   // 1..100
		uint8_t to;     // 1..100
	};

	// File-scope tables; defined out-of-line in the .cpp.
	static const Jump kSnakes[kSnakeCount];
	static const Jump kLadders[kLadderCount];

	// ---- LVGL node graph --------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudYouLabel  = nullptr;
	lv_obj_t* hudCpuLabel  = nullptr;
	lv_obj_t* hudTurnLabel = nullptr;

	// Board container + grid border.
	lv_obj_t* boardPanel   = nullptr;

	// Snake / ladder marker squares + connector lines.
	lv_obj_t* snakeFrom[kSnakeCount];
	lv_obj_t* snakeTo[kSnakeCount];
	lv_obj_t* snakeLine[kSnakeCount];
	lv_point_t snakePts[kSnakeCount][2];

	lv_obj_t* ladderFrom[kLadderCount];
	lv_obj_t* ladderTo[kLadderCount];
	lv_obj_t* ladderLine[kLadderCount];
	lv_point_t ladderPts[kLadderCount][2];

	// Pawns.
	lv_obj_t* pawnYou      = nullptr;
	lv_obj_t* pawnCpu      = nullptr;

	// Side-panel: dice cube + 7 pip slots + readouts.
	lv_obj_t* diceBox      = nullptr;
	lv_obj_t* dicePips[7];           // TL TR ML C MR BL BR

	lv_obj_t* rolledLabel  = nullptr;
	lv_obj_t* youCellLabel = nullptr;
	lv_obj_t* cpuCellLabel = nullptr;
	lv_obj_t* turnLabel    = nullptr;

	// Centre overlay (intro + game-over).
	lv_obj_t* overlayLabel = nullptr;

	// ---- game state -------------------------------------------------
	enum class Phase : uint8_t {
		Idle,
		PlayerRoll,
		AnimDice,
		ShowDice,
		PawnMove,
		PawnJump,
		Handoff,
		CpuPending,    // 350 ms beat before the CPU's dice starts
		PlayerWon,
		CpuWon,
	};

	Phase phase = Phase::Idle;

	// Whose turn it is right now. true = player, false = CPU.
	bool playerTurn = true;

	// Pawn cells. 0 = "off-board" (start), 1..100 = on board.
	uint8_t youCell = 0;
	uint8_t cpuCell = 0;

	// Current die value (1..6). dieRollFinal is the value the dice
	// will SETTLE on; dieFace is whatever pip pattern is currently
	// being shown (flickers during AnimDice, then locks to dieRollFinal).
	uint8_t dieRollFinal = 1;
	uint8_t dieFace      = 1;

	// Pre-jump landing cell + post-jump destination cell. Used during
	// PawnMove + PawnJump to drive the visual without re-rolling.
	uint8_t pendingLanding = 0;
	uint8_t pendingDest    = 0;

	// Per-phase countdown (ms) and accumulated dice-flicker timer.
	int16_t phaseTimerMs  = 0;
	int16_t flickerMs     = 0;

	// Session tally.
	uint16_t winsYou = 0;
	uint16_t winsCpu = 0;

	// First mover alternates between rounds so the player doesn't
	// always get the head start.
	bool playerStartsNext = true;

	// Repeating tick driver.
	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ----------------------------------------------
	void buildHud();
	void buildBoard();
	void buildSidePanel();
	void buildOverlay();
	void buildPawns();

	// ---- state transitions ------------------------------------------
	void newRound();
	void beginPlayerTurn();
	void beginCpuTurn();
	void startRoll();           // moves into AnimDice
	void completeRoll();        // resolves landing cell + schedules jump
	void resolveTurn();         // checks win + hands off
	void awardWin(bool playerWon);

	// Game-loop step (called from the tick timer). Drives every animated
	// phase + per-pawn flicker decay. Same idiom as PhoneSimon::tick().
	void tick();

	// ---- helpers ----------------------------------------------------
	// Translates 1..100 cell indices into pixel coordinates inside the
	// board container -- accounts for the boustrophedon row order
	// (row 0 left->right, row 1 right->left, ...). Returns the centre
	// pixel of the cell relative to the board container's origin.
	lv_coord_t cellCenterX(uint8_t cell) const;
	lv_coord_t cellCenterY(uint8_t cell) const;

	// Find the destination cell for a given landing cell. Returns the
	// landing cell unchanged if no snake/ladder applies.
	uint8_t   destinationFor(uint8_t cell) const;

	// Roll a 1..6.
	uint8_t   rollDie();

	// Pip patterns. Defined in .cpp.
	void      setDiceFace(uint8_t value);

	// ---- audio ------------------------------------------------------
	void playRollTone();
	void playJumpTone(bool ladder);
	void playWinTone();
	void silencePiezo();

	// ---- rendering --------------------------------------------------
	void renderPawn(lv_obj_t* pawn, uint8_t cell, bool playerColor);
	void renderAllPawns();
	void refreshHud();
	void refreshSoftKeys();
	void refreshSidePanel();
	void refreshOverlay();

	// ---- timer helpers ----------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	// ---- input ------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESNAKESLADDERS_H
