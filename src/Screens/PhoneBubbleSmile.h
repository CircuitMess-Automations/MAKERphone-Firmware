#ifndef MAKERPHONE_PHONEBUBBLESMILE_H
#define MAKERPHONE_PHONEBUBBLESMILE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneBubbleSmile (S77, extended in S78)
 *
 * Phase-N arcade entry: a colour-matching grid puzzle in the
 * "Bubble Smile" / Bejeweled / "match-3" tradition. The board is
 * a 6x7 grid of round bubbles in five colours; the player swaps
 * two adjacent bubbles to line up runs of three or more matching
 * colours, which clear, drop, and refill in a cascade.
 *
 * Layout (160 x 128):
 *   - PhoneStatusBar at the top  (10 px, y =   0..  9)
 *   - HUD strip                  (10 px, y =  10.. 19)
 *       "BUBBLES"  centred title
 *       score + target right-anchored
 *   - Playfield                  (98 px, y =  20..117)
 *       6 cols x 7 rows of 18x12 px bubbles (1 px gaps)
 *       grid bounds: x = 23..136, y = 24..114
 *   - PhoneSoftKeyBar at bottom  (10 px, y = 118..127)
 *
 * Bubble grid: 6 columns x 7 rows = 42 cells. Each cell is an
 * 18x12 px pad showing a coloured 14x9 px "bubble" with a 2x2
 * highlight pixel so the read-out feels round-ish at this scale.
 * Five colours are used (cyan / red / yellow / green / magenta).
 *
 * Controls:
 *   - BTN_LEFT  / BTN_4 : move cursor one column left.
 *   - BTN_RIGHT / BTN_6 : move cursor one column right.
 *   - BTN_2 (numpad up) : move cursor one row up.
 *   - BTN_8 (numpad down) : move cursor one row down.
 *   - BTN_ENTER (A)     : pick up / drop a bubble.
 *                         First press: marks current cell as the
 *                                      "selected" anchor (cyan
 *                                      border pulse).
 *                         Second press on the *same* cell: deselect.
 *                         Second press on an *adjacent* cell: swap.
 *                         If the swap creates no match, the swap
 *                         reverts (classic Bejeweled rule).
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- "PRESS START" overlay, fresh board.
 *                ENTER -> Playing.
 *   Playing   -- normal cursor input. ENTER selects / swaps.
 *   Resolving -- no input; cascade animation runs on a 150 ms
 *                ticker. We sequence through three phases per
 *                cascade tick: Match (flash + score) -> Drop
 *                (fall into empties) -> Fill (top refill) ->
 *                Match (next cascade level) -> ... until no more
 *                matches; then we run a "no valid moves" sweep
 *                and either return to Playing or shuffle the
 *                board.
 *   GameOver  -- target score reached -> "WELL DONE!" overlay.
 *                ENTER -> Idle (full reset).
 *
 * Scoring:
 *   - Each cleared bubble: 10 * cascadeLevel points.
 *     (cascadeLevel starts at 1 on the player's swap, +1 per
 *     extra cascade chain.)
 *   - Reach TargetScore (1000) to clear the round.
 *
 * Implementation notes:
 *   - 100% code-only -- no SPIFFS asset cost. Each bubble is one
 *     `lv_obj` rectangle plus a 2x2 highlight rectangle.
 *   - Random placement uses a tiny xorshift16 PRNG seeded from
 *     millis() at game start so each session feels fresh.
 *   - Match detection is a two-pass scan (rows + columns) over the
 *     6x7 grid; we mark a cell-mask, sum it for score, then drop
 *     and refill in column-major order.
 *   - "No valid moves" detection walks every adjacent pair, swaps
 *     in-place, runs match detection, then swaps back. With 42
 *     cells and ~78 adjacent pairs the cost is trivial on the
 *     ESP32 and only runs once per cascade settle.
 *
 * S78 split (combo cascades + 5 power-ups):
 *   - Combo cascades. Each cascade chain link beyond the first
 *     scales the score multiplier (cascadeLevel * 10), and chains
 *     of 3+ also earn a +50%% bonus on that tick's points and a
 *     "COMBO xN" flash in the HUD strip.
 *   - Five event-driven power-ups, awarded inline during match
 *     detection (no persistent special-bubble state required):
 *       1. StripeRow  - any horizontal run of 4 also clears the
 *                       entire row the run lives on.
 *       2. StripeCol  - any vertical   run of 4 also clears the
 *                       entire column the run lives on.
 *       3. Bomb       - any single-direction run of 5 also clears
 *                       the 3x3 area centred on the run.
 *       4. ColorBlast - any run of 6 also clears every bubble of
 *                       that colour anywhere on the board.
 *       5. Combo      - cascadeLevel >= 3 multiplies the tick's
 *                       points by 1.5 (chain reward).
 *     The most spectacular power-up that triggered in a Match-phase
 *     tick wins the `powerUpLabel` HUD flash for that tick.
 */
class PhoneBubbleSmile : public LVScreen, private InputListener {
public:
	PhoneBubbleSmile();
	virtual ~PhoneBubbleSmile() override;

	void onStart() override;
	void onStop() override;

	// Grid sizing (public so the constexpr-sized sprite arrays in the
	// .cpp file size from the same source).
	static constexpr uint8_t GridCols  = 6;
	static constexpr uint8_t GridRows  = 7;
	static constexpr uint8_t CellCount = GridCols * GridRows; // 42

	// Five distinct bubble colours plus an Empty sentinel used during
	// the cascade resolve.
	enum class Bubble : uint8_t {
		Empty   = 0,
		Cyan    = 1,
		Red     = 2,
		Yellow  = 3,
		Green   = 4,
		Magenta = 5,
	};
	static constexpr uint8_t ColorCount = 5;   // Bubble::Cyan .. Bubble::Magenta

