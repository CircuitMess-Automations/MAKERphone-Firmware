#include "PhoneStatusBar.h"
#include "../Fonts/font.h"
#include <Loop/LoopManager.h>
#include <Chatter.h>
#include <stdio.h>

// MAKERphone retro palette
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple bar background
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (separator + battery fill)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (active signal bars)
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (inactive bars / battery outline interior)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream digits

PhoneStatusBar::PhoneStatusBar(lv_obj_t* parent) : LVObject(parent){
	// Anchor to the top regardless of parent layout.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, 160, BarHeight);
	lv_obj_set_pos(obj, 0, 0);

	// Background slab + thin orange separator at the bottom.
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(obj, MP_ACCENT, 0);
	lv_obj_set_style_border_width(obj, 1, 0);

	buildSignal();
	buildClock();
	buildBattery();

	updateBattery();
	updateClock();
	redrawSignal();

	LoopManager::addListener(this);
}

PhoneStatusBar::~PhoneStatusBar(){
	LoopManager::removeListener(this);
}

// ----- builders -----

void PhoneStatusBar::buildSignal(){
	// 4 vertical bars, increasing height, total footprint ~14x7 px.
	// Heights: 2, 4, 6, 7. Drawn left-aligned starting at x=2.
	const uint8_t heights[SignalBarCount] = { 2, 4, 6, 7 };
	uint8_t x = 2;
	for(uint8_t i = 0; i < SignalBarCount; i++){
		lv_obj_t* b = lv_obj_create(obj);
		lv_obj_remove_style_all(b);
		lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(b, 2, heights[i]);
		// Bottom-align bars at y = 8 (1 px above the orange separator).
		lv_obj_set_pos(b, x, 8 - heights[i]);
		lv_obj_set_style_radius(b, 0, 0);
		lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
		signalBars[i] = b;
		x += 3;
	}
}

void PhoneStatusBar::buildClock(){
	clockLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(clockLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(clockLabel, MP_TEXT, 0);
	lv_label_set_text(clockLabel, "00:00");
	lv_obj_set_align(clockLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(clockLabel, 1);
}

void PhoneStatusBar::buildBattery(){
	// Battery glyph: outline 14x6 with a 1x3 tip on the right.
	battOutline = lv_obj_create(obj);
	lv_obj_remove_style_all(battOutline);
	lv_obj_clear_flag(battOutline, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(battOutline, 14, 6);
	// Position so the whole battery (outline + 1px tip) ends at x=159.
	lv_obj_set_pos(battOutline, 144, 2);
	lv_obj_set_style_radius(battOutline, 0, 0);
	lv_obj_set_style_bg_opa(battOutline, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(battOutline, 1, 0);
	lv_obj_set_style_border_color(battOutline, MP_TEXT, 0);
	lv_obj_set_style_border_side(battOutline, LV_BORDER_SIDE_FULL, 0);

	// Inner fill: up to 12x4 inside the 1px border.
	battFill = lv_obj_create(battOutline);
	lv_obj_remove_style_all(battFill);
	lv_obj_clear_flag(battFill, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(battFill, 12, 4);
	lv_obj_set_pos(battFill, 1, 1);
	lv_obj_set_style_radius(battFill, 0, 0);
	lv_obj_set_style_bg_color(battFill, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(battFill, LV_OPA_COVER, 0);

	// Battery tip: 1x3 nub to the right of the outline.
	battTip = lv_obj_create(obj);
	lv_obj_remove_style_all(battTip);
	lv_obj_clear_flag(battTip, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(battTip, 1, 3);
	lv_obj_set_pos(battTip, 158, 3);
	lv_obj_set_style_radius(battTip, 0, 0);
	lv_obj_set_style_bg_color(battTip, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(battTip, LV_OPA_COVER, 0);
}

// ----- updaters -----

void PhoneStatusBar::redrawSignal(){
	for(uint8_t i = 0; i < SignalBarCount; i++){
		bool active = (i < signal);
		lv_obj_set_style_bg_color(signalBars[i],
			active ? MP_HIGHLIGHT : MP_DIM, 0);
	}
}

void PhoneStatusBar::updateBattery(){
	uint8_t newLevel = Battery.getLevel(); // 0..4 nominal
	if(newLevel == level) return;
	level = newLevel;

	// Clamp level to the 0..4 visual range.
	uint8_t v = (level > 4) ? 4 : level;

	// Map to inner-fill width: 0 -> 0px, 4 -> 12px.
	uint8_t w = (v * 12) / 4;
	lv_obj_set_width(battFill, w);

	// Color shifts to red when low (level 0 or 1) for that "battery low" feel.
	if(v <= 1){
		lv_obj_set_style_bg_color(battFill, lv_color_make(255, 60, 60), 0);
	}else{
		lv_obj_set_style_bg_color(battFill, MP_ACCENT, 0);
	}
}

void PhoneStatusBar::updateClock(){
	// Uptime-based clock. millis() rolls over at ~49 days; modulo by 24h
	// keeps the display sane in the meantime. When a real time source is
	// wired in, replace this body.
	uint32_t secs = millis() / 1000UL;
	uint16_t totalMin = (secs / 60UL) % (24UL * 60UL);
	if(totalMin == lastMin) return;
	lastMin = totalMin;

	uint8_t hh = totalMin / 60;
	uint8_t mm = totalMin % 60;
	char buf[6];
	snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned) hh, (unsigned) mm);
	lv_label_set_text(clockLabel, buf);
}

void PhoneStatusBar::loop(uint micros){
	updateBattery();
	updateClock();
}

void PhoneStatusBar::setSignal(uint8_t bars){
	if(bars > SignalBarCount) bars = SignalBarCount;
	if(bars == signal) return;
	signal = bars;
	redrawSignal();
}
