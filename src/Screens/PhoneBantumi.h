#ifndef MAKERPHONE_PHONEBANTUMI_H
#define MAKERPHONE_PHONEBANTUMI_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneBantumi (S76)
 *
 * Phase-N arcade entry: the Nokia-classic Mancala (a.k.a. "Bantumi" on
 * the Nokia 3310). Two sides of six pits each, two stores, four stones
 * per pit at start, sown counter-clockwise. Player-vs-CPU only -- the
 * 16-button shift-register keypad is too cosy for split-screen pass-
 * and-play and the LoRa link is out of scope this session.
 *
 * Board layout (160 x 128, all coordinates in screen space):
 *   - PhoneStatusBar           : y =   0..  9 (10 px)
 *   - HUD strip ("MANCALA")    : y =  10.. 19 (10 px)
 *   - Playfield                : y =  20..117 (98 px)
 *       CPU pit row (top)      : y =  24.. 39
 *       Centre status text     : y =  60.. 70
 *       Player pit row (bottom): y =  80.. 95
 *       CPU store (left)       : y =  24.. 95   (full height)
 *       Player store (right)   : y =  24.. 95
 *   - PhoneSoftKeyBar          : y = 118..127 (10 px)
 *
 *   x layout (left -> right):
 *       2 px gutter | CPU store 14 px | 4 px gap |
 *       6 pits (16 px each, 2 px gaps) = 16*6 + 2*5 = 106 px |
 *       4 px gap | Player store 14 px | 2 px gutter
 *   total = 2 + 14 + 4 + 106 + 4 + 14 + 2 = 146 px, centred at x = 7.
 *
 * Board indexing (counter-clockwise from player perspective):
 *   pits[0..5]   = player pits, displayed bottom-row left-to-right.
 *   pits[6]      = player store (right side).
 *   pits[7..12]  = CPU pits, displayed top-row right-to-left, so the
 *                  visual `top column j` corresponds to `pits[12 - j]`.
 *   pits[13]     = CPU store (left side).
 *
 *   Sowing direction adds +1 mod 14, but skips the *opponent's* store
 *   (so the player sowing past index 6 next lands in 7, and from 12
 *   wraps to 0; the CPU sowing past index 13 next lands in 0, and from
 *   5 wraps to 7).
 *
 *   Capture rule (classic Mancala): if the last sown stone lands in
 *   one of *your* empty pits (and the opposite pit has stones), you
 *   capture both your stone and every stone in the opposite pit into
 *   your store. Opposite of pit i is `12 - i` (player <-> CPU).
 *
 *   Free turn rule: if the last sown stone lands in your own store,
 *   you go again.
 *
 *   Game end: when one side's six pits are all empty. The remaining
 *   stones on the other side are swept into that side's store. Player
 *   with the most stones wins; tie counts as a draw.
 *
 * Controls:
 *   - BTN_LEFT  / BTN_4 : move the cursor one pit left  (player turn).
 *   - BTN_RIGHT / BTN_6 : move the cursor one pit right (player turn).
 *   - BTN_ENTER (A)     : sow the highlighted pit.
 *                         On Idle / GameOver it (re)starts a match.
 *                         On Sowing it speed-finishes the animation.
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- "PRESS START" overlay, board reset, 4 stones / pit.
 *                ENTER -> PlayerTurn.
 *   PlayerTurn-- player picks a pit (cursor on 0..5).
 *                ENTER on a non-empty pit -> Sowing.
 *   CpuThink  -- short 350 ms beat where the CPU "thinks", then it
 *                picks a pit by simple greedy heuristic (extra-turn
 *                bias, then capture bias, then largest-stone fallback)
 *                and transitions -> Sowing.
 *   Sowing    -- animated: one stone dropped per ~120 ms tick. Honours
 *                the no-opponent-store rule. After the last stone:
 *                resolve capture / extra turn, then check game end.
 *   GameOver  -- final tally + winner banner overlay.
 *                ENTER -> Idle.
 *
 * Implementation notes:
 *   - 100% code-only -- every visual is plain `lv_obj` rectangles +
 *     pixelbasic7 labels. No SPIFFS asset cost.
 *   - Stones are not individually drawn -- pits show a numeric stone
 *     count in pixelbasic7. The 16x16 pit cell is too small to render
 *     14+ pebbles legibly, so we lean on the digit + colour cue.
 *   - The "last sown" pit pulses via a one-shot accent border for one
 *     tick after each drop, giving the sow a satisfying read on the
 *     160 px panel.
 *   - The CPU AI is intentionally tiny (one-ply). It keeps the binary
 *     small and the difficulty fair-but-beatable -- the classic Nokia
 *     Bantumi AI was nothing fancy either.
 */
class PhoneBantumi : public LVScreen, private InputListener {
public:
	PhoneBantumi();
	virtual ~PhoneBantumi() override;

	void onStart() override;
	void onStop() override;

