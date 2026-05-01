#ifndef MAKERPHONE_PHONEMEMORYMATCH_H
#define MAKERPHONE_PHONEMEMORYMATCH_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneMemoryMatch (S82)
 *
 * Phase-N arcade entry: classic concentration / pairs game. The board
 * is a 4x4 grid of face-down cards hiding eight pairs of pixel-art
 * icons. The player flips two cards per turn; matching pairs stay
 * revealed, mismatched pairs flip back after a short reveal. The
 * count-to-clear timer ticks the moment the player flips their first
 * card and stops on the move that resolves the last pair, giving a
 * "best time + best flips" leaderboard feel for repeat plays.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | FLIPS 04   PAIRS 01/8   00:23     | <- HUD strip       (12 px)
 *   |        +-----+-----+-----+-----+   |
 *   |        | ?:: | ::? | ::? | ::? |   |
 *   |        +-----+-----+-----+-----+   |    <- 4x4 board, 22x22 px
 *   |        | <3  | ::? | ::? | <3  |   |       cards. Total 94x94
 *   |        +-----+-----+-----+-----+   |       centred on a 160 px
 *   |        | ::? | ::? | ::? | ::? |   |       panel (origin x = 33)
 *   |        +-----+-----+-----+-----+   |
 *   |        | ::? | ::? | ::? | ::? |   |
 *   |        +-----+-----+-----+-----+   |
 *   |   FLIP                BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 / BTN_8       : cursor up / down (wraps)
 *   - BTN_4 / BTN_6       : cursor left / right (wraps)
 *   - BTN_LEFT / BTN_RIGHT: cursor left / right (alias)
 *   - BTN_5 / BTN_ENTER   : flip the highlighted card. Ignored if the
 *                           card is already revealed or matched, or
 *                           while a mismatched pair is fading back.
 *   - BTN_R               : reshuffle / start a new round at any time.
 *   - BTN_BACK (B)        : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- zero or zero+matched cards face up; waiting for a
 *                first flip.
 *   Showing   -- exactly one face-up un-matched card; waiting for the
 *                player's second flip.
 *   Resolving -- two face-up un-matched cards; an lv_timer is in
 *                flight that will either lock them as matched or flip
 *                them face down again. Inputs other than BACK / R are
 *                swallowed during this window so the player cannot
 *                "cheat" by speed-flipping a third card.
 *   Won       -- all eight pairs matched. Overlay shows the final
 *                flip count + time. BTN_5 / BTN_ENTER reshuffles for
 *                another go; BTN_BACK pops back to the launcher.
 *
 * Implementation notes:
 *   - 100% code-only -- every card and every pixel-art icon is built
 *     from plain `lv_obj` rectangles. No SPIFFS asset cost.
 *   - The eight icon kinds are drawn by `paintIcon()` from a tiny
 *     dispatch on the kind id (0..7). Each icon fits inside the 14x14
 *     interior of a 22x22 card with 4 px padding.
 *   - Card backs render the same purple-with-cyan-dot pattern, so the
 *     player only learns what's underneath by flipping. Matched cards
 *     dim down to MP_DIM tint and stay shown.
 *   - The "reveal" delay between the second flip and the resolution
 *     is 700 ms -- long enough to read the icon, short enough not to
 *     break flow. A non-match auto-flips both cards back; a match
 *     stays revealed and the state returns to Idle.
 *   - The mm:ss timer mirrors PhoneSlidingPuzzle (S80): 250 ms tick
 *     cadence, capped at 99:59, frozen the moment the last pair is
 *     resolved. A 250 ms tick is the sweet spot between perceived
 *     responsiveness and LVGL redraw cost.
 *   - "Best" records (least flips + fastest time to clear) live in
 *     RAM only -- they reset on screen pop, matching every other
 *     Phase-N game in the v1.0 roadmap.
 */
class PhoneMemoryMatch : public LVScreen, private InputListener {
public:
	PhoneMemoryMatch();
	virtual ~PhoneMemoryMatch() override;

	void onStart() override;
	void onStop() override;

