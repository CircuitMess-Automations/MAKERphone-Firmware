#ifndef MAKERPHONE_PHONETOWERDEFENCE_H
#define MAKERPHONE_PHONETOWERDEFENCE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneTowerDefence (S100)
 *
 * Phase-N arcade entry: a single-lane tower defence game with eight
 * scripted waves. Enemies march left-to-right along a horizontal lane;
 * the player builds and upgrades up to six towers along the lane,
 * earning cash for each kill and a bonus on every wave clear. Lose
 * five lives to enemies that reach the right edge and the base falls;
 * survive all eight waves and the round is won.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### |  <- PhoneStatusBar  (10 px)
 *   |   W1/8        $50         L:5      |  <- HUD strip       (~10 px)
 *   |   PRESS 2 TO START W1              |  <- state hint      (~9 px)
 *   |  ===== <-< LANE >-> ============== |  <- lane strip      (12 px)
 *   |        e        e                  |  <- enemies in lane
 *   |    [TOWER] [TOWER] [    ] [    ]   |  <- 6 tower slots   (16 px)
 *   |   BUILD $30                        |  <- cost hint       (~7 px)
 *   |    PLAY                  BACK      |  <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_4 / BTN_LEFT  : cursor moves to previous slot.
 *   - BTN_6 / BTN_RIGHT : cursor moves to next slot.
 *   - BTN_5 / BTN_ENTER : build a tower in the focused slot, or upgrade
 *                         it one level if it already has a tower.
 *                         In Idle the same key starts a new round; in
 *                         Won/Lost it returns to Idle for a rematch.
 *   - BTN_2             : start the next wave (only in Building).
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- intro overlay; ENTER -> Building (round reset).
 *   Building  -- between waves; user spends cash on towers; press
 *                BTN_2 -> Playing (next wave starts).
 *   Playing   -- physics tick (30 Hz); enemies walk + towers fire.
 *                Wave clears -> Building (or Won, after wave 8).
 *                lives <= 0 -> Lost.
 *   Won       -- final overlay "VICTORY!". ENTER -> Idle.
 *   Lost      -- final overlay "BASE LOST". ENTER -> Idle.
 *
 * Implementation notes:
 *  - 100% code-only -- the lane is one dim strip with two coloured
 *    end-caps for spawn / goal, towers are plain coloured rectangles,
 *    enemies are tiny coloured squares. No SPIFFS asset cost.
 *  - Enemy x position in Q8 fixed-point (pixels << 8) so the physics
 *    bookkeeping stays integer-only on the ESP32 -- matches the
 *    PhoneBrickBreaker / PhoneAirHockey style.
 *  - Tower firing is hitscan: every cooldown tick the tower picks the
 *    most-advanced enemy in range and inflicts level-equal damage.
 *    A short flash on tower + enemy gives visual feedback without the
 *    cost of moving bullet sprites.
 *  - Six tower slots, sized so the row fits cleanly on the 160 px
 *    display: 24 px wide each, centres at x = 20, 44, 68, 92, 116, 140.
 *  - Wave compositions are baked at file scope so the sequencing is
 *    deterministic and easy to balance later. Spawn interval shrinks
 *    with the wave number so the late game feels frantic.
 *  - Enemy pool sized to 14 -- comfortably above any single wave's
 *    on-screen population, since fast enemies are short-lived and the
 *    pool only needs to cover the spawn cadence vs. cross time.
 */
class PhoneTowerDefence : public LVScreen, private InputListener {
public:
	PhoneTowerDefence();
	virtual ~PhoneTowerDefence() override;

	void onStart() override;
	void onStop() override;

	// ---- Layout constants (160 x 128 panel) -----------------------------
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;

	// HUD strip: wave / cash / lives.
	static constexpr lv_coord_t HudY       = 11;
	// State hint label (one short line under the HUD).
	static constexpr lv_coord_t HintY      = 22;

	// Lane: a 12-px-tall strip across the full width. Enemies walk along it.
	static constexpr lv_coord_t LaneX      = 0;
	static constexpr lv_coord_t LaneY      = 44;
	static constexpr lv_coord_t LaneW      = 160;
	static constexpr lv_coord_t LaneH      = 12;
	static constexpr lv_coord_t EnemyY     = 47;     // centred in lane
	static constexpr lv_coord_t EnemySize  = 6;

	// Tower row sits below the lane.
	static constexpr lv_coord_t TowerY     = 64;
	static constexpr lv_coord_t TowerSize  = 16;

	// Slot geometry: 6 slots, 24 px wide, centred (8 px margin each side).
	static constexpr uint8_t   SlotCount   = 6;
	static constexpr lv_coord_t SlotX0     = 8;
	static constexpr lv_coord_t SlotW      = 24;

