#include "PhoneWhackAMole.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside PhoneTetris (S71/72), PhoneTicTacToe (S81),
// PhoneMemoryMatch (S82), PhoneReversi (S89) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneWhackAMole::PhoneWhackAMole()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < HoleCount; ++i) {
		holePanels[i] = nullptr;
		holeLabels[i] = nullptr;
		holeMoles[i]  = nullptr;
		holeVis[i]    = HoleVis::Empty;
		holeLifeMs[i] = 0;
		holeFlashTicks[i] = 0;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildHoles();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("BACK");

	// Start in Idle: rules overlay visible, no ticking yet.
	enterIdle();
}

PhoneWhackAMole::~PhoneWhackAMole() {
	stopTickTimer();
	// All children are parented to obj; LVScreen frees them recursively.
}

void PhoneWhackAMole::onStart() {
	Input::getInstance()->addListener(this);
	// We start the tick timer only when the round actually begins;
	// idle state doesn't need to spend cycles. Anything in flight from
	// a prior life of the screen has already been torn down by onStop.
}

void PhoneWhackAMole::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneWhackAMole::buildHud() {
	// Score badge (left): cyan SCORE ## counter.
	hudScoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudScoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudScoreLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudScoreLabel, "SCORE 00");
	lv_obj_set_pos(hudScoreLabel, 4, HudY + 2);

	// High-score badge (centre): warm cream HI ## tally that survives
	// "play again" but resets when the screen is popped.
	hudHiLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudHiLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudHiLabel, MP_TEXT, 0);
	lv_label_set_text(hudHiLabel, "HI 00");
	lv_obj_set_align(hudHiLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudHiLabel, HudY + 2);

	// Lives row (right): a "LIVES" caption followed by three small dots.
	// The dots are positioned individually so we can re-tint them in
	// place without churning a label string on every miss.
	hudLivesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudLivesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudLivesLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudLivesLabel, "LIVES");
	lv_obj_set_pos(hudLivesLabel, 117, HudY + 2);

	for(uint8_t i = 0; i < kStartLives; ++i) {
		auto* dot = lv_obj_create(obj);
		lv_obj_remove_style_all(dot);
		lv_obj_set_size(dot, 3, 3);
		lv_obj_set_pos(dot, 142 + i * 5, HudY + 4);
		lv_obj_set_style_bg_color(dot, MP_HIGHLIGHT, 0);
		lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(dot, 0, 0);
		lv_obj_set_style_radius(dot, 2, 0);
		lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
		hudLivesDot[i] = dot;
	}
}

