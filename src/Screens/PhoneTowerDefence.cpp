#include "PhoneTowerDefence.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside the rest of the Phase-N arcade without a seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// Wave compositions: how many of each enemy type spawn in each wave.
// Order is { basic, fast, tank }. Tunes the difficulty curve so the
// early waves teach the user what towers do, the middle waves mix in
// fast runners that punish a weak left flank, and the late waves
// pressure with tanks that need overlapping fire to bring down.
struct WaveDef { uint8_t basic; uint8_t fast; uint8_t tank; };

constexpr WaveDef Waves[8] = {
	{ 6, 0, 0 },   // W1 -- six basic walkers, easy intro.
	{ 7, 2, 0 },   // W2 -- a couple fast runners join in.
	{ 6, 4, 0 },   // W3 -- more runners; cash starts mattering.
	{ 5, 4, 2 },   // W4 -- first tanks; need an upgraded tower.
	{ 3, 7, 2 },   // W5 -- runner-heavy.
	{ 6, 5, 3 },   // W6 -- mixed.
	{ 4, 6, 4 },   // W7 -- tank wall.
	{ 4, 8, 5 },   // W8 -- final wave; needs full upgrades.
};

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneTowerDefence::PhoneTowerDefence()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	// Full-screen container, no scrollbars, no padding -- same blank-canvas
	// pattern PhoneAirHockey / PhoneTetris use. Children below either pin
	// themselves with absolute coords or fill via LVGL primitives.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The
	// status bar / HUD / lane / towers / soft-keys overlay it without
	// per-child opacity gymnastics. Same pattern every Phase-N screen
	// follows.
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildHints();
	buildLane();
	buildTowers();
	buildEnemies();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PLAY");
	softKeys->setRight("BACK");

	enterIdle();
}

PhoneTowerDefence::~PhoneTowerDefence() {
	stopTickTimer();
	// All children are parented to obj; LVGL frees them recursively when
	// the screen's obj is destroyed by the LVScreen base destructor.
}