	// Layout - 160x128 panel, status/soft-key bars 10 px each.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	// 4x4 grid of 22 px cards with a 2 px gap between them. Total
	// board is 94x94, centred on the 160 px panel (origin x = 33),
	// flush below the HUD strip (origin y = 22). Bottom edge at 116,
	// soft-key bar at 118 -> 2 px breathing space.
	static constexpr uint8_t BoardCols = 4;
	static constexpr uint8_t BoardRows = 4;
	static constexpr uint8_t CardCount = BoardCols * BoardRows; // 16
	static constexpr uint8_t PairCount = CardCount / 2;         // 8
	static constexpr lv_coord_t CardPx = 22;
	static constexpr lv_coord_t CardGap = 2;
	static constexpr lv_coord_t BoardPxW = CardPx * BoardCols + CardGap * (BoardCols - 1); // 94
	static constexpr lv_coord_t BoardPxH = CardPx * BoardRows + CardGap * (BoardRows - 1); // 94
	static constexpr lv_coord_t BoardOriginX = (160 - BoardPxW) / 2; // 33
	static constexpr lv_coord_t BoardOriginY = StatusBarH + HudH;    // 22

	// Reveal window between the player's second flip and the auto-
	// resolution. 700 ms is the readable-but-snappy zone. See the
	// notes above for why we don't make this user-configurable yet.
	static constexpr uint16_t kRevealMs = 700;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudFlipsLabel = nullptr;
	lv_obj_t* hudPairsLabel = nullptr;
	lv_obj_t* hudTimerLabel = nullptr;
	lv_obj_t* overlayLabel  = nullptr;

	// One sprite + one icon parent per card slot. Icons are children
	// of `cardIcons[i]` so we can clean / redraw a single card cheaply
	// without touching its sibling cards' rectangles.
	lv_obj_t* cardSprites[CardCount];
	lv_obj_t* cardIcons[CardCount];

	// ---- game state ---------------------------------------------------
	enum class CardState : uint8_t {
		Hidden  = 0,
		Shown   = 1,
		Matched = 2,
	};

	enum class GameState : uint8_t {
		Idle,
		Showing,
		Resolving,
		Won,
	};

	// Per-card icon kind (0..7). Two cards share each kind id.
	uint8_t   kinds[CardCount];
	CardState states[CardCount];

	GameState state = GameState::Idle;
	int8_t    firstFlipped  = -1; // valid when state == Showing/Resolving
	int8_t    secondFlipped = -1; // valid when state == Resolving

	// Cursor in card coordinates (0..BoardCols-1, 0..BoardRows-1).
	uint8_t cursorCol = 0;
	uint8_t cursorRow = 0;

	// Bookkeeping.
	uint16_t flips        = 0;
	uint8_t  pairsCleared = 0;
	uint16_t bestFlips    = 0;   // 0 = no record yet
	uint32_t bestMillis   = 0;   // 0 = no record yet
	uint32_t startMillis  = 0;
	uint32_t finishMillis = 0;   // captured on Won

	lv_timer_t* tickTimer    = nullptr; // mm:ss heartbeat (250 ms)
	lv_timer_t* resolveTimer = nullptr; // one-shot reveal window

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildCards();

	// ---- state transitions --------------------------------------------
	void newRound();
	void flipAt(uint8_t col, uint8_t row);
	void scheduleResolve();
	void resolvePair();
	void winMatch();

	// ---- helpers ------------------------------------------------------
	uint8_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint8_t>(row * BoardCols + col);
	}
	bool timerStarted() const { return startMillis != 0; }
	void ensureTimerStarted();

	// ---- rendering ----------------------------------------------------
	void renderAllCards();
	void renderCard(uint8_t cell);
	void renderCursor();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	// Pixel-art icon dispatch. Draws kind 0..7 inside the 14x14
	// interior of the card sprite (4 px padding from the card edge).
	void paintIcon(lv_obj_t* iconParent, uint8_t kind);

	// Deal a fresh shuffle of the eight pairs into kinds[].
	void shuffleDeck();

	// ---- timer helpers ------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	void cancelResolveTimer();
	static void onResolveStatic(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEMEMORYMATCH_H
