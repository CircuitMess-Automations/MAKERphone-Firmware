#include "PhoneLockHint.h"
#include "../Fonts/font.h"
#include "../MakerphoneTheme.h"
#include <Loop/LoopManager.h>

// MAKERphone retro palette - kept identical across the Phone* family.
//
// S102 — these now resolve through MakerphoneTheme so the lock-screen
// hint picks up the active theme. The caption colour is the live one
// (MP_TEXT for the cream Synthwave caption / dark olive ink under the
// Nokia 3310 panel) and the chevron sweep uses the theme's highlight
// colour, so the slide cue reads correctly against either wallpaper.
// Boost (chord-armed) state still uses the accent colour, which under
// Nokia is the deep frame olive — readable on the pale-olive panel.
#define MP_TEXT       (MakerphoneTheme::text())
#define MP_HIGHLIGHT  (MakerphoneTheme::highlight())
#define MP_ACCENT     (MakerphoneTheme::accent())
#define MP_DIM        (MakerphoneTheme::dim())

PhoneLockHint::PhoneLockHint(lv_obj_t* parent) : LVObject(parent){
	caption = nullptr;
	for(uint8_t i = 0; i < ChevronN; i++){
		chevrons[i] = nullptr;
	}

	// Anchor independently of any flex/column layout the host screen uses.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, HintWidth, HintHeight);

	// Transparent slab - the synthwave wallpaper / chat list shows through.
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);

	buildLabels();
	redrawChevrons();
	redrawCaption();

	LoopManager::addListener(this);
}

PhoneLockHint::~PhoneLockHint(){
	LoopManager::removeListener(this);
}

void PhoneLockHint::buildLabels(){
	caption = lv_label_create(obj);
	lv_label_set_text(caption, "SLIDE TO UNLOCK");
	lv_obj_set_style_text_font(caption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(caption, MP_TEXT, 0);
	lv_obj_set_align(caption, LV_ALIGN_LEFT_MID);
	// 6 px gutter from the screen edge - matches PhoneSoftKeyBar's padding.
	lv_obj_set_x(caption, 6);

	// Three chevrons stacked rightward. Drawn right-aligned with negative
	// x offsets so the rightmost chevron sits flush at -6 px from the edge
	// and the others step inward by 6 px each.
	for(uint8_t i = 0; i < ChevronN; i++){
		chevrons[i] = lv_label_create(obj);
		lv_label_set_text(chevrons[i], ">");
		lv_obj_set_style_text_font(chevrons[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(chevrons[i], MP_HIGHLIGHT, 0);
		lv_obj_set_align(chevrons[i], LV_ALIGN_RIGHT_MID);
		// Index 0 is leftmost chevron, ChevronN-1 is rightmost.
		const int16_t step = 6;
		lv_obj_set_x(chevrons[i], -(int16_t) (6 + (ChevronN - 1 - i) * step));
	}
}

void PhoneLockHint::redrawChevrons(){
	for(uint8_t i = 0; i < ChevronN; i++){
		if(!chevrons[i]) continue;

		if(boost){
			// Chord-armed: every chevron is bright cyan; the active one
			// strobes at full opacity, the others stay at a high baseline
			// so the row reads as a solid pulsing cue rather than a sweep.
			const bool isActive = (i == activeIdx);
			lv_obj_set_style_text_color(chevrons[i], MP_HIGHLIGHT, 0);
			lv_obj_set_style_text_opa(chevrons[i], isActive ? LV_OPA_COVER : LV_OPA_70, 0);
		}else{
			const bool isActive = (i == activeIdx);
			lv_obj_set_style_text_color(chevrons[i], isActive ? MP_HIGHLIGHT : MP_DIM, 0);
			lv_obj_set_style_text_opa(chevrons[i], isActive ? LV_OPA_COVER : LV_OPA_60, 0);
		}
	}
}

void PhoneLockHint::redrawCaption(){
	if(!caption) return;

	// 4-phase opacity wave - the "hint shimmer" of S48. Idle pulses
	// gently between 80% and 100% so the caption breathes without
	// distracting; boost mode strobes harder and recolors orange so
	// the user knows the next-step prompt is live.
	static const lv_opa_t idleWave[4]  = { LV_OPA_COVER, LV_OPA_90, LV_OPA_80, LV_OPA_90 };
	static const lv_opa_t boostWave[4] = { LV_OPA_COVER, LV_OPA_70, LV_OPA_50, LV_OPA_70 };

	const lv_opa_t op = (boost ? boostWave : idleWave)[shimmerPhase % 4];

	lv_obj_set_style_text_color(caption, boost ? MP_ACCENT : MP_TEXT, 0);
	lv_obj_set_style_text_opa(caption, op, 0);
}

void PhoneLockHint::setActive(bool active){
	running = active;
	if(!running){
		// Park all chevrons dim so the hint is visually quiet while paused.
		activeIdx = 0xFF;
		redrawChevrons();
		// Caption stays at idle look while paused.
		boost = false;
		redrawCaption();
	}else{
		activeIdx = 0;
		shimmerPhase = 0;
		lastStepMs = millis();
		lastShimmerMs = lastStepMs;
		redrawChevrons();
		redrawCaption();
	}
}

void PhoneLockHint::setBoost(bool b){
	if(boost == b) return;
	boost = b;

	if(!caption) return;

	if(boost){
		lv_label_set_text(caption, "PRESS  >  TO UNLOCK");
	}else{
		lv_label_set_text(caption, "SLIDE TO UNLOCK");
	}

	// Reset the sweep so the new mode starts on a clean phase.
	activeIdx = 0;
	shimmerPhase = 0;
	lastStepMs = millis();
	lastShimmerMs = lastStepMs;
	redrawChevrons();
	redrawCaption();
}

void PhoneLockHint::loop(uint micros){
	if(!running) return;

	const uint32_t now = millis();

	// Caption shimmer wave - independent cadence from the chevron sweep
	// so the two animations interleave rather than locking into a beat.
	if(now - lastShimmerMs >= ShimmerStepMs){
		lastShimmerMs = now;
		shimmerPhase = (shimmerPhase + 1) & 0x03;
		redrawCaption();
	}

	const uint32_t step = boost ? BoostStepMs : StepMs;
	if(now - lastStepMs < step) return;
	lastStepMs = now;

	activeIdx = (activeIdx + 1) % ChevronN;
	redrawChevrons();
}
