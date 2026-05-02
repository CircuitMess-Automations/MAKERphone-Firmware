#include "PhoneSimon.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Audio/Piezo.h>
#include <Settings.h>
#include <stdio.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside PhoneTetris (S71/72), PhoneTicTacToe (S81),
// PhoneWhackAMole (S90), PhoneWordle (S96) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// Per-pad palette. The four pads keep two looks each: a dim "off"
// tint that reads as a faint plate against the synthwave wallpaper,
// and a bright "lit" tint that reads as glowing plastic. We keep the
// hue stable between off + lit -- only the brightness changes -- so
// the player can name pads by colour ("the cyan one!") even when
// they're inactive.
namespace {

struct PadColor {
	lv_color_t off;
	lv_color_t lit;
	lv_color_t border;
	const char* name;
};

// Index 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right.
// Cyan / orange / magenta / yellow -- a 4-corner palette every retro
// gamer recognises from the original Simon device, restyled for the
// MAKERphone synthwave wallpaper.
const PadColor PadColors[PhoneSimon::PadCount] = {
	{ // 0 -- cyan
		lv_color_make( 35,  90, 110),
		lv_color_make(122, 232, 255),
		lv_color_make( 70, 200, 230),
		"CYAN"
	},
	{ // 1 -- orange
		lv_color_make(110,  60,  20),
		lv_color_make(255, 160,  50),
		lv_color_make(220, 130,  30),
		"ORANGE"
	},
	{ // 2 -- magenta
		lv_color_make( 80,  30,  90),
		lv_color_make(220, 110, 230),
		lv_color_make(190,  90, 200),
		"MAGENTA"
	},
	{ // 3 -- yellow
		lv_color_make(100,  90,  20),
		lv_color_make(255, 220,  60),
		lv_color_make(220, 190,  40),
		"YELLOW"
	},
};

// Dialer-key digit caption painted in the top-left of each pad, so a
// player using the dialer instantly knows "to play the orange pad,
// press 3". Top row = 1 / 3, bottom row = 7 / 9.
const char* PadDigits[PhoneSimon::PadCount] = { "1", "3", "7", "9" };

} // namespace

// Static pad-pitch table. Defined out-of-line because some Arduino
// builds don't ODR-deduplicate inline-constexpr arrays inside header
// classes cleanly.
const uint16_t PhoneSimon::kPadFreq[PhoneSimon::PadCount] = {
	392,   // ~G4  cyan
	523,   // ~C5  orange
	659,   // ~E5  magenta
	784    // ~G5  yellow
};

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneSimon::PhoneSimon()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < PadCount; ++i) {
		padPanels[i] = nullptr;
		padLabels[i] = nullptr;
		padNames[i]  = nullptr;
		padFlashMs[i] = 0;
		sequence[i % kSeqMax] = 0; // touched -- full reset below
	}
	for(uint8_t i = 0; i < kSeqMax; ++i) {
		sequence[i] = 0;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildPads();
	buildProgress();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("BACK");

	// Seed rand() with a value distinct from every other Phone* game's
	// seed-mode so two screens push in quick succession don't end up
	// playing the same pad sequence by accident.
	srand(static_cast<unsigned>(millis() ^ 0x517A1A));

	enterIdle();
}

PhoneSimon::~PhoneSimon() {
	stopTickTimer();
	silencePiezo();
	// All children are parented to obj; LVScreen frees them recursively.
}