void PhoneTowerDefence::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneTowerDefence::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneTowerDefence::buildHud() {
	// Wave indicator (left-pinned, cyan).
	hudWaveLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudWaveLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudWaveLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudWaveLabel, "W1/8");
	lv_obj_set_pos(hudWaveLabel, 4, HudY);

	// Cash indicator (centred, sunset orange so it reads as currency).
	hudCashLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudCashLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudCashLabel, MP_ACCENT, 0);
	lv_label_set_text(hudCashLabel, "$50");
	lv_obj_set_align(hudCashLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudCashLabel, HudY);

	// Lives indicator (right-pinned, warm cream).
	hudLivesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudLivesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudLivesLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(hudLivesLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(hudLivesLabel, "L:5");
	lv_obj_set_align(hudLivesLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(hudLivesLabel, -4, HudY);
}

void PhoneTowerDefence::buildHints() {
	// State hint -- short single-line label under the HUD telling the
	// user what the current state expects from them ("PRESS PLAY" /
	// "PRESS 2 START W1" / "VICTORY!" / etc.). Centred on the screen.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "PRESS PLAY");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, HintY);

	// Cost hint -- sits just under the tower row and tracks the cursor
	// slot. Reads "BUILD $30" on an empty slot, "UPGR $25" on an L1 etc.,
	// and "L3 MAX" on a fully-upgraded tower. Hidden in Idle/Won/Lost.
	costHintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(costHintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(costHintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(costHintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(costHintLabel, "");
	lv_obj_set_align(costHintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(costHintLabel, CostHintY);
}

void PhoneTowerDefence::buildLane() {
	// Lane backdrop -- a 12-px-tall strip across the full width with a
	// faint dim purple fill so it reads as a path enemies are walking
	// on top of the synthwave wallpaper without obscuring it.
	laneStrip = lv_obj_create(obj);
	lv_obj_remove_style_all(laneStrip);
	lv_obj_set_size(laneStrip, LaneW, LaneH);
	lv_obj_set_pos(laneStrip, LaneX, LaneY);
	lv_obj_set_style_bg_color(laneStrip, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(laneStrip, LV_OPA_50, 0);
	lv_obj_set_style_border_color(laneStrip, MP_DIM, 0);
	lv_obj_set_style_border_width(laneStrip, 1, 0);
	lv_obj_set_style_radius(laneStrip, 0, 0);
	lv_obj_set_style_pad_all(laneStrip, 0, 0);
	lv_obj_clear_flag(laneStrip, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(laneStrip, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(laneStrip, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Spawn cap on the left -- a slim cyan strip that flags where the
	// enemies appear from. Reads as a portal mouth at this resolution.
	spawnCap = lv_obj_create(obj);
	lv_obj_remove_style_all(spawnCap);
	lv_obj_set_size(spawnCap, 3, LaneH - 2);
	lv_obj_set_pos(spawnCap, LaneX + 1, LaneY + 1);
	lv_obj_set_style_bg_color(spawnCap, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(spawnCap, LV_OPA_70, 0);
	lv_obj_set_style_border_width(spawnCap, 0, 0);
	lv_obj_set_style_radius(spawnCap, 0, 0);
	lv_obj_clear_flag(spawnCap, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(spawnCap, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(spawnCap, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Goal cap on the right -- sunset orange so the player understands
	// "anything reaching this end costs me a life".
	goalCap = lv_obj_create(obj);
	lv_obj_remove_style_all(goalCap);
	lv_obj_set_size(goalCap, 3, LaneH - 2);
	lv_obj_set_pos(goalCap, LaneX + LaneW - 4, LaneY + 1);
	lv_obj_set_style_bg_color(goalCap, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(goalCap, LV_OPA_70, 0);
	lv_obj_set_style_border_width(goalCap, 0, 0);
	lv_obj_set_style_radius(goalCap, 0, 0);
	lv_obj_clear_flag(goalCap, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(goalCap, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(goalCap, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneTowerDefence::buildTowers() {
	// Build six tower visuals. Each tower is a single lv_obj rectangle
	// whose colour / border tells the user (a) whether the slot is
	// empty, (b) the current upgrade level, and (c) which slot the
	// cursor is on. The empty / built styles are repainted in
	// refreshTower() so the build code stays small.
	for(uint8_t i = 0; i < SlotCount; ++i) {
		Tower& t = towers[i];
		t.level      = 0;
		t.cooldown   = 0;
		t.flashTicks = 0;

		t.node = lv_obj_create(obj);
		lv_obj_remove_style_all(t.node);
		lv_obj_set_size(t.node, TowerSize, TowerSize);
		lv_obj_set_pos(t.node, slotCenterX(i) - TowerSize / 2, TowerY);
		lv_obj_set_style_radius(t.node, 1, 0);
		lv_obj_set_style_pad_all(t.node, 0, 0);
		lv_obj_clear_flag(t.node, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(t.node, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(t.node, LV_OBJ_FLAG_IGNORE_LAYOUT);
		refreshTower(i);
	}
}

void PhoneTowerDefence::buildEnemies() {
	// Pre-allocate the enemy pool. Each enemy gets its own lv_obj that
	// stays parked off-screen (or hidden) until spawned. This avoids
	// allocating LVGL objects mid-tick and keeps the per-tick budget
	// stable on the ESP32.
	for(uint8_t i = 0; i < MaxEnemies; ++i) {
		Enemy& e = enemies[i];
		e.active = false;
		e.node = lv_obj_create(obj);
		lv_obj_remove_style_all(e.node);
		lv_obj_set_size(e.node, EnemySize, EnemySize);
		lv_obj_set_pos(e.node, -EnemySize, EnemyY);
		lv_obj_set_style_bg_color(e.node, MP_HIGHLIGHT, 0);
		lv_obj_set_style_bg_opa(e.node, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(e.node, 0, 0);
		lv_obj_set_style_radius(e.node, 1, 0);
		lv_obj_clear_flag(e.node, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(e.node, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(e.node, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_add_flag(e.node, LV_OBJ_FLAG_HIDDEN);
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneTowerDefence::enterIdle() {
	stopTickTimer();
	state = GameState::Idle;
	waveNum = 0;
	cash = StartCash;
	lives = StartLives;
	cursor = 0;

	for(uint8_t i = 0; i < SlotCount; ++i) {
		towers[i].level = 0;
		towers[i].cooldown = 0;
		towers[i].flashTicks = 0;
	}
	for(uint8_t i = 0; i < MaxEnemies; ++i) {
		Enemy& e = enemies[i];
		e.active = false;
		e.flashTicks = 0;
		if(e.node) lv_obj_add_flag(e.node, LV_OBJ_FLAG_HIDDEN);
	}
	waveRemaining[0] = waveRemaining[1] = waveRemaining[2] = 0;
	spawnRot = 0;
	spawnTimer = 0;
	spawnIntervalTicks = 28;

	refreshAllTowers();
	refreshHud();
	refreshSoftKeys();
	refreshHint();
	refreshCostHint();
}

void PhoneTowerDefence::enterBuilding() {
	stopTickTimer();
	state = GameState::Building;
	refreshHud();
	refreshSoftKeys();
	refreshHint();
	refreshCostHint();
	refreshAllTowers();   // re-tint cursor slot (state-dependent)
}

void PhoneTowerDefence::startNextWave() {
	if(state != GameState::Building && state != GameState::Idle) return;
	if(waveNum >= WaveCount) return;

	waveNum++;
	const WaveDef& w = Waves[waveNum - 1];
	waveRemaining[0] = w.basic;
	waveRemaining[1] = w.fast;
	waveRemaining[2] = w.tank;
	spawnRot = 0;
	spawnTimer = 12;          // brief lead-in before first spawn

	// Spawn cadence shrinks with the wave number so late waves feel
	// frantic without changing per-enemy speed (which would mess with
	// the tower-range tuning). 30 - 2*wave, clamped to 14 ticks.
	int16_t interval = static_cast<int16_t>(30 - waveNum * 2);
	if(interval < 14) interval = 14;
	spawnIntervalTicks = interval;

	state = GameState::Playing;
	refreshHud();
	refreshSoftKeys();
	refreshHint();
	refreshCostHint();
	refreshAllTowers();
	startTickTimer();
}

void PhoneTowerDefence::endMatchWon() {
	stopTickTimer();
	state = GameState::Won;
	refreshHud();
	refreshSoftKeys();
	refreshHint();
	refreshCostHint();
	refreshAllTowers();
}

void PhoneTowerDefence::endMatchLost() {
	stopTickTimer();
	state = GameState::Lost;
	refreshHud();
	refreshSoftKeys();
	refreshHint();
	refreshCostHint();
	refreshAllTowers();
	// Hide any still-active enemies so the BASE LOST screen doesn't
	// have ghost walkers parading across it.
	for(uint8_t i = 0; i < MaxEnemies; ++i) {
		Enemy& e = enemies[i];
		if(!e.active) continue;
		e.active = false;
		if(e.node) lv_obj_add_flag(e.node, LV_OBJ_FLAG_HIDDEN);
	}
}

// ===========================================================================
// core game ops
// ===========================================================================

void PhoneTowerDefence::physicsStep() {
	// --- 1. Move enemies ---------------------------------------------------
	// Walk every active enemy rightward at its type-specific speed and
	// retire any that reach the goal line (costing the player a life).
	for(uint8_t i = 0; i < MaxEnemies; ++i) {
		Enemy& e = enemies[i];
		if(!e.active) continue;
		e.xQ8 += enemySpeedQ8(e.type);
		if(e.flashTicks > 0) {
			if(--e.flashTicks == 0) refreshEnemy(i);
		}
		const int16_t ex = static_cast<int16_t>(e.xQ8 >> 8);
		if(ex >= GoalX) {
			e.active = false;
			if(e.node) lv_obj_add_flag(e.node, LV_OBJ_FLAG_HIDDEN);
			if(lives > 0) lives--;
			refreshHud();
			if(lives <= 0) {
				lives = 0;
				endMatchLost();
				return;
			}
		}
	}

	// --- 2. Tower fire ----------------------------------------------------
	for(uint8_t i = 0; i < SlotCount; ++i) {
		Tower& t = towers[i];
		if(t.flashTicks > 0) {
			if(--t.flashTicks == 0) refreshTower(i);
		}
		if(t.level == 0) continue;
		if(t.cooldown > 0) {
			t.cooldown--;
			continue;
		}

		// Pick the most-advanced enemy in range -- focusing on the
		// nearest-to-goal target makes the towers feel smart and saves
		// lives in dense waves.
		const int16_t towerX = slotCenterX(i);
		const int16_t range  = towerRange(t.level);
		int16_t bestIdx = -1;
		int32_t bestX   = -1;     // lane-local enemies can't be < 0 in flight
		for(uint8_t j = 0; j < MaxEnemies; ++j) {
			Enemy& e = enemies[j];
			if(!e.active) continue;
			const int16_t ex = static_cast<int16_t>(e.xQ8 >> 8);
			int16_t dx = ex - towerX;
			if(dx < 0) dx = -dx;
			if(dx <= range && e.xQ8 > bestX) {
				bestX = e.xQ8;
				bestIdx = static_cast<int16_t>(j);
			}
		}
		if(bestIdx < 0) continue;

		Enemy& target = enemies[bestIdx];
		target.hp -= static_cast<int16_t>(t.level);    // dmg = level (1..3)
		target.flashTicks = 2;
		t.flashTicks = 3;
		t.cooldown   = towerCooldownTicks(t.level);
		refreshTower(i);

		if(target.hp <= 0) {
			cash += enemyReward(target.type);
			killEnemy(static_cast<uint8_t>(bestIdx));
			refreshHud();
			refreshCostHint();
		} else {
			refreshEnemy(static_cast<uint8_t>(bestIdx));
		}
	}

	// --- 3. Spawn new enemies ---------------------------------------------
	if(state == GameState::Playing && !waveDoneSpawning()) {
		if(--spawnTimer <= 0) {
			// Round-robin across the three types so the lane mixes up
			// rather than dumping all the basics first then a wall of
			// tanks. The rotation still respects the per-type counts.
			for(uint8_t k = 0; k < 3; ++k) {
				const uint8_t typ = (spawnRot + k) % 3;
				if(waveRemaining[typ] > 0) {
					spawnEnemy(typ);
					waveRemaining[typ]--;
					spawnRot = static_cast<uint8_t>((typ + 1) % 3);
					break;
				}
			}
			spawnTimer = spawnIntervalTicks;
		}
	}

	// --- 4. Render enemies (positional only) ------------------------------
	for(uint8_t i = 0; i < MaxEnemies; ++i) {
		Enemy& e = enemies[i];
		if(!e.active || e.node == nullptr) continue;
		const int16_t ex = static_cast<int16_t>(e.xQ8 >> 8);
		lv_obj_set_x(e.node, ex - EnemySize / 2);
	}

	// --- 5. Wave-cleared check -------------------------------------------
	if(state == GameState::Playing && waveDoneSpawning() && noActiveEnemies()) {
		cash += WaveBonus;
		refreshHud();
		if(waveNum >= WaveCount) {
			endMatchWon();
		} else {
			enterBuilding();
		}
	}
}

void PhoneTowerDefence::tryBuildOrUpgrade() {
	if(state != GameState::Building && state != GameState::Playing) return;
	Tower& t = towers[cursor];
	if(t.level >= 3) return;
	const int16_t cost = upgradeCost(t.level);
	if(cash < cost) return;
	cash -= cost;
	t.level = static_cast<uint8_t>(t.level + 1);
	t.cooldown = 0;
	t.flashTicks = 0;
	refreshTower(cursor);
	refreshHud();
	refreshCostHint();
	if(softKeys) softKeys->flashLeft();
}

void PhoneTowerDefence::spawnEnemy(uint8_t type) {
	// Find a free pool slot and bring it on-screen at the spawn point.
	for(uint8_t i = 0; i < MaxEnemies; ++i) {
		Enemy& e = enemies[i];
		if(e.active) continue;
		e.active = true;
		e.type = type;
		e.hp = enemyHp(type);
		// Start one body off the left edge so the enemy slides into
		// the lane rather than popping in.
		e.xQ8 = static_cast<int32_t>(-EnemySize) * Q8;
		e.flashTicks = 0;
		refreshEnemy(i);
		return;
	}
	// Pool exhausted -- silently drop the spawn. The pool size is sized
	// for the worst-case wave so this should never fire in practice;
	// dropping is preferable to crashing on overflow.
}

void PhoneTowerDefence::killEnemy(uint8_t i) {
	Enemy& e = enemies[i];
	e.active = false;
	e.flashTicks = 0;
	if(e.node) lv_obj_add_flag(e.node, LV_OBJ_FLAG_HIDDEN);
}

bool PhoneTowerDefence::waveDoneSpawning() const {
	return (waveRemaining[0] + waveRemaining[1] + waveRemaining[2]) == 0;
}

bool PhoneTowerDefence::noActiveEnemies() const {
	for(uint8_t i = 0; i < MaxEnemies; ++i) {
		if(enemies[i].active) return false;
	}
	return true;
}

// ===========================================================================
// helpers
// ===========================================================================

int16_t PhoneTowerDefence::slotCenterX(uint8_t slot) const {
	// Slots are centred: 8 px margin + 12 px to slot centre + 24 px each.
	// Yields { 20, 44, 68, 92, 116, 140 } for slot 0..5.
	return SlotX0 + 12 + static_cast<int16_t>(slot) * SlotW;
}

int16_t PhoneTowerDefence::towerRange(uint8_t level) const {
	// Range grows with level so an upgrade also widens coverage. L1
	// covers ~1.8 slots either side; L3 covers ~2.5.
	if(level == 1) return 22;
	if(level == 2) return 26;
	return 30;
}

int16_t PhoneTowerDefence::towerCooldownTicks(uint8_t level) const {
	// Faster fire at higher levels; tuned so an L3 tower can solo a
	// lane of basics but two L1s are still enough for an early wave.
	if(level == 1) return 24;   // ~800 ms
	if(level == 2) return 18;   // ~600 ms
	return 12;                  // ~400 ms
}

int16_t PhoneTowerDefence::enemySpeedQ8(uint8_t type) const {
	// Tuned so a basic crosses the lane in ~10s, a fast in ~6.5s, a
	// tank in ~16s. Numbers are Q8 px/tick; tick is 33ms (~30 Hz).
	if(type == 1) return 200;   // fast
	if(type == 2) return 80;    // tank
	return 128;                 // basic
}

int16_t PhoneTowerDefence::enemyHp(uint8_t type) const {
	if(type == 1) return 1;     // fast: glass cannon
	if(type == 2) return 5;     // tank: needs sustained fire
	return 2;                   // basic
}

int16_t PhoneTowerDefence::enemyReward(uint8_t type) const {
	if(type == 1) return 10;
	if(type == 2) return 18;
	return 8;
}

int16_t PhoneTowerDefence::upgradeCost(uint8_t currentLevel) const {
	if(currentLevel == 0) return BuildCost;
	if(currentLevel == 1) return UpgL2Cost;
	if(currentLevel == 2) return UpgL3Cost;
	return 9999;     // already maxed -- caller checks level >= 3 first
}

lv_color_t PhoneTowerDefence::enemyColor(uint8_t type) const {
	if(type == 1) return MP_ACCENT;     // fast = orange
	if(type == 2) return MP_LABEL_DIM;  // tank = pale purple
	return MP_HIGHLIGHT;                // basic = cyan
}

lv_color_t PhoneTowerDefence::towerBgColor(uint8_t level) const {
	if(level == 1) return MP_DIM;
	if(level == 2) return MP_ACCENT;
	if(level == 3) return MP_HIGHLIGHT;
	return MP_BG_DARK;
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneTowerDefence::refreshHud() {
	if(hudWaveLabel) {
		// Show the wave the user is engaging with: in Building/Idle,
		// that's the upcoming one (waveNum + 1 capped at WaveCount);
		// in Playing, it's the wave currently in flight (waveNum).
		uint8_t shown = waveNum;
		if(state == GameState::Idle) shown = 1;
		else if(state == GameState::Building) shown = static_cast<uint8_t>(waveNum + 1);
		if(shown < 1) shown = 1;
		if(shown > WaveCount) shown = WaveCount;
		char buf[16];
		snprintf(buf, sizeof(buf), "W%u/%u",
		         static_cast<unsigned>(shown),
		         static_cast<unsigned>(WaveCount));
		lv_label_set_text(hudWaveLabel, buf);
	}
	if(hudCashLabel) {
		char buf[16];
		snprintf(buf, sizeof(buf), "$%d", static_cast<int>(cash));
		lv_label_set_text(hudCashLabel, buf);
	}
	if(hudLivesLabel) {
		char buf[16];
		snprintf(buf, sizeof(buf), "L:%d", static_cast<int>(lives));
		lv_label_set_text(hudLivesLabel, buf);
	}
}

void PhoneTowerDefence::refreshTower(uint8_t i) {
	if(i >= SlotCount) return;
	Tower& t = towers[i];
	if(t.node == nullptr) return;

	const bool selected =
		(state == GameState::Building || state == GameState::Playing) &&
		(cursor == i);

	if(t.level == 0) {
		// Empty slot -- thin dim outline, transparent fill so the
		// synthwave wallpaper bleeds through and the slot reads as
		// "available" rather than "broken".
		lv_obj_set_style_bg_color(t.node, MP_BG_DARK, 0);
		lv_obj_set_style_bg_opa(t.node, LV_OPA_30, 0);
		lv_obj_set_style_border_color(t.node,
		                              selected ? MP_ACCENT : MP_DIM, 0);
		lv_obj_set_style_border_width(t.node, selected ? 2 : 1, 0);
	} else {
		// Built tower -- solid fill in the level colour, cream border
		// (sunset orange while selected, cream when firing, otherwise
		// just a thin cream rim so it pops on the dim purple lane).
		lv_color_t bg = towerBgColor(t.level);
		lv_color_t border = selected ? MP_ACCENT : MP_TEXT;
		if(t.flashTicks > 0) {
			// Brief muzzle-flash: bg flips to cream, border stays.
			bg = MP_TEXT;
		}
		lv_obj_set_style_bg_color(t.node, bg, 0);
		lv_obj_set_style_bg_opa(t.node, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(t.node, border, 0);
		lv_obj_set_style_border_width(t.node, selected ? 2 : 1, 0);
	}
}

void PhoneTowerDefence::refreshAllTowers() {
	for(uint8_t i = 0; i < SlotCount; ++i) refreshTower(i);
}

void PhoneTowerDefence::refreshEnemy(uint8_t i) {
	if(i >= MaxEnemies) return;
	Enemy& e = enemies[i];
	if(e.node == nullptr) return;
	if(!e.active) {
		lv_obj_add_flag(e.node, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_color_t col = (e.flashTicks > 0) ? MP_TEXT : enemyColor(e.type);
	lv_obj_set_style_bg_color(e.node, col, 0);
	lv_obj_set_style_bg_opa(e.node, LV_OPA_COVER, 0);
	lv_obj_clear_flag(e.node, LV_OBJ_FLAG_HIDDEN);
	const int16_t ex = static_cast<int16_t>(e.xQ8 >> 8);
	lv_obj_set_pos(e.node, ex - EnemySize / 2, EnemyY);
}

void PhoneTowerDefence::refreshSoftKeys() {
	if(!softKeys) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("PLAY");
			softKeys->setRight("BACK");
			break;
		case GameState::Building:
			softKeys->setLeft("BUILD");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			softKeys->setLeft("BUILD");
			softKeys->setRight("BACK");
			break;
		case GameState::Won:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
		case GameState::Lost:
			softKeys->setLeft("RETRY");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneTowerDefence::refreshHint() {
	if(!hintLabel) return;
	char buf[28];
	switch(state) {
		case GameState::Idle:
			lv_obj_set_style_text_color(hintLabel, MP_TEXT, 0);
			lv_label_set_text(hintLabel, "PRESS PLAY TO START");
			break;
		case GameState::Building: {
			lv_obj_set_style_text_color(hintLabel, MP_HIGHLIGHT, 0);
			const unsigned next = static_cast<unsigned>(waveNum + 1);
			snprintf(buf, sizeof(buf), "PRESS 2 - START W%u", next);
			lv_label_set_text(hintLabel, buf);
			break;
		}
		case GameState::Playing: {
			lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
			snprintf(buf, sizeof(buf), "WAVE %u INCOMING",
			         static_cast<unsigned>(waveNum));
			lv_label_set_text(hintLabel, buf);
			break;
		}
		case GameState::Won:
			lv_obj_set_style_text_color(hintLabel, MP_HIGHLIGHT, 0);
			lv_label_set_text(hintLabel, "VICTORY!");
			break;
		case GameState::Lost:
			lv_obj_set_style_text_color(hintLabel, MP_ACCENT, 0);
			lv_label_set_text(hintLabel, "BASE LOST");
			break;
	}
}

void PhoneTowerDefence::refreshCostHint() {
	if(!costHintLabel) return;
	if(state != GameState::Building && state != GameState::Playing) {
		lv_label_set_text(costHintLabel, "");
		return;
	}
	const Tower& t = towers[cursor];
	char buf[24];
	if(t.level >= 3) {
		snprintf(buf, sizeof(buf), "L3 MAX");
	} else {
		const int16_t c = upgradeCost(t.level);
		const char* op = (t.level == 0) ? "BUILD" : "UPGR";
		snprintf(buf, sizeof(buf), "%s $%d", op, static_cast<int>(c));
	}
	// Shade the hint cyan when the player can afford the action, dim
	// purple otherwise -- a subtle affordance cue without a separate
	// "can build" indicator.
	const bool canAfford = (t.level < 3 && cash >= upgradeCost(t.level));
	lv_obj_set_style_text_color(costHintLabel,
	                            canAfford ? MP_HIGHLIGHT : MP_LABEL_DIM, 0);
	lv_label_set_text(costHintLabel, buf);
}

// ===========================================================================
// timers
// ===========================================================================

void PhoneTowerDefence::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneTowerDefence::onTickTimerStatic,
	                            TickMs, this);
}

void PhoneTowerDefence::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneTowerDefence::onTickTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTowerDefence*>(timer->user_data);
	if(self == nullptr) return;
	self->physicsStep();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneTowerDefence::buttonPressed(uint i) {
	switch(state) {
		case GameState::Idle:
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				enterBuilding();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Won:
		case GameState::Lost:
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				enterIdle();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Building:
		case GameState::Playing:
			break;   // fall through to the gameplay key handler
	}

	switch(i) {
		case BTN_LEFT:
		case BTN_4: {
			if(cursor > 0) {
				const uint8_t prev = cursor;
				cursor--;
				refreshTower(prev);
				refreshTower(cursor);
				refreshCostHint();
			}
			break;
		}
		case BTN_RIGHT:
		case BTN_6: {
			if(cursor + 1 < SlotCount) {
				const uint8_t prev = cursor;
				cursor++;
				refreshTower(prev);
				refreshTower(cursor);
				refreshCostHint();
			}
			break;
		}
		case BTN_ENTER:
		case BTN_5:
			tryBuildOrUpgrade();
			break;
		case BTN_2:
			// Start the next wave from the Building menu. Ignored
			// during Playing so the player can't accidentally double-
			// trigger a wave by mashing the dialer.
			if(state == GameState::Building) {
				if(softKeys) softKeys->flashLeft();
				startNextWave();
			}
			break;
		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			pop();
			break;
		default:
			break;
	}
}