	// S78 power-up tiers tracked during the cascade pipeline. Higher
	// tiers strictly subsume lower tiers when multiple are triggered
	// in the same Match-phase tick: the most spectacular one wins
	// the HUD `powerUpLabel` flash.
	enum class PowerUp : uint8_t {
		None       = 0,
		StripeRow  = 1,
		StripeCol  = 2,
		Bomb       = 3,
		ColorBlast = 4,
		Combo      = 5,
	};

	// Cell pixel size + spacing. Centred horizontally inside 160 px
	// playfield: total width = 6*18 + 5*1 = 113, x_start = 23.
	// Vertical: y_start = 24, total height = 7*12 + 6*1 = 90, ends at 114.
	static constexpr lv_coord_t CellW    = 18;
	static constexpr lv_coord_t CellH    = 12;
	static constexpr lv_coord_t CellGapX = 1;
	static constexpr lv_coord_t CellGapY = 1;
	static constexpr lv_coord_t GridX    = 23;
	static constexpr lv_coord_t GridY    = 24;

	// Status / soft-key strips.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;

	// Cascade tick.
	static constexpr uint32_t ResolveStepMs = 150;

	// Win threshold.
	static constexpr uint32_t TargetScore = 1000;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudLeftLabel  = nullptr;   // "BUBBLES"
	lv_obj_t* scoreLabel    = nullptr;   // numeric score
	lv_obj_t* targetLabel   = nullptr;   // "/ 1000"
	lv_obj_t* playfield     = nullptr;
	lv_obj_t* overlayLabel  = nullptr;
	lv_obj_t* powerUpLabel  = nullptr;   // S78 power-up flash text

	// One cell = pad + bubble inner + highlight pixel. We keep the pad
	// as the focusable frame so cursor / selection borders can be drawn
	// on it without resizing children.
	lv_obj_t* cellPads[CellCount];
	lv_obj_t* bubbleSprites[CellCount];
	lv_obj_t* highlightDots[CellCount];

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Playing,
		Resolving,
		GameOver,
	};
	GameState state = GameState::Idle;

	// S78 cascade flash state: which power-up tier (if any) triggered
	// during the most recent Match-phase tick. Reset to None on player
	// swap and on the Drop tick that follows the flash so the HUD does
	// not retain stale text.
	PowerUp lastPowerUp = PowerUp::None;

	// Grid of bubbles + per-cell match-flag (used during resolve).
	Bubble  grid[CellCount];
	bool    matched[CellCount];

	// Cursor + selected anchor cell (-1 = nothing selected).
	uint8_t cursor   = 0;            // 0..CellCount-1
	int8_t  selected = -1;

	// Score / progress.
	uint32_t score          = 0;
	uint8_t  cascadeLevel   = 0;     // 1 on player swap, +1 per cascade chain

	// Cascade pipeline phase.
	enum class ResolvePhase : uint8_t {
		Match,    // detect + mark + score; if none, settle
		Drop,     // gravity bubbles into empties
		Fill,     // refill top empties with random colours
	};
	ResolvePhase phase = ResolvePhase::Match;

	// Pending swap revert (set when the player's swap produced no match
	// and we want to swap back after a one-tick "no-match" feedback).
	bool revertPending  = false;
	uint8_t revertA     = 0;
	uint8_t revertB     = 0;

	// Tiny PRNG state -- xorshift16, seeded from millis() at start.
	uint16_t rngState = 0xACE1u;

	lv_timer_t* resolveTimer = nullptr;

	// ---- helpers ------------------------------------------------------
	static uint8_t cellIndex(uint8_t col, uint8_t row) {
		return static_cast<uint8_t>(row * GridCols + col);
	}
	static uint8_t cellCol(uint8_t idx) { return idx % GridCols; }
	static uint8_t cellRow(uint8_t idx) { return idx / GridCols; }
	static bool   isAdjacent(uint8_t a, uint8_t b);

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildPlayfield();
	void buildCells();
	void buildOverlay();
	void buildPowerUpLabel();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startGame();
	void winGame();

	// ---- game ops -----------------------------------------------------
	void seedBoard();                 // fill grid avoiding pre-matches
	Bubble randomColor();
	uint16_t xorshift16();

	bool detectMatches(uint16_t& cleared);  // mark + return cleared count
	void clearMatches();
	void dropBubbles();
	void fillTop();
	bool hasAnyValidMove() const;
	void shuffleBoard();              // emergency shuffle if no moves

	// S78 power-up sweep, run after detectMatches() during every
	// Match-phase tick. Walks rows/columns again, this time looking
	// for runs of length >= 4 and extending the matched[] mask with
	// the power-up's blast radius. Sets `lastPowerUp` to the
	// highest-tier power-up triggered during the sweep.
	void applyPowerUps();
	void clearRow(uint8_t row);
	void clearCol(uint8_t col);
	void clearArea3x3(uint8_t cx, uint8_t cy);
	void clearAllOfColor(Bubble c);

	void doSwap(uint8_t a, uint8_t b);
	void onPlayerSwap();              // begin resolve pipeline after swap

	// Helpers for hasAnyValidMove(): try a swap, run match check, undo.
	bool wouldMatch(uint8_t a, uint8_t b) const;

	// ---- rendering ----------------------------------------------------
	void render();
	void renderCell(uint8_t idx);
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();
	void refreshPowerUpLabel();
	lv_color_t bubbleColor(Bubble b) const;

	// ---- timers -------------------------------------------------------
	void startResolveTimer();
	void stopResolveTimer();
	static void onResolveTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEBUBBLESMILE_H
