#include "PhoneIdleHint.h"
#include "../Fonts/font.h"
#include "../MakerphoneTheme.h"
#include <Loop/LoopManager.h>
#include <Input/Input.h>

// Palette: theme-aware so the hint flips to the active skin (Synthwave
// cream + dim purple by default, Nokia 3310 dark olive on pale olive,
// etc.). The caption draws in MP_LABEL_DIM at low opacities and
// brightens to MP_TEXT once it has crested the fade-in - same trick
// the soft-key flash pass uses, kept here so the hint reads as
// "just woke up" rather than "sun-faded".
#define MP_TEXT       (MakerphoneTheme::text())
#define MP_LABEL_DIM  (MakerphoneTheme::labelDim())

PhoneIdleHint::PhoneIdleHint(lv_obj_t* parent) : LVObject(parent){
	// Anchor on top of the host's flex/column layout - the hint
	// sits as a free-floating widget the home screen places by
	// hand. The y offset is chosen so the slab sits clearly above
	// PhoneChargingOverlay (BOTTOM_MID y = -14, height 13 -> spans
	// y = 101..114) and the soft-key bar (y = 118..128). HintHeight
	// is 9, so y = -30 puts the hint at y = 89..98 - clear of both.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, HintWidth, HintHeight);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_align(obj, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(obj, -30);

	caption = lv_label_create(obj);
	lv_label_set_text(caption, "PRESS ANY KEY");
	lv_obj_set_style_text_font(caption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(caption, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_letter_space(caption, 1, 0);
	lv_obj_center(caption);
	// Boot invisible - the very first loop() tick at most flips
	// nothing. The fade-in only starts after IdleMs of stillness.
	lv_obj_set_style_opa(caption, LV_OPA_TRANSP, 0);

	lastActivityMs = millis();
	appliedOpa     = 0;
	shownAtMs      = 0;

	LoopManager::addListener(this);
	Input::getInstance()->addListener(this);
}

PhoneIdleHint::~PhoneIdleHint(){
	// Order matters: drop the listener registrations BEFORE the LVObject
	// base destructor frees `obj`, so a frame in flight does not see a
	// dangling caption pointer.
	LoopManager::removeListener(this);
	Input::getInstance()->removeListener(this);
}

void PhoneIdleHint::setActive(bool a){
	active = a;
	if(!a){
		applyOpa(0);
		shownAtMs      = 0;
		lastActivityMs = millis();
	}else{
		// Re-arming after a gate close - the user has not just touched
		// a key, so don't reset the clock. Let the host call
		// resetActivity() if it wants the idle window to start fresh.
	}
}

void PhoneIdleHint::setText(const char* text){
	if(caption != nullptr && text != nullptr){
		lv_label_set_text(caption, text);
	}
}

void PhoneIdleHint::resetActivity(){
	lastActivityMs = millis();
	if(appliedOpa != 0){
		applyOpa(0);
	}
	shownAtMs = 0;
}

void PhoneIdleHint::loop(uint /*micros*/){
	if(!active){
		if(appliedOpa != 0){
			applyOpa(0);
		}
		return;
	}

	const uint32_t now     = millis();
	const uint32_t idleFor = now - lastActivityMs;
	if(idleFor < IdleMs){
		// Still inside the "user just touched something" window. Keep
		// the hint hidden and the shownAt clock cleared so the next
		// idle cycle starts cleanly from a 0-opa baseline.
		if(appliedOpa != 0){
			applyOpa(0);
			shownAtMs = 0;
		}
		return;
	}

	if(shownAtMs == 0){
		shownAtMs = now;
	}
	const uint32_t shownFor = now - shownAtMs;

	uint16_t opa;
	if(shownFor < FadeMs){
		// Linear ramp 0 -> PeakOpa across the fade window.
		opa = (uint16_t)(((uint32_t) PeakOpa * shownFor) / FadeMs);
	}else{
		// Soft triangle pulse between PulseTroughOpa and PeakOpa, so
		// the hint stays alive-but-quiet without lighting up the LCD
		// every frame at full intensity. Triangle (rather than sine)
		// keeps the maths integer-only and the period predictable.
		const uint32_t phase = (shownFor - FadeMs) % PulseMs;
		uint32_t       tri;
		if(phase < (PulseMs / 2)){
			tri = (phase * 1024U) / (PulseMs / 2);
		}else{
			tri = ((PulseMs - phase) * 1024U) / (PulseMs / 2);
		}
		const uint16_t spread = PeakOpa - PulseTroughOpa;
		opa = PulseTroughOpa + (uint16_t)((spread * tri) / 1024U);
	}
	if(opa > 255){
		opa = 255;
	}
	applyOpa((uint8_t) opa);
}

void PhoneIdleHint::anyKeyPressed(){
	// Any button counts as activity - exactly the contract the
	// PhoneIdleDim service uses. The idle clock resets, the caption
	// snaps invisible, and the next idle cycle starts from a clean
	// 0-opa baseline so the user never sees a half-faded ghost on
	// the very next press.
	lastActivityMs = millis();
	if(appliedOpa != 0){
		applyOpa(0);
	}
	shownAtMs = 0;
}

void PhoneIdleHint::applyOpa(uint8_t opa){
	if(caption == nullptr){
		return;
	}
	if(opa == appliedOpa){
		return;
	}
	lv_obj_set_style_opa(caption, opa, 0);
	// Brighten the colour as the caption rises out of the fade so a
	// just-emerging hint reads as "warming up" rather than as a
	// stale ghost. Threshold matches the half-way point of the
	// PulseTroughOpa..PeakOpa span for a smooth-feeling crossover.
	if(opa > 0){
		lv_obj_set_style_text_color(caption, opa < 140 ? MP_LABEL_DIM : MP_TEXT, 0);
	}
	appliedOpa = opa;
}
