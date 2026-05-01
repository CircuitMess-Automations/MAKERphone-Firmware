#include "PhoneLockHint.h"
#include "../Fonts/font.h"
#include <Loop/LoopManager.h>

// MAKERphone retro palette - kept identical across the Phone* family.
#define MP_TEXT       lv_color_make(255, 220, 180)   // warm cream caption
#define MP_HIGHLIGHT  lv_color_make(122, 232, 255)   // cyan chevrons
#define MP_DIM        lv_color_make(70, 56, 100)     // muted purple - inactive chevrons

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
		const bool isActive = (i == activeIdx);
		lv_obj_set_style_text_color(chevrons[i], isActive ? MP_HIGHLIGHT : MP_DIM, 0);
		lv_obj_set_style_text_opa(chevrons[i], isActive ? LV_OPA_COVER : LV_OPA_60, 0);
	}
}

void PhoneLockHint::setActive(bool active){
	running = active;
	if(!running){
		// Park all chevrons dim so the hint is visually quiet while paused.
		activeIdx = 0xFF;
		redrawChevrons();
	}else{
		activeIdx = 0;
		lastStepMs = millis();
		redrawChevrons();
	}
}

void PhoneLockHint::loop(uint micros){
	if(!running) return;

	const uint32_t now = millis();
	if(now - lastStepMs < StepMs) return;
	lastStepMs = now;

	activeIdx = (activeIdx + 1) % ChevronN;
	redrawChevrons();
}