	// Cost hint label (just below the tower row).
	static constexpr lv_coord_t CostHintY  = 84;

	// ---- Match tuning --------------------------------------------------
	static constexpr uint8_t  WaveCount    = 8;
	static constexpr uint8_t  MaxEnemies   = 14;
	static constexpr int16_t  StartCash    = 50;
	static constexpr int8_t   StartLives   = 5;
	static constexpr int16_t  BuildCost    = 30;
	static constexpr int16_t  UpgL2Cost    = 25;
	static constexpr int16_t  UpgL3Cost    = 35;
	static constexpr int16_t  WaveBonus    = 20;

	// ---- Tick / physics constants --------------------------------------
	static constexpr uint32_t TickMs       = 33;
	static constexpr int16_t  Q8           = 256;
	// Goal X (lane-local px). When an enemy's centre crosses this, the
	// player loses a life and the enemy is despawned.
	static constexpr int16_t  GoalX        = LaneW;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	lv_obj_t* hudWaveLabel  = nullptr;
	lv_obj_t* hudCashLabel  = nullptr;
	lv_obj_t* hudLivesLabel = nullptr;

	lv_obj_t* hintLabel     = nullptr;     // state hint under HUD
	lv_obj_t* costHintLabel = nullptr;     // build / upgrade cost preview

	lv_obj_t* laneStrip     = nullptr;
	lv_obj_t* spawnCap      = nullptr;     // little cyan strip on the left
	lv_obj_t* goalCap       = nullptr;     // sunset orange strip on the right

	struct Tower {
		uint8_t   level      = 0;          // 0 = empty, 1..3 = built level
		int16_t   cooldown   = 0;          // ticks until next fire
		int16_t   flashTicks = 0;          // tick countdown for muzzle flash
		lv_obj_t* node       = nullptr;
	};
	struct Enemy {
		bool      active     = false;
		uint8_t   type       = 0;          // 0 basic, 1 fast, 2 tank
		int16_t   hp         = 0;
		int32_t   xQ8        = 0;          // lane-local x in Q8 px
		int16_t   flashTicks = 0;          // tick countdown for hit flash
		lv_obj_t* node       = nullptr;
	};

	Tower towers[SlotCount];
	Enemy enemies[MaxEnemies];

	// ---- game state ----------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Building,
		Playing,
		Won,
		Lost,
	};
	GameState state = GameState::Idle;

	uint8_t cursor       = 0;              // 0..SlotCount-1
	uint8_t waveNum      = 0;              // 0 = before W1; 1..8 = current/last
	int16_t cash         = StartCash;
	int8_t  lives        = StartLives;

	// Wave spawn tracking (counts of basic/fast/tank still to send).
	uint8_t waveRemaining[3] = { 0, 0, 0 };
	uint8_t spawnRot         = 0;          // round-robin offset across types
	int16_t spawnTimer       = 0;
	int16_t spawnIntervalTicks = 28;

	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers -------------------------------------------------
	void buildHud();
	void buildHints();
	void buildLane();
	void buildTowers();
	void buildEnemies();

	// ---- state transitions ---------------------------------------------
	void enterIdle();
	void enterBuilding();
	void startNextWave();
	void endMatchWon();
	void endMatchLost();

	// ---- core game ops -------------------------------------------------
	void physicsStep();
	void tryBuildOrUpgrade();
	void spawnEnemy(uint8_t type);
	void killEnemy(uint8_t i);
	bool waveDoneSpawning() const;
	bool noActiveEnemies() const;

	// ---- helpers -------------------------------------------------------
	int16_t slotCenterX(uint8_t slot) const;
	int16_t towerRange(uint8_t level) const;
	int16_t towerCooldownTicks(uint8_t level) const;
	int16_t enemySpeedQ8(uint8_t type) const;
	int16_t enemyHp(uint8_t type) const;
	int16_t enemyReward(uint8_t type) const;
	int16_t upgradeCost(uint8_t currentLevel) const;
	lv_color_t enemyColor(uint8_t type) const;
	lv_color_t towerBgColor(uint8_t level) const;

	// ---- rendering -----------------------------------------------------
	void refreshHud();
	void refreshTower(uint8_t i);
	void refreshAllTowers();
	void refreshEnemy(uint8_t i);
	void refreshSoftKeys();
	void refreshHint();
	void refreshCostHint();

	// ---- timers --------------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickTimerStatic(lv_timer_t* timer);

	// ---- input ---------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONETOWERDEFENCE_H
