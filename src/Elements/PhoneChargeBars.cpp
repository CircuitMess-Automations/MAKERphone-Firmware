#include "PhoneChargeBars.h"
#include "PhoneChargingOverlay.h"
#include <Loop/LoopManager.h>

// MAKERphone retro palette - kept identical with every other Phone* widget
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid, PhoneDialerKey, PhoneDialerPad,
// PhonePixelAvatar, PhoneChatBubble, PhoneSignalIcon, PhoneBatteryIcon,
// PhoneChargingOverlay) so a screen mixing any of them reads as one
// coherent device UI.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (active fill)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (inactive cells)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream (outline + tip)


PhoneChargeBars::PhoneChargeBars(lv_obj_t* parent, PhoneChargingOverlay* src)
    : LVObject(parent), source(src){
	for(uint8_t i = 0; i < CellCount; i++){
		cells[i] = nullptr;
	}

	// Anchor independently of any flex / grid layout the host screen
	// uses, matching every other Phone* atom. Hosts position the
	// widget explicitly with `lv_obj_set_align` / `lv_obj_set_pos`.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, Width, Height);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);

	buildOutline();
	buildTip();
	buildCells();

	// Hidden until charging flips on. No animation is scheduled and no
	// `LoopListener::loop()` runs while idle; both are spun up lazily
	// by `setCharging(true)` / `setSource()`.
	applyVisibility(false);
	redrawCells(0);

	// Only subscribe to LoopManager when a source was provided. In
	// fully-manual mode the host drives `setCharging()` itself, so we
	// avoid the per-loop overhead of an empty listener entirely.
	if(source != nullptr){
		LoopManager::addListener(this);
		loopRegistered = true;
	}
}

PhoneChargeBars::~PhoneChargeBars(){
	// Cancel the sweep timer first so it can't fire on a freed `this`
	// after we go away. The outline / tip / cell lv_objs are children
	// of `obj` and tear themselves down via LVGL's normal child cleanup.
	stopTimer();
	if(loopRegistered){
		LoopManager::removeListener(this);
		loopRegistered = false;
	}
}

// ----- builders -----

void PhoneChargeBars::buildOutline(){
	// Body: 63 px wide, 9 px tall, 1 px MP_TEXT border, transparent inside
	// so the 10 fill cells (children) draw cleanly on top of whatever
	// wallpaper the host screen is rendering behind us.
	outline = lv_obj_create(obj);
	lv_obj_remove_style_all(outline);
	lv_obj_clear_flag(outline, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(outline, 63, Height);
	lv_obj_set_pos(outline, 0, 0);
	lv_obj_set_style_radius(outline, 0, 0);
	lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(outline, 1, 0);
	lv_obj_set_style_border_color(outline, MP_TEXT, 0);
	lv_obj_set_style_border_side(outline, LV_BORDER_SIDE_FULL, 0);
	lv_obj_set_style_pad_all(outline, 0, 0);
}

void PhoneChargeBars::buildTip(){
	// 1x5 nub on the right at y=2 (vertically centered against the 9px
	// body). Same MP_TEXT cream as the outline so body + tip read as a
	// single object — same silhouette PhoneBatteryIcon uses.
	tip = lv_obj_create(obj);
	lv_obj_remove_style_all(tip);
	lv_obj_clear_flag(tip, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(tip, 1, 5);
	lv_obj_set_pos(tip, 63, 2);
	lv_obj_set_style_radius(tip, 0, 0);
	lv_obj_set_style_bg_color(tip, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);
}

void PhoneChargeBars::buildCells(){
	// 10 cells inside the 63 x 9 body. Inner area after the 1px border
	// is 61 x 7. Cells are 5 px wide, 5 px tall, 1 px gap between them,
	// 1 px margin on every side: 1 + (5 + 1) * 10 - 1 + 1 = 61. They live
	// as children of `outline` so their (x, y) is naturally relative to
	// the body's inner edge.
	const uint8_t cellW   = 5;
	const uint8_t cellH   = 5;
	const uint8_t gap     = 1;
	const uint8_t marginX = 1;
	const uint8_t marginY = 1;
	for(uint8_t i = 0; i < CellCount; i++){
		lv_obj_t* c = lv_obj_create(outline);
		lv_obj_remove_style_all(c);
		lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(c, cellW, cellH);
		lv_obj_set_pos(c, marginX + i * (cellW + gap), marginY);
		lv_obj_set_style_radius(c, 0, 0);
		lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(c, MP_DIM, 0);
		cells[i] = c;
	}
}

// ----- redraw helpers -----

void PhoneChargeBars::redrawCells(uint8_t activeCells){
	if(activeCells > CellCount) activeCells = CellCount;
	for(uint8_t i = 0; i < CellCount; i++){
		if(cells[i] == nullptr) continue;
		const bool active = (i < activeCells);
		lv_obj_set_style_bg_color(cells[i], active ? MP_ACCENT : MP_DIM, 0);
	}
}

void PhoneChargeBars::applyVisibility(bool show){
	if(show){
		lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	}
}

// ----- public API -----

void PhoneChargeBars::setCharging(bool on){
	if(charging == on) return;
	charging = on;
	step     = 0;

	if(on){
		// Reset to "1 cell active" so the sweep starts on a meaningful
		// frame instead of an empty tank that would read as broken for
		// one tick.
		redrawCells(1);
		applyVisibility(true);
		startTimer();
	}else{
		stopTimer();
		redrawCells(0);
		applyVisibility(false);
	}
}

void PhoneChargeBars::setSource(PhoneChargingOverlay* src){
	source = src;
	if(source != nullptr && !loopRegistered){
		LoopManager::addListener(this);
		loopRegistered = true;
	}else if(source == nullptr && loopRegistered){
		LoopManager::removeListener(this);
		loopRegistered = false;
	}
}

// ----- LoopListener -----

void PhoneChargeBars::loop(uint micros){
	(void) micros;
	if(source == nullptr) return;
	const bool srcCharging = source->isCharging();
	if(srcCharging != charging){
		setCharging(srcCharging);
	}
}

// ----- timer plumbing -----

void PhoneChargeBars::startTimer(){
	// Tear down any stale timer first so toggling charging off and on
	// in quick succession always lands on a freshly scheduled callback.
	stopTimer();
	animTimer = lv_timer_create(animTimerCb, SweepIntervalMs, this);
}

void PhoneChargeBars::stopTimer(){
	if(animTimer != nullptr){
		lv_timer_del(animTimer);
		animTimer = nullptr;
	}
}

void PhoneChargeBars::animTimerCb(lv_timer_t* timer){
	auto* self = static_cast<PhoneChargeBars*>(timer->user_data);
	if(self == nullptr) return;
	if(!self->charging) return;

	// Sweep 1 -> 2 -> ... -> CellCount -> 1 -> ...   We start the cycle
	// at "1 cell active" rather than 0 so the tank never visually
	// empties mid-charge — which would read as the device unplugging
	// rather than charging up.
	self->step = (uint8_t)((self->step + 1) % CellCount);
	const uint8_t active = (uint8_t)(self->step + 1);
	self->redrawCells(active);
}
