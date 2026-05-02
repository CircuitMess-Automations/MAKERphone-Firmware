#ifndef MAKERPHONE_PHONEWORDLE_H
#define MAKERPHONE_PHONEWORDLE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneWordle (S96)
 *
 * Phase-N arcade entry: a five-letter daily word-guess in the spirit of
 * Wordle. The player has six attempts to identify a hidden five-letter
 * word; after each row is submitted, every cell is tinted to reveal how
 * close the guess was -- green for "right letter, right slot", yellow
 * for "right letter, wrong slot", grey for "letter not in the word".
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar (10 px)
 *   |     WORDLE          WIN 02 LOST 01 | <- title + win/loss tally
 *   |        +--+--+--+--+--+            |
 *   |        | A| P| P| L| E|            |
 *   |        | B| R| A| V| E|            |
 *   |        | C| H| _| _| _|            |
 *   |        | _| _| _| _| _|            |  <- 6 rows x 5 cells
 *   |        | _| _| _| _| _|            |
 *   |        | _| _| _| _| _|            |
 *   |        +--+--+--+--+--+            |
 *   |  TYPING: A    abc[a]               | <- pending T9 hint strip
 *   |   ENTER              BACK          | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls (mirroring PhoneHangman's T9 layer so the muscle memory is
 * shared across every dialer-driven Phase-N game):
 *   - BTN_2 .. BTN_9       : T9 multi-tap. First press shows the key's
 *                            first letter pending in the current cell.
 *                            Repeated presses of the same digit cycle
 *                            through that key's ring in place.
 *                            Pressing a different digit commits the
 *                            pending letter, advances the cursor and
 *                            starts a new cycle on the new key.
 *   - BTN_ENTER            : with a partial row + pending letter, commit
 *                            and advance. With a complete five-letter
 *                            row and no pending letter, submit the
 *                            guess. While the win/loss overlay is up,
 *                            ENTER also starts a new round (so the
 *                            player does not have to remember R).
 *   - BTN_LEFT / BTN_L     : backspace -- cancels a pending letter, or
 *                            deletes the previously committed letter
 *                            in the current row if no pending exists.
 *   - BTN_RIGHT            : nudge the pending letter forward through
 *                            the active key's ring without re-typing
 *                            the digit. (Mirror of PhoneHangman.)
 *   - BTN_R                : start a new round (resets the board, keeps
 *                            the win / loss tally for the session).
 *   - BTN_BACK (B)         : pop back to PhoneGamesScreen. Any pending
 *                            letter is silently cancelled rather than
 *                            committed -- the player should never lose
 *                            a guess to BACK.
 *
 * State machine:
 *   Playing  -- rows being filled in; guesses still possible.
 *   Won      -- the player matched the target word in <= 6 guesses.
 *               Overlay shows "YOU WIN -- <word>" and locks input
 *               until R or BACK.
 *   Lost     -- six rows submitted without matching. Overlay shows
 *               "GAME OVER -- <word>" so the player learns the answer.
 *
 * Implementation notes:
 *   - 100 % code-only -- every cell is a `lv_obj` rectangle plus a
 *     centred pixelbasic7 label. No SPIFFS asset cost.
 *   - Word list is an inline `static constexpr` array of common
 *     five-letter words, all uppercase A-Z. Picked uniformly at random
 *     by `newRound()` using Arduino's `rand()` (already seeded by the
 *     boot path elsewhere in the firmware).
 *   - Wordle's classic two-pass scoring: greens are scored first and
 *     remove letters from the target's remaining multiset, then
 *     yellows draw from whatever target letters remain. This is the
 *     only algorithm that handles repeated letters correctly (e.g.
 *     guess "ALLEY" against target "EAGLE" must yellow exactly one L).
 *   - Multi-tap commit timer is one `lv_timer_t` that re-arms on every
 *     pending-letter mutation. Same lifecycle pattern as
 *     PhoneT9Input::commitTimer and PhoneHangman::commitTimer.
 *   - Wins / losses persist across "new round" but reset when the
 *     screen pops, matching every other Phone* arcade game.
 */