void PhoneWhackAMole::buildOverlay() {
	// Centred panel that doubles as the Idle "rules" card and the
	// GameOver "score" card. We rebuild text in refreshOverlay() rather
	// than tearing the panel down between states so LVGL only has to
	// re-layout once per round.
	overlayPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(overlayPanel);
	lv_obj_set_size(overlayPanel, 110, 60);
	lv_obj_set_align(overlayPanel, LV_ALIGN_CENTER);
	lv_obj_set_style_bg_color(overlayPanel, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(overlayPanel, LV_OPA_90, 0);
	lv_obj_set_style_border_color(overlayPanel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(overlayPanel, 1, 0);
	lv_obj_set_style_radius(overlayPanel, 3, 0);
	lv_obj_set_style_pad_all(overlayPanel, 4, 0);
	lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_CLICKABLE);

	overlayTitle = lv_label_create(overlayPanel);
	lv_obj_set_style_text_font(overlayTitle, &pixelbasic16, 0);
	lv_obj_set_style_text_color(overlayTitle, MP_ACCENT, 0);
	lv_obj_set_style_text_align(overlayTitle, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayTitle, "WHACK!");
	lv_obj_set_align(overlayTitle, LV_ALIGN_TOP_MID);
	lv_obj_set_y(overlayTitle, 0);

	overlayLines = lv_label_create(overlayPanel);
	lv_obj_set_style_text_font(overlayLines, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLines, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLines,
	                  "TAP 1-9 TO HIT MOLES\nA TO START");
	lv_obj_set_align(overlayLines, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(overlayLines, -1);
}

void PhoneWhackAMole::buildHoles() {
	// One cell per hole: a rounded panel for the "ground" + a digit
	// caption in dim purple + a hidden cyan mole rectangle that we
	// reveal when a mole spawns there. The hole index matches the
	// dialer position (top-left = 1, mid-mid = 5, bot-right = 9).
	for(uint8_t row = 0; row < Rows; ++row) {
		for(uint8_t col = 0; col < Cols; ++col) {
			const uint8_t idx = static_cast<uint8_t>(row * Cols + col);
			const lv_coord_t x = GridX + col * CellW;
			const lv_coord_t y = GridY + row * CellH;

			// Hole panel: a rounded rectangle with a 1 px purple frame.
			auto* p = lv_obj_create(obj);
			lv_obj_remove_style_all(p);
			lv_obj_set_size(p, CellW - 2, CellH - 2);
			lv_obj_set_pos(p, x + 1, y + 1);
			lv_obj_set_style_bg_color(p, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(p, LV_OPA_70, 0);
			lv_obj_set_style_border_color(p, MP_DIM, 0);
			lv_obj_set_style_border_width(p, 1, 0);
			lv_obj_set_style_radius(p, 3, 0);
			lv_obj_set_style_pad_all(p, 0, 0);
			lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(p, LV_OBJ_FLAG_IGNORE_LAYOUT);
			holePanels[idx] = p;

			// Digit caption: pixelbasic16 of the dialer key. Sits in
			// the upper portion of the cell so the mole sprite (drawn
			// lower) doesn't fight it visually.
			auto* lbl = lv_label_create(p);
			lv_obj_set_style_text_font(lbl, &pixelbasic16, 0);
			lv_obj_set_style_text_color(lbl, MP_LABEL_DIM, 0);
			lv_obj_set_style_pad_all(lbl, 0, 0);
			char digit[2] = { static_cast<char>('1' + idx), '\0' };
			lv_label_set_text(lbl, digit);
			lv_obj_set_align(lbl, LV_ALIGN_TOP_MID);
			lv_obj_set_y(lbl, 1);
			holeLabels[idx] = lbl;

			// Mole sprite: a small cyan rounded rectangle anchored to
			// the bottom of the cell. Hidden by default; shown when a
			// mole pops up here. We don't draw a face -- at 16x10 the
			// silhouette is enough to read as "thing to whack".
			auto* m = lv_obj_create(p);
			lv_obj_remove_style_all(m);
			lv_obj_set_size(m, 16, 10);
			lv_obj_set_align(m, LV_ALIGN_BOTTOM_MID);
			lv_obj_set_y(m, -3);
			lv_obj_set_style_bg_color(m, MP_HIGHLIGHT, 0);
			lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(m, MP_BG_DARK, 0);
			lv_obj_set_style_border_width(m, 1, 0);
			lv_obj_set_style_radius(m, 4, 0);
			lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
			holeMoles[idx] = m;
		}
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneWhackAMole::enterIdle() {
	stopTickTimer();
	state = GameState::Idle;

	for(uint8_t i = 0; i < HoleCount; ++i) {
		holeVis[i] = HoleVis::Empty;
		holeLifeMs[i] = 0;
		holeFlashTicks[i] = 0;
	}
	score = 0;
	lives = kStartLives;
	spawnTimerMs = 0;

	renderAllHoles();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneWhackAMole::startRound() {
	state = GameState::Playing;

	for(uint8_t i = 0; i < HoleCount; ++i) {
		holeVis[i] = HoleVis::Empty;
		holeLifeMs[i] = 0;
		holeFlashTicks[i] = 0;
	}
	score = 0;
	lives = kStartLives;
	// Fire the first mole quickly so the player feels the round
	// start; subsequent spawns settle into the standard interval.
	spawnTimerMs = 200;

	renderAllHoles();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	startTickTimer();
}

void PhoneWhackAMole::endRound() {
	stopTickTimer();
	state = GameState::GameOver;

	if(score > highScore) highScore = score;

	// Clear any in-flight moles + flashes so the GameOver overlay sits
	// over a quiet board.
	for(uint8_t i = 0; i < HoleCount; ++i) {
		holeVis[i] = HoleVis::Empty;
		holeLifeMs[i] = 0;
		holeFlashTicks[i] = 0;
	}

	renderAllHoles();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

// ===========================================================================
// game loop
// ===========================================================================

void PhoneWhackAMole::tick() {
	if(state != GameState::Playing) return;

	const int16_t dt = static_cast<int16_t>(kTickMs);

	// 1. Decrement per-hole timers (mole lifetime + flash fx).
	for(uint8_t i = 0; i < HoleCount; ++i) {
		switch(holeVis[i]) {
			case HoleVis::Mole: {
				holeLifeMs[i] -= dt;
				if(holeLifeMs[i] <= 0) {
					// Mole escaped -- lose a life and start the miss flash.
					holeVis[i] = HoleVis::MissFx;
					holeFlashTicks[i] = kMissFlashTicks;
					holeLifeMs[i] = 0;
					if(lives > 0) --lives;
					renderHole(i);
					refreshHud();
				} else {
					// Mole still up; nothing to redraw, the sprite is
					// already visible. Skip the per-tick render to keep
					// the LVGL invalidation set tight.
				}
				break;
			}
			case HoleVis::HitFx:
			case HoleVis::MissFx: {
				if(holeFlashTicks[i] > 0) --holeFlashTicks[i];
				if(holeFlashTicks[i] == 0) {
					holeVis[i] = HoleVis::Empty;
					renderHole(i);
				}
				break;
			}
			case HoleVis::Empty:
			default:
				break;
		}
	}

	// 2. End the round if we're out of lives. We check this AFTER the
	//    per-hole loop so the final miss flash gets a chance to register
	//    on screen for one tick before the overlay covers it.
	if(lives == 0) {
		endRound();
		return;
	}

	// 3. Advance the spawn timer.
	spawnTimerMs -= dt;
	if(spawnTimerMs <= 0) {
		spawnMole();
		spawnTimerMs = static_cast<int16_t>(currentSpawnIntervalMs());
	}
}

void PhoneWhackAMole::spawnMole() {
	// Pick a random empty hole. We tolerate "every hole occupied" as a
	// silent no-op (the next tick will retry once another mole resolves).
	const uint8_t empties = emptyHoleCount();
	if(empties == 0) return;

	uint8_t pick = static_cast<uint8_t>(rand() % empties);
	for(uint8_t i = 0; i < HoleCount; ++i) {
		if(holeVis[i] != HoleVis::Empty) continue;
		if(pick == 0) {
			holeVis[i] = HoleVis::Mole;
			holeLifeMs[i] = static_cast<int16_t>(currentMoleLifeMs());
			holeFlashTicks[i] = 0;
			renderHole(i);
			return;
		}
		--pick;
	}
}

bool PhoneWhackAMole::whackHole(uint8_t holeIdx) {
	if(holeIdx >= HoleCount) return false;
	if(holeVis[holeIdx] != HoleVis::Mole) return false;

	// Hit! Score it and start the orange-flash post-effect.
	holeVis[holeIdx] = HoleVis::HitFx;
	holeFlashTicks[holeIdx] = kHitFlashTicks;
	holeLifeMs[holeIdx] = 0;
	if(score < 0xFFFF) ++score;
	if(score > highScore) highScore = score;
	renderHole(holeIdx);
	refreshHud();
	return true;
}

// ===========================================================================
// helpers
// ===========================================================================

uint8_t PhoneWhackAMole::emptyHoleCount() const {
	uint8_t n = 0;
	for(uint8_t i = 0; i < HoleCount; ++i) {
		if(holeVis[i] == HoleVis::Empty) ++n;
	}
	return n;
}

uint16_t PhoneWhackAMole::currentMoleLifeMs() const {
	// Linear ramp from kMoleStartMs at score 0 down to kMoleFloorMs
	// at score kRampHits, clamped flat thereafter.
	if(kRampHits == 0) return kMoleFloorMs;
	const uint16_t s = (score < kRampHits) ? score : kRampHits;
	const int32_t span = static_cast<int32_t>(kMoleStartMs)
	                   - static_cast<int32_t>(kMoleFloorMs);
	const int32_t drop = (span * static_cast<int32_t>(s))
	                   / static_cast<int32_t>(kRampHits);
	const int32_t life = static_cast<int32_t>(kMoleStartMs) - drop;
	if(life < kMoleFloorMs) return kMoleFloorMs;
	if(life > kMoleStartMs) return kMoleStartMs;
	return static_cast<uint16_t>(life);
}

uint16_t PhoneWhackAMole::currentSpawnIntervalMs() const {
	if(kRampHits == 0) return kSpawnFloorMs;
	const uint16_t s = (score < kRampHits) ? score : kRampHits;
	const int32_t span = static_cast<int32_t>(kSpawnStartMs)
	                   - static_cast<int32_t>(kSpawnFloorMs);
	const int32_t drop = (span * static_cast<int32_t>(s))
	                   / static_cast<int32_t>(kRampHits);
	const int32_t iv = static_cast<int32_t>(kSpawnStartMs) - drop;
	if(iv < kSpawnFloorMs) return kSpawnFloorMs;
	if(iv > kSpawnStartMs) return kSpawnStartMs;
	return static_cast<uint16_t>(iv);
}

uint8_t PhoneWhackAMole::buttonToHoleIdx(uint i) const {
	// Map dialer keys 1..9 to hole indices 0..8. Anything else returns
	// 0xFF to signal "not a whack input".
	switch(i) {
		case BTN_1: return 0;
		case BTN_2: return 1;
		case BTN_3: return 2;
		case BTN_4: return 3;
		case BTN_5: return 4;
		case BTN_6: return 5;
		case BTN_7: return 6;
		case BTN_8: return 7;
		case BTN_9: return 8;
		default:    return 0xFF;
	}
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneWhackAMole::renderAllHoles() {
	for(uint8_t i = 0; i < HoleCount; ++i) {
		renderHole(i);
	}
}

void PhoneWhackAMole::renderHole(uint8_t idx) {
	if(idx >= HoleCount) return;
	auto* p = holePanels[idx];
	auto* l = holeLabels[idx];
	auto* m = holeMoles[idx];
	if(p == nullptr || l == nullptr || m == nullptr) return;

	switch(holeVis[idx]) {
		case HoleVis::Empty:
			// Default look: dim background, dim caption, mole hidden.
			lv_obj_set_style_bg_color(p, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(p, LV_OPA_70, 0);
			lv_obj_set_style_border_color(p, MP_DIM, 0);
			lv_obj_set_style_text_color(l, MP_LABEL_DIM, 0);
			lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
			break;

		case HoleVis::Mole:
			// Active mole: slightly brighter background to draw the eye
			// to the active hole, cyan caption to colour-link with the
			// mole sprite below it, and the mole rect visible.
			lv_obj_set_style_bg_color(p, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
			lv_obj_set_style_border_color(p, MP_HIGHLIGHT, 0);
			lv_obj_set_style_text_color(l, MP_HIGHLIGHT, 0);
			lv_obj_set_style_bg_color(m, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(m, LV_OBJ_FLAG_HIDDEN);
			break;

		case HoleVis::HitFx:
			// Successful whack: orange wash on the cell, mole sprite
			// briefly tinted orange too so the hit reads instantly.
			lv_obj_set_style_bg_color(p, MP_ACCENT, 0);
			lv_obj_set_style_bg_opa(p, LV_OPA_70, 0);
			lv_obj_set_style_border_color(p, MP_ACCENT, 0);
			lv_obj_set_style_text_color(l, MP_TEXT, 0);
			lv_obj_set_style_bg_color(m, MP_ACCENT, 0);
			lv_obj_clear_flag(m, LV_OBJ_FLAG_HIDDEN);
			break;

		case HoleVis::MissFx:
			// Mole escaped: dim purple wash, mole sprite hidden, caption
			// dimmed so the cell reads as "you missed this one".
			lv_obj_set_style_bg_color(p, MP_DIM, 0);
			lv_obj_set_style_bg_opa(p, LV_OPA_70, 0);
			lv_obj_set_style_border_color(p, MP_LABEL_DIM, 0);
			lv_obj_set_style_text_color(l, MP_LABEL_DIM, 0);
			lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
			break;
	}
}

void PhoneWhackAMole::refreshHud() {
	if(hudScoreLabel != nullptr) {
		char buf[16];
		const unsigned s = score > 9999 ? 9999 : score;
		snprintf(buf, sizeof(buf), "SCORE %02u", s);
		lv_label_set_text(hudScoreLabel, buf);
	}
	if(hudHiLabel != nullptr) {
		char buf[16];
		const unsigned h = highScore > 9999 ? 9999 : highScore;
		snprintf(buf, sizeof(buf), "HI %02u", h);
		lv_label_set_text(hudHiLabel, buf);
	}
	for(uint8_t i = 0; i < kStartLives; ++i) {
		if(hudLivesDot[i] == nullptr) continue;
		// Dots remain in place; we only re-tint to "lit" cyan or "spent"
		// dim purple so the row's geometry stays stable on every miss.
		const bool alive = (i < lives);
		lv_obj_set_style_bg_color(hudLivesDot[i],
		                          alive ? MP_HIGHLIGHT : MP_DIM, 0);
		lv_obj_set_style_bg_opa(hudLivesDot[i], LV_OPA_COVER, 0);
	}
}

void PhoneWhackAMole::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("START");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			softKeys->setLeft("WHACK");
			softKeys->setRight("BACK");
			break;
		case GameState::GameOver:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneWhackAMole::refreshOverlay() {
	if(overlayPanel == nullptr) return;
	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayTitle, "WHACK!");
			lv_obj_set_style_text_color(overlayTitle, MP_ACCENT, 0);
			lv_label_set_text(overlayLines,
			                  "TAP 1-9 TO HIT MOLES\nA TO START");
			lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayPanel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayPanel);
			break;

		case GameState::Playing:
			lv_obj_add_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			break;

		case GameState::GameOver: {
			char body[48];
			const unsigned s = score > 9999 ? 9999 : score;
			const unsigned h = highScore > 9999 ? 9999 : highScore;
			snprintf(body, sizeof(body),
			         "SCORE %u\nHI %u\nA TO PLAY AGAIN",
			         s, h);
			lv_label_set_text(overlayTitle, "GAME OVER");
			lv_obj_set_style_text_color(overlayTitle, MP_ACCENT, 0);
			lv_label_set_text(overlayLines, body);
			lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayPanel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayPanel);
			break;
		}
	}
}

// ===========================================================================
// timer helpers
// ===========================================================================

void PhoneWhackAMole::startTickTimer() {
	stopTickTimer();
	tickTimer = lv_timer_create(&PhoneWhackAMole::onTickStatic,
	                            kTickMs, this);
	// Repeating timer -- LVGL leaves repeat_count at -1 (forever) by
	// default, which is exactly what we want here.
}

void PhoneWhackAMole::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneWhackAMole::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneWhackAMole*>(timer->user_data);
	if(self == nullptr) return;
	self->tick();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneWhackAMole::buttonPressed(uint i) {
	// BACK always pops, regardless of state. Hit BACK during a round
	// and you abandon it -- the high score still survives in case the
	// player re-enters the screen later.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// R restarts the round (works in every state). Useful both for
	// "give up mid-game" and "go again after game over" without having
	// to reach for a different key.
	if(i == BTN_R) {
		startRound();
		return;
	}

	switch(state) {
		case GameState::Idle: {
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startRound();
				return;
			}
			return;
		}

		case GameState::Playing: {
			// Map 1..9 to a hole and resolve the whack. BTN_5 is on
			// this list too -- in Idle / GameOver it's "start", but
			// once the round is live the dialer hand position takes
			// precedence so the centre-key whack works literally.
			const uint8_t holeIdx = buttonToHoleIdx(i);
			if(holeIdx != 0xFF) {
				const bool hit = whackHole(holeIdx);
				if(softKeys && hit) softKeys->flashLeft();
			}
			// BTN_ENTER doesn't whack -- it's reserved for "start
			// next round" in the GameOver state. During Playing it's
			// a no-op so an accidental enter-press doesn't mis-fire.
			return;
		}

		case GameState::GameOver: {
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startRound();
				return;
			}
			return;
		}
	}
}
