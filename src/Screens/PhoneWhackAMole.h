#ifndef MAKERPHONE_PHONEWHACKAMOLE_H
#define MAKERPHONE_PHONEWHACKAMOLE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneWhackAMole (S90)
 *
 * Phase-N arcade entry: a dialer-key-driven reaction game. Nine
 * "holes" are arranged in a 3x3 grid that mirrors the dialer layout
 * (top row = keys 1/2/3, middle row = 4/5/6, bottom row = 7/8/9),
 * so the keypad becomes a literal one-to-one map of the playfield.
 * Cyan moles pop up at random holes for a short window. The player
 * "whacks" them by pressing the matching number key. A hit scores
 * a point; a mole that escapes without being whacked costs one life.
 * Three lives and the round ends.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | SCORE 04   HI 12      LIVES OOO   | <- HUD strip       (12 px)
 *   |       +------+------+------+      |
 *   |       |  1   |  2   |  3   |      |
 *   |       +------+------+------+      |    <- 3x3 hole grid mapping
 *   |       |  4   |  5   |  6   |      |       to dialer keys 1-9.
 *   |       +------+------+------+      |       Cells are 36x30 px,
 *   |       |  7   |  8   |  9   |      |       grid 108x90, centred
 *   |       +------+------+------+      |       at x = 26, y = 24.
 *   |   START              BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_1 .. BTN_9 : whack the mole at the matching hole. Pressing
 *                      a hole that has no mole still counts as a tap
 *                      but is harmless (no score, no life lost) so
 *                      mashing isn't immediately punished.
 *   - BTN_5 / BTN_ENTER : during Idle / GameOver, start a new round.
 *                      During Play, BTN_5 doubles as "whack hole 5"
 *                      so the dialer hand position works literally.
 *   - BTN_R         : restart the round (works in every state).
 *   - BTN_BACK (B)  : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle       -- start screen with the rules in the centre overlay.
 *                 Soft-keys: START / BACK.
 *   Playing    -- moles pop up, ticks drive spawn + lifetime. Soft-keys:
 *                 WHACK / BACK.
 *   GameOver   -- "GAME OVER" overlay, score + hi-score shown. Soft-keys:
 *                 AGAIN / BACK.
 *
 * Implementation notes:
 *  - 100% code-only -- every hole is a small rounded LVGL panel, the
 *    mole body is a single cyan rounded rectangle that toggles visible
 *    when a mole spawns there, and the score / hi-score / lives badges
 *    are plain pixelbasic7 labels. No SPIFFS asset cost.
 *  - Difficulty ramp: as the running score climbs, both the spawn
 *    interval and the per-mole lifetime shorten linearly, clamped at
 *    a floor that's still humanly reachable. The ramp is intentionally
 *    gentle for the first dozen hits so first-time players ease in,
 *    then accelerates so a competent player still feels a real ceiling.
 *  - Lives are rendered as three small cyan dots in the HUD's right
 *    slot; a lost life dims to muted purple instead of being removed
 *    so the row's geometry doesn't reflow on every miss.
 *  - The tick driver is a single repeating lv_timer running at ~60 ms,
 *    started in onStart() and stopped in onStop() so the screen can
 *    survive being pushed and re-popped without leaking timers. A
 *    fresh round resets the elapsed counters but reuses the same
 *    timer object; only state matters, not timer identity.
 *  - Hit / miss visual feedback: when a mole is whacked, the hole
 *    flashes orange for a few ticks before clearing; when a mole
 *    escapes, the hole flashes dim purple. Both are driven by a
 *    short countdown on the cell so we don't need a second timer.
 *  - Session high score (best score across rounds) survives "new
 *    round" but resets when the screen is popped, matching every
 *    other Phone* game in the v1.0 + Phase-N roadmap.
 */
class PhoneWhackAMole : public LVScreen, private InputListener {
public:
	PhoneWhackAMole();
	virtual ~PhoneWhackAMole() override;

	void onStart() override;
	void onStop() override;

	// ---- Layout constants (160 x 128 panel) ---------------------------
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	// 3x3 hole grid that mirrors the dialer's 1-9 layout. Cells are
	// 36 wide x 30 tall, total grid 108 x 90, centred horizontally on
	// the 160 px panel and tucked just under the HUD.
	static constexpr uint8_t   Cols       = 3;
	static constexpr uint8_t   Rows       = 3;
	static constexpr uint8_t   HoleCount  = Cols * Rows;
	static constexpr lv_coord_t CellW     = 36;
	static constexpr lv_coord_t CellH     = 30;
	static constexpr lv_coord_t GridW     = CellW * Cols;       // 108
	static constexpr lv_coord_t GridH     = CellH * Rows;       // 90
	static constexpr lv_coord_t GridX     = (160 - GridW) / 2;  // 26
	static constexpr lv_coord_t GridY     = StatusBarH + HudH;  // 22