class PhoneWordle : public LVScreen, private InputListener {
public:
	PhoneWordle();
	virtual ~PhoneWordle() override;

	void onStart() override;
	void onStop() override;

	// Layout - 160 x 128 panel.
	static constexpr uint8_t kRows  = 6;
	static constexpr uint8_t kCols  = 5;

	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;

	// Title row sits under the status bar.
	static constexpr lv_coord_t TitleY     = 11;
	static constexpr lv_coord_t TitleH     = 9;

	// Letter cells. 14 x 13 px is small enough for six rows + a hint
	// strip + a soft-key bar to fit inside 128 px tall, but big enough
	// for a single pixelbasic7 glyph to read cleanly with a 1 px frame.
	static constexpr lv_coord_t CellW      = 14;
	static constexpr lv_coord_t CellH      = 13;
	static constexpr lv_coord_t CellGap    = 1;
	static constexpr lv_coord_t GridW      = kCols * CellW + (kCols - 1) * CellGap;   // 74
	static constexpr lv_coord_t GridH      = kRows * CellH + (kRows - 1) * CellGap;   // 83
	static constexpr lv_coord_t GridX      = (160 - GridW) / 2;                       // 43
	static constexpr lv_coord_t GridY      = StatusBarH + TitleH + 2;                 // 21

	// Pending T9 hint strip sits below the grid, above the soft-keys.
	static constexpr lv_coord_t HintY      = GridY + GridH + 1;
	static constexpr lv_coord_t HintH      = 128 - SoftKeyH - HintY;

	// T9 multi-tap window. Same value PhoneT9Input / PhoneHangman use
	// (kCommitMs = 900) so the muscle-memory feel is identical between
	// every dialer-driven game.
	static constexpr uint16_t kCommitMs    = 900;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	lv_obj_t* titleLabel = nullptr;
	lv_obj_t* statsLabel = nullptr;
	lv_obj_t* hintLabel  = nullptr;

	struct Cell {
		lv_obj_t* bg    = nullptr;
		lv_obj_t* label = nullptr;
	};
	Cell cells[kRows][kCols];

	lv_obj_t* overlayPanel = nullptr;
	lv_obj_t* overlayLabel = nullptr;

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Playing,
		Won,
		Lost,
	};

	enum class CellScore : uint8_t {
		Empty,        // never typed
		Pending,      // currently being edited (T9 ring active)
		Typed,        // committed but row not yet submitted
		Hit,          // submitted & exact match
		Present,      // submitted & letter in word but wrong slot
		Miss,         // submitted & letter not in word
	};

	GameState state               = GameState::Playing;
	char      target[kCols + 1]   = {0};                        // uppercase
	char      rowBuf[kRows][kCols + 1] = {{0}};                 // working text
	CellScore rowScore[kRows][kCols] = {};                      // per-cell tint
	bool      rowSubmitted[kRows] = {false};
	uint8_t   rowIdx              = 0;
	uint8_t   colIdx              = 0;

	// Active T9 cycle - mirrors PhoneHangman's pendingKey / pendingCharIdx.
	int8_t    pendingKey          = -1;
	int8_t    pendingCharIdx      = -1;
	lv_timer_t* commitTimer       = nullptr;

	// Session tally.
	uint16_t  winsCount           = 0;
	uint16_t  lossesCount         = 0;

	// ---- build helpers ------------------------------------------------
	void buildTitle();
	void buildGrid();
	void buildHint();
	void buildOverlay();

	// ---- state transitions --------------------------------------------
	void newRound();
	void pickWord();
	void submitRow();
	void afterSubmit();

	// T9 multi-tap helpers.
	void onDigitPress(uint8_t digit);
	void cycleDirection(int8_t dir);
	void commitPending();           // commits pending letter and advances cursor
	void cancelPending();
	void rearmCommitTimer();
	void cancelCommitTimer();
	void backspace();

	// ---- rendering ----------------------------------------------------
	void renderAll();
	void renderTitleStats();
	void renderGrid();
	void renderHint();
	void renderOverlay();
	void renderSoftKeys();

	// ---- timer callbacks ----------------------------------------------
	static void commitTimerCb(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEWORDLE_H