	// Board geometry / counts.
	static constexpr uint8_t  PitCount      = 14;   // 6 + store + 6 + store
	static constexpr uint8_t  PlayerStore   = 6;
	static constexpr uint8_t  CpuStore      = 13;
	static constexpr uint8_t  StonesPerPit  = 4;

	// Layout (matches the diagram above).
	static constexpr lv_coord_t StatusBarH  = 10;
	static constexpr lv_coord_t SoftKeyH    = 10;
	static constexpr lv_coord_t HudY        = 10;
	static constexpr lv_coord_t HudH        = 10;
	static constexpr lv_coord_t PlayfieldY  = 20;
	static constexpr lv_coord_t PlayfieldH  = 98;

	static constexpr lv_coord_t StoreW      = 14;
	static constexpr lv_coord_t StoreH      = 72;   // CPU pit top to player pit bottom
	static constexpr lv_coord_t StoreYTop   = 24;

	static constexpr lv_coord_t PitW        = 16;
	static constexpr lv_coord_t PitH        = 16;
	static constexpr lv_coord_t PitGapX     = 2;

	static constexpr lv_coord_t CpuRowY     = 24;
	static constexpr lv_coord_t PlayerRowY  = 80;

	// Compose the leftmost pit X so the row centres in the playfield.
	// Total row width = 6 * PitW + 5 * PitGapX = 106. The pit row sits
	// between the two stores, with a 4 px gap on either side. CPU store
	// is at x = 2 + 0 = 2, w = 14, so first pit X = 2 + 14 + 4 = 20.
	static constexpr lv_coord_t CpuStoreX    = 2;
	static constexpr lv_coord_t PitsStartX   = 20;
	static constexpr lv_coord_t PlayerStoreX = 144;

	// Sowing tick (one stone dropped per N ms while in Sowing state).
	static constexpr uint32_t SowTickMs      = 120;
	// Beat between turn handover and CPU's first sow, so the player can
	// see who just played.
	static constexpr uint32_t CpuThinkMs     = 350;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* titleLabel    = nullptr;
	lv_obj_t* statusLabel   = nullptr;   // "YOUR TURN" / "CPU..." / etc.
	lv_obj_t* overlayLabel  = nullptr;   // game-state overlay banner

	// One sprite per pit / store. pitSprites[i] is the framed cell;
	// pitCountLabels[i] is the digit shown inside it.
	lv_obj_t* pitSprites[PitCount];
	lv_obj_t* pitCountLabels[PitCount];

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		PlayerTurn,
		CpuThink,
		Sowing,
		GameOver,
	};
	GameState state = GameState::Idle;

	// 14-cell board (player pits 0..5, player store 6, CPU pits 7..12,
	// CPU store 13).
	uint8_t pits[PitCount];

	// Cursor: which player pit (0..5) is currently highlighted.
	uint8_t cursor = 0;

	// Sowing animation bookkeeping. When state == Sowing, every
	// SowTickMs we drop one stone from `sowHand` into pit `sowNext`,
	// where sowNext advances counter-clockwise from the picked pit.
	// `sowOwnerIsPlayer` decides which store to skip while sowing.
	uint8_t  sowHand           = 0;
	uint8_t  sowNext           = 0;       // next pit index to receive a stone
	uint8_t  sowLastIdx        = 0;       // last pit that received a stone
	bool     sowOwnerIsPlayer  = true;    // true: player sowing; false: CPU
	bool     sowExtraTurn      = false;   // last drop landed in own store

	// Last winner banner ("YOU WIN" / "CPU WINS" / "DRAW").
	enum class WinResult : uint8_t {
		None = 0,
		Player,
		Cpu,
		Draw,
	};
	WinResult lastResult = WinResult::None;

	lv_timer_t* sowTimer    = nullptr;    // ticks during Sowing
	lv_timer_t* cpuTimer    = nullptr;    // one-shot beat before CPU sow

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildBoard();
	void buildOverlay();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startMatch();
	void beginPlayerTurn();
	void beginCpuTurn();
	void beginSow(uint8_t fromPit, bool ownerIsPlayer);
	void finishSow();             // post-sow rules (capture / extra turn / end)
	void endMatch();

	// ---- core game ops ------------------------------------------------
	uint8_t advanceIndex(uint8_t idx, bool ownerIsPlayer) const;
	bool    sideEmpty(bool playerSide) const;
	void    sweepRemaining();
	bool    pickCpuMove(uint8_t& out) const;
	uint8_t opposite(uint8_t idx) const { return 12 - idx; }
	bool    isPlayerPit(uint8_t idx) const { return idx <= 5; }
	bool    isCpuPit(uint8_t idx)    const { return idx >= 7 && idx <= 12; }

	// ---- rendering ----------------------------------------------------
	void render();
	void refreshPits();
	void refreshSelection();
	void refreshStatusLine();
	void refreshSoftKeys();
	void refreshOverlay();

	// ---- timers -------------------------------------------------------
	void startSowTimer();
	void stopSowTimer();
	void startCpuTimer();
	void stopCpuTimer();
	static void onSowTickStatic(lv_timer_t* timer);
	static void onCpuTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEBANTUMI_H