	// ---- Game tuning --------------------------------------------------
	// Tick period of the gameplay loop. 60 ms is fine-grained enough that
	// per-mole lifetime + spawn interval feel responsive, while still
	// being light on the LVGL timer scheduler.
	static constexpr uint16_t kTickMs       = 60;

	// Per-mole lifetime ramps from kMoleStartMs down to kMoleFloorMs
	// linearly across the first kRampHits hits. After that, lifetime
	// stays at the floor so the game doesn't become impossible.
	static constexpr uint16_t kMoleStartMs  = 1500;
	static constexpr uint16_t kMoleFloorMs  =  500;
	static constexpr uint16_t kRampHits     =   24;

	// Time between spawn attempts. Same linear ramp, different floor:
	// the spawn interval can dip to 320 ms which keeps two-on-screen
	// from being trivial without being mash-only.
	static constexpr uint16_t kSpawnStartMs = 1000;
	static constexpr uint16_t kSpawnFloorMs =  320;

	// Visual feedback windows (in ticks). 5 ticks * 60 ms = 300 ms.
	static constexpr uint8_t  kHitFlashTicks  = 5;
	static constexpr uint8_t  kMissFlashTicks = 5;

	// Lives per round.
	static constexpr uint8_t  kStartLives = 3;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudScoreLabel = nullptr;
	lv_obj_t* hudHiLabel    = nullptr;
	lv_obj_t* hudLivesLabel = nullptr;
	lv_obj_t* hudLivesDot[kStartLives] = { nullptr, nullptr, nullptr };

	lv_obj_t* overlayPanel  = nullptr;
	lv_obj_t* overlayTitle  = nullptr;
	lv_obj_t* overlayLines  = nullptr;

	// One panel + one digit-label + one mole-sprite per hole. The hole
	// panel is the cell background; the mole sprite is a child rect
	// that's hidden by default and shown when a mole spawns there.
	lv_obj_t* holePanels[HoleCount];
	lv_obj_t* holeLabels[HoleCount];
	lv_obj_t* holeMoles[HoleCount];

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,        // start screen, waiting for the user to begin
		Playing,     // moles popping, ticks running
		GameOver,    // round finished; overlay shown
	};

	enum class HoleVis : uint8_t {
		Empty   = 0, // dim, just shows the digit caption
		Mole    = 1, // mole is up; cyan sprite visible, digit dim
		HitFx   = 2, // brief orange flash after a successful whack
		MissFx  = 3, // brief dim-purple flash after an escaped mole
	};

	GameState state = GameState::Idle;

	// Per-hole runtime: lifetime ms remaining for an active mole, or
	// flash-tick countdown for HitFx / MissFx. Empty holes ignore both.
	HoleVis  holeVis[HoleCount];
	int16_t  holeLifeMs[HoleCount];
	uint8_t  holeFlashTicks[HoleCount];

	// Spawn cadence: ms remaining until the next spawn attempt.
	int16_t  spawnTimerMs = 0;

	// Scoring.
	uint16_t score      = 0;
	uint16_t highScore  = 0;
	uint8_t  lives      = kStartLives;

	// Running tick driver. One repeating lv_timer is enough -- spawn
	// timing and per-mole lifetimes are both decremented from the same
	// tick callback.
	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildHoles();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startRound();
	void endRound();

	// Game-loop step (called from the tick timer). Decrements lifetimes,
	// resolves escaped moles, decrements flash-fx counters, and fires
	// new spawns when the spawn timer rolls over.
	void tick();

	// Mole spawn: pick a random empty hole and pop a mole there with
	// the current lifetime budget. Silent no-op if every hole is full
	// so we don't crowd the board.
	void spawnMole();

	// Player whack on hole index 0..8. Returns true if a mole was hit.
	bool whackHole(uint8_t holeIdx);

	// ---- helpers ------------------------------------------------------
	uint8_t  emptyHoleCount() const;
	uint16_t currentMoleLifeMs() const;
	uint16_t currentSpawnIntervalMs() const;
	uint8_t  buttonToHoleIdx(uint i) const;

	// ---- rendering ----------------------------------------------------
	void renderAllHoles();
	void renderHole(uint8_t holeIdx);
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	// ---- timer helpers ------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEWHACKAMOLE_H