void PhoneSimon::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneSimon::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
	silencePiezo();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneSimon::buildHud() {
	// Round badge (left): cyan ROUND ## counter. We deliberately call
	// it "ROUND" rather than "SCORE" because in Simon the score IS
	// the sequence length you're currently echoing.
	hudRoundLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudRoundLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudRoundLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudRoundLabel, "ROUND 00");
	lv_obj_set_pos(hudRoundLabel, 4, HudY + 2);

	// High-score badge (centre): warm cream HI ## tally that survives
	// "play again" but resets when the screen is popped.
	hudHiLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudHiLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudHiLabel, MP_TEXT, 0);
	lv_label_set_text(hudHiLabel, "HI 00");
	lv_obj_set_align(hudHiLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudHiLabel, HudY + 2);

	// State badge (right): "WATCH" / "ECHO" / "READY" / "OVER" so the
	// player knows whether to look or to tap. Sunset orange so it pops
	// against the cyan / cream HUD pair on the left.
	hudStateLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudStateLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudStateLabel, MP_ACCENT, 0);
	lv_label_set_text(hudStateLabel, "READY");
	lv_obj_set_pos(hudStateLabel, 130, HudY + 2);
}

void PhoneSimon::buildOverlay() {
	// Centred panel that doubles as the Idle "rules" card and the
	// GameOver "score" card. We rebuild text in refreshOverlay() rather
	// than tearing the panel down between states so LVGL only has to
	// re-layout once per round.
	overlayPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(overlayPanel);
	lv_obj_set_size(overlayPanel, 122, 70);
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
	lv_label_set_text(overlayTitle, "SIMON");
	lv_obj_set_align(overlayTitle, LV_ALIGN_TOP_MID);
	lv_obj_set_y(overlayTitle, 0);

	overlayLines = lv_label_create(overlayPanel);
	lv_obj_set_style_text_font(overlayLines, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLines, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLines,
	                  "WATCH THE PADS\nMIRROR WITH 1/3/7/9\nA TO START");
	lv_obj_set_align(overlayLines, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(overlayLines, -1);
}

void PhoneSimon::buildProgress() {
	// Thin pixelbasic7 strip just above the soft-key bar. Reads
	// "SEQ 4   ECHO 0/4" during Echo so the player knows how far they
	// are through the current mirror. Hidden during Idle/GameOver
	// (the overlay covers it anyway).
	progressLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(progressLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(progressLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(progressLabel, "");
	lv_obj_set_align(progressLabel, LV_ALIGN_BOTTOM_MID);
	// Sit just above the soft-key bar (10 px tall + 1 px breathing room).
	lv_obj_set_y(progressLabel, -(SoftKeyH + 1));
}

void PhoneSimon::buildPads() {
	// 2x2 cluster of rounded pads. Each pad is a coloured panel with
	// a dialer-digit caption in the top-left + a colour-name caption
	// pinned to the bottom-centre. We render the colour states later
	// in renderPad() so initial styling doesn't fight the watch-state
	// flash logic.
	for(uint8_t row = 0; row < 2; ++row) {
		for(uint8_t col = 0; col < 2; ++col) {
			const uint8_t idx = static_cast<uint8_t>(row * 2 + col);
			const lv_coord_t x = ClusterX + col * (PadW + PadGapX);
			const lv_coord_t y = ClusterY + row * (PadH + PadGapY);

			auto* p = lv_obj_create(obj);
			lv_obj_remove_style_all(p);
			lv_obj_set_size(p, PadW, PadH);
			lv_obj_set_pos(p, x, y);
			lv_obj_set_style_bg_color(p, PadColors[idx].off, 0);
			lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(p, PadColors[idx].border, 0);
			lv_obj_set_style_border_width(p, 1, 0);
			lv_obj_set_style_radius(p, 5, 0);
			lv_obj_set_style_pad_all(p, 0, 0);
			lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(p, LV_OBJ_FLAG_IGNORE_LAYOUT);
			padPanels[idx] = p;

			// Dialer-digit caption: pixelbasic16 in the pad's top-left
			// corner. We pin it manually so the caption stays aligned
			// across the four pads regardless of pad-children layout.
			auto* lbl = lv_label_create(p);
			lv_obj_set_style_text_font(lbl, &pixelbasic16, 0);
			lv_obj_set_style_text_color(lbl, MP_BG_DARK, 0);
			lv_obj_set_style_pad_all(lbl, 0, 0);
			lv_label_set_text(lbl, PadDigits[idx]);
			lv_obj_set_pos(lbl, 3, 1);
			padLabels[idx] = lbl;

			// Colour-name caption pinned to the bottom-centre of the
			// pad so a first-time player can read "CYAN" / "ORANGE" /
			// "MAGENTA" / "YELLOW" off the pads while learning. We
			// dim it once a round is in progress so the colour itself
			// carries the message.
			auto* nm = lv_label_create(p);
			lv_obj_set_style_text_font(nm, &pixelbasic7, 0);
			lv_obj_set_style_text_color(nm, MP_BG_DARK, 0);
			lv_label_set_text(nm, PadColors[idx].name);
			lv_obj_set_align(nm, LV_ALIGN_BOTTOM_MID);
			lv_obj_set_y(nm, -2);
			padNames[idx] = nm;
		}
	}
	renderAllPads();
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneSimon::enterIdle() {
	stopTickTimer();
	silencePiezo();
	state = GameState::Idle;

	seqLen = 0;
	echoIdx = 0;
	watchIdx = 0;
	watchTimerMs = 0;
	watchInLit = false;
	watchLitPad = 0xFF;
	failToneMs = 0;
	for(uint8_t i = 0; i < PadCount; ++i) padFlashMs[i] = 0;

	renderAllPads();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshProgress();
}

void PhoneSimon::startRound() {
	silencePiezo();

	seqLen = 0;
	echoIdx = 0;
	watchIdx = 0;
	watchTimerMs = 0;
	watchInLit = false;
	watchLitPad = 0xFF;
	failToneMs = 0;
	for(uint8_t i = 0; i < PadCount; ++i) padFlashMs[i] = 0;

	// Push the first sequence step + dive straight into Watch.
	if(seqLen < kSeqMax) {
		sequence[seqLen++] = static_cast<uint8_t>(rand() % PadCount);
	}

	beginWatch();
}

void PhoneSimon::beginWatch() {
	state = GameState::Watch;
	watchIdx = 0;
	watchInLit = false;
	watchLitPad = 0xFF;
	// Brief lead-in pause before the first lit step: gives the player
	// a beat to focus on the pads after the overlay hides.
	watchTimerMs = static_cast<int16_t>(currentGapMs());

	renderAllPads();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshProgress();
	startTickTimer();
}

void PhoneSimon::beginEcho() {
	state = GameState::Echo;
	echoIdx = 0;
	watchLitPad = 0xFF;
	for(uint8_t i = 0; i < PadCount; ++i) padFlashMs[i] = 0;
	silencePiezo();

	renderAllPads();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshProgress();
	// Tick timer keeps running so the per-pad flash decays cleanly.
	startTickTimer();
}

void PhoneSimon::endRound(bool failed) {
	state = GameState::GameOver;

	// "Cleared" length is the longest sequence the player echoed
	// successfully. On a failure that's seqLen-1 (because the full
	// new sequence broke this round); on a clean exit (currently
	// unreachable but reserved) it's seqLen.
	const uint8_t cleared = failed ? (seqLen > 0 ? seqLen - 1 : 0) : seqLen;
	if(cleared > highScore) highScore = cleared;

	watchLitPad = 0xFF;
	for(uint8_t i = 0; i < PadCount; ++i) padFlashMs[i] = 0;

	if(failed) {
		playFailTone();
		failToneMs = static_cast<int16_t>(kFailMs);
	} else {
		silencePiezo();
		failToneMs = 0;
	}

	renderAllPads();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshProgress();
	// Keep the tick timer running so the failure tone decays cleanly,
	// then we stop it from inside tick() once failToneMs hits zero.
	startTickTimer();
}

// ===========================================================================
// game loop
// ===========================================================================

void PhoneSimon::tick() {
	const int16_t dt = static_cast<int16_t>(kTickMs);

	// Decay any per-pad echo flashes regardless of state.
	for(uint8_t i = 0; i < PadCount; ++i) {
		if(padFlashMs[i] > 0) {
			padFlashMs[i] -= dt;
			if(padFlashMs[i] <= 0) {
				padFlashMs[i] = 0;
				renderPad(i);
			}
		}
	}

	switch(state) {
		case GameState::Idle: {
			// Nothing to do; the timer being live is harmless. Stop
			// it so we don't burn cycles waiting on input.
			stopTickTimer();
			return;
		}

		case GameState::Watch: {
			if(watchTimerMs > 0) {
				watchTimerMs -= dt;
				if(watchTimerMs > 0) return;
			}

			if(watchInLit) {
				// Lit phase ended -- silence the piezo, dim the pad,
				// and enter the inter-step gap.
				unlightPadWatch();
				watchInLit = false;
				watchTimerMs = static_cast<int16_t>(currentGapMs());
				return;
			}

			// Gap phase ended -- light the next pad in the sequence,
			// or transition to Echo if we've covered every step.
			if(watchIdx >= seqLen) {
				beginEcho();
				return;
			}
			const uint8_t pad = sequence[watchIdx];
			lightPadWatch(pad);
			watchIdx++;
			watchInLit = true;
			watchTimerMs = static_cast<int16_t>(currentLitMs());
			return;
		}

		case GameState::Echo: {
			// Pure flash decay (handled at the top); nothing else to
			// do here. The timer can continue to run cheaply because
			// the per-pad decay loop is tiny.
			return;
		}

		case GameState::GameOver: {
			if(failToneMs > 0) {
				failToneMs -= dt;
				if(failToneMs <= 0) {
					failToneMs = 0;
					silencePiezo();
				}
			}
			if(failToneMs <= 0) {
				stopTickTimer();
			}
			return;
		}
	}
}

void PhoneSimon::lightPadWatch(uint8_t padIdx) {
	if(padIdx >= PadCount) return;
	watchLitPad = padIdx;
	playPadTone(padIdx);
	renderPad(padIdx);
}

void PhoneSimon::unlightPadWatch() {
	const uint8_t prev = watchLitPad;
	watchLitPad = 0xFF;
	silencePiezo();
	if(prev < PadCount) renderPad(prev);
}

bool PhoneSimon::echoTap(uint8_t padIdx) {
	if(state != GameState::Echo) return false;
	if(padIdx >= PadCount) return false;

	// Always flash + tone the pad the player tapped, even on a wrong
	// answer, so the audio + visual feedback feels honest.
	padFlashMs[padIdx] = static_cast<int16_t>(kEchoFlashMs);
	playPadTone(padIdx);
	renderPad(padIdx);

	if(echoIdx >= seqLen) {
		// Defensive: shouldn't happen because we transition out of
		// Echo as soon as echoIdx == seqLen, but treat as a miss.
		endRound(true);
		return false;
	}

	const uint8_t expected = sequence[echoIdx];
	if(padIdx != expected) {
		endRound(true);
		return false;
	}

	echoIdx++;
	refreshProgress();

	if(echoIdx >= seqLen) {
		// Cleared this round! Bump high-score, append a new step,
		// dive back into Watch for the longer sequence.
		if(seqLen > highScore) highScore = seqLen;
		if(seqLen < kSeqMax) {
			sequence[seqLen++] = static_cast<uint8_t>(rand() % PadCount);
		}
		// Brief lead-in handled by beginWatch's initial gap timer.
		beginWatch();
	}
	return true;
}

// ===========================================================================
// helpers
// ===========================================================================

uint16_t PhoneSimon::currentLitMs() const {
	if(kRampLen == 0) return kLitFloorMs;
	const uint16_t s = (seqLen < kRampLen) ? seqLen : kRampLen;
	const int32_t span = static_cast<int32_t>(kLitStartMs)
	                   - static_cast<int32_t>(kLitFloorMs);
	const int32_t drop = (span * static_cast<int32_t>(s))
	                   / static_cast<int32_t>(kRampLen);
	const int32_t lit = static_cast<int32_t>(kLitStartMs) - drop;
	if(lit < kLitFloorMs) return kLitFloorMs;
	if(lit > kLitStartMs) return kLitStartMs;
	return static_cast<uint16_t>(lit);
}

uint16_t PhoneSimon::currentGapMs() const {
	const uint16_t lit = currentLitMs();
	uint16_t gap = static_cast<uint16_t>(lit / 2);
	if(gap < kGapFloorMs) gap = kGapFloorMs;
	return gap;
}

uint8_t PhoneSimon::buttonToPadIdx(uint i) const {
	// Map dialer keys + d-pad aliases to pad indices 0..3.
	// Top row (1 / 3): cyan + orange. Bottom row (7 / 9): magenta + yellow.
	// 4 / 2 / 6 / 8 alias to 1 / 3 / 9 / 7 so a thumb player can use
	// the d-pad numerics; LEFT / RIGHT alias to top-row pads as well
	// because there's no natural "row" affordance for them.
	switch(i) {
		case BTN_1:    return 0;
		case BTN_3:    return 1;
		case BTN_7:    return 2;
		case BTN_9:    return 3;
		case BTN_4:    return 0;
		case BTN_2:    return 1;
		case BTN_8:    return 2;
		case BTN_6:    return 3;
		case BTN_LEFT:  return 0;
		case BTN_RIGHT: return 1;
		default:       return 0xFF;
	}
}

// ===========================================================================
// audio
// ===========================================================================

void PhoneSimon::playPadTone(uint8_t padIdx) {
	if(padIdx >= PadCount) return;
	if(!Settings.get().sound) return;
	Piezo.tone(kPadFreq[padIdx]);
}

void PhoneSimon::playFailTone() {
	if(!Settings.get().sound) return;
	Piezo.tone(kFailFreq);
}

void PhoneSimon::silencePiezo() {
	Piezo.noTone();
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneSimon::renderAllPads() {
	for(uint8_t i = 0; i < PadCount; ++i) {
		renderPad(i);
	}
}

void PhoneSimon::renderPad(uint8_t idx) {
	if(idx >= PadCount) return;
	auto* p = padPanels[idx];
	auto* l = padLabels[idx];
	auto* n = padNames[idx];
	if(p == nullptr || l == nullptr || n == nullptr) return;

	const bool litWatch = (state == GameState::Watch && watchLitPad == idx);
	const bool litEcho  = (padFlashMs[idx] > 0);
	const bool lit      = litWatch || litEcho;

	const lv_color_t bg = lit ? PadColors[idx].lit : PadColors[idx].off;
	lv_obj_set_style_bg_color(p, bg, 0);
	lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(p, PadColors[idx].border, 0);

	// Captions: keep them legible against both lit + dim. On the lit
	// pad we paint in deep purple (high contrast against the bright
	// fill); on the dim pad we paint in cream so the digit reads
	// without dazzling.
	const lv_color_t labelColor = lit ? MP_BG_DARK : MP_TEXT;
	lv_obj_set_style_text_color(l, labelColor, 0);
	// Hide the colour-name caption once the round starts -- it's a
	// learning aid for first-time players, not a permanent overlay.
	const bool showName = (state == GameState::Idle);
	if(showName) {
		lv_obj_clear_flag(n, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_text_color(n, lit ? MP_BG_DARK : MP_TEXT, 0);
	} else {
		lv_obj_add_flag(n, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneSimon::refreshHud() {
	if(hudRoundLabel != nullptr) {
		char buf[16];
		const unsigned r = seqLen > 99 ? 99 : seqLen;
		snprintf(buf, sizeof(buf), "ROUND %02u", r);
		lv_label_set_text(hudRoundLabel, buf);
	}
	if(hudHiLabel != nullptr) {
		char buf[16];
		const unsigned h = highScore > 99 ? 99 : highScore;
		snprintf(buf, sizeof(buf), "HI %02u", h);
		lv_label_set_text(hudHiLabel, buf);
	}
	if(hudStateLabel != nullptr) {
		const char* word = "READY";
		switch(state) {
			case GameState::Idle:     word = "READY"; break;
			case GameState::Watch:    word = "WATCH"; break;
			case GameState::Echo:     word = "ECHO";  break;
			case GameState::GameOver: word = "OVER";  break;
		}
		lv_label_set_text(hudStateLabel, word);
	}
}

void PhoneSimon::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("START");
			softKeys->setRight("BACK");
			break;
		case GameState::Watch:
			softKeys->setLeft("WATCH");
			softKeys->setRight("BACK");
			break;
		case GameState::Echo:
			softKeys->setLeft("ECHO");
			softKeys->setRight("BACK");
			break;
		case GameState::GameOver:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneSimon::refreshOverlay() {
	if(overlayPanel == nullptr) return;
	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayTitle, "SIMON");
			lv_obj_set_style_text_color(overlayTitle, MP_ACCENT, 0);
			lv_label_set_text(overlayLines,
			                  "WATCH THE PADS\nMIRROR WITH 1/3/7/9\nA TO START");
			lv_obj_set_style_text_color(overlayLines, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayPanel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayPanel);
			break;

		case GameState::Watch:
		case GameState::Echo:
			lv_obj_add_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
			break;

		case GameState::GameOver: {
			char body[64];
			const unsigned cleared =
				(seqLen > 0) ? static_cast<unsigned>(seqLen - 1) : 0u;
			const unsigned h = highScore > 99 ? 99 : highScore;
			snprintf(body, sizeof(body),
			         "YOU CLEARED %u\nHI %u\nA TO PLAY AGAIN",
			         cleared, h);
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

void PhoneSimon::refreshProgress() {
	if(progressLabel == nullptr) return;
	switch(state) {
		case GameState::Idle:
		case GameState::GameOver:
			lv_label_set_text(progressLabel, "");
			break;
		case GameState::Watch: {
			char buf[24];
			const unsigned s = seqLen > 99 ? 99 : seqLen;
			const unsigned w = (watchIdx > seqLen ? seqLen : watchIdx);
			snprintf(buf, sizeof(buf), "SEQ %u   STEP %u/%u",
			         s, w, s);
			lv_label_set_text(progressLabel, buf);
			break;
		}
		case GameState::Echo: {
			char buf[24];
			const unsigned s = seqLen > 99 ? 99 : seqLen;
			const unsigned e = echoIdx > seqLen ? seqLen : echoIdx;
			snprintf(buf, sizeof(buf), "SEQ %u   ECHO %u/%u",
			         s, e, s);
			lv_label_set_text(progressLabel, buf);
			break;
		}
	}
}

// ===========================================================================
// timer helpers
// ===========================================================================

void PhoneSimon::startTickTimer() {
	if(tickTimer != nullptr) return; // already running -- keep it alive
	tickTimer = lv_timer_create(&PhoneSimon::onTickStatic, kTickMs, this);
}

void PhoneSimon::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneSimon::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneSimon*>(timer->user_data);
	if(self == nullptr) return;
	self->tick();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneSimon::buttonPressed(uint i) {
	// BACK always pops, regardless of state. Hit BACK during a round
	// and you abandon it -- the high score still survives in case the
	// player re-enters the screen later.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// R restarts the round (works in every state). Useful both for
	// "give up mid-game" and "go again after game over" without
	// having to reach for a different key.
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

		case GameState::Watch: {
			// Player input is intentionally ignored while the device
			// is showing the sequence. Letting taps register here
			// would otherwise cause the player to feed taps into the
			// next Echo state by accident.
			return;
		}

		case GameState::Echo: {
			const uint8_t padIdx = buttonToPadIdx(i);
			if(padIdx != 0xFF) {
				const bool ok = echoTap(padIdx);
				if(softKeys && ok) softKeys->flashLeft();
			}
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
