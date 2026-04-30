#include "PhoneBatteryIcon.h"
#include <Loop/LoopManager.h>
#include <Chatter.h>

// MAKERphone retro palette - kept identical with every other Phone* widget
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid, PhoneDialerKey, PhoneDialerPad,
// PhonePixelAvatar, PhoneChatBubble, PhoneSignalIcon) so a screen that
// mixes any of them reads as one coherent device UI. Duplicated rather
// than centralised at this small scale - if the palette ever moves out
// of the widgets, every widget moves together.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple background
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (active fill)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (inactive cells)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream (outline + tip)
#define MP_WARN        lv_color_make(255, 60, 60)    // low-battery red

// ----- ctor / dtor -----

PhoneBatteryIcon::PhoneBatteryIcon(lv_obj_t* parent) : LVObject(parent){
	// Anchor regardless of parent layout. Same flag pattern as the other
	// Phone* atoms so callers can drop the icon into a flex / grid screen
	// and place it explicitly with lv_obj_set_pos() / lv_obj_set_align().
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, IconWidth, IconHeight);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);

	buildOutline();
	buildTip();
	buildCells();

	// Seed with whatever the BatteryService currently reports. If
	// Battery hasn't been begin()'d yet (very early in boot) this just
	// returns 0 and the next loop() tick will catch up.
	level = Battery.getLevel();
	if(level > CellCount) level = CellCount;
	redrawCells(level, level == 0);

	LoopManager::addListener(this);
}

PhoneBatteryIcon::~PhoneBatteryIcon(){
	// Cancel the animation timer first so it can't fire on a freed
	// `this` after we go away. The outline / tip / cell lv_objs are
	// children of `obj` and tear themselves down via LVGL's normal
	// child cleanup.
	stopTimer();
	LoopManager::removeListener(this);
}

// ----- builders -----

void PhoneBatteryIcon::buildOutline(){
	// Body: 15 px wide, 9 px tall, 1 px MP_TEXT border, transparent inside
	// so the cells (4 children) draw cleanly on top of the parent bg.
	outline = lv_obj_create(obj);
	lv_obj_remove_style_all(outline);
	lv_obj_clear_flag(outline, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(outline, 15, IconHeight);
	lv_obj_set_pos(outline, 0, 0);
	lv_obj_set_style_radius(outline, 0, 0);
	lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(outline, 1, 0);
	lv_obj_set_style_border_color(outline, MP_TEXT, 0);
	lv_obj_set_style_border_side(outline, LV_BORDER_SIDE_FULL, 0);
	lv_obj_set_style_pad_all(outline, 0, 0);
}

void PhoneBatteryIcon::buildTip(){
	// 1x5 nub on the right at y=2 (vertically centered against the 9px
	// body). Same MP_TEXT cream as the outline so the body + tip read
	// as a single object.
	tip = lv_obj_create(obj);
	lv_obj_remove_style_all(tip);
	lv_obj_clear_flag(tip, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(tip, 1, 5);
	lv_obj_set_pos(tip, 15, 2);
	lv_obj_set_style_radius(tip, 0, 0);
	lv_obj_set_style_bg_color(tip, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);
}

void PhoneBatteryIcon::buildCells(){
	// 4 cells inside the 15x9 body. Inner area after the 1px border is
	// 13x7. Cells are 2px wide, 5px tall, with 1px gap between them and
	// 1px margin on every side: 1 + (2 + 1)*4 - 1 + 1 = 13. Cells live as
	// children of `outline` so their (x,y) is naturally relative to the
	// body's inner edge.
	const uint8_t cellW = 2;
	const uint8_t cellH = 5;
	const uint8_t gap   = 1;
	const uint8_t marginX = 1; // sits inside the 1 px outline border
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

// ----- redraw helper -----

void PhoneBatteryIcon::redrawCells(uint8_t activeCells, bool warning){
	if(activeCells > CellCount) activeCells = CellCount;
	for(uint8_t i = 0; i < CellCount; i++){
		const bool active = (i < activeCells);
		lv_color_t color;
		if(active){
			color = warning ? MP_WARN : MP_ACCENT;
		}else{
			color = MP_DIM;
		}
		lv_obj_set_style_bg_color(cells[i], color, 0);
	}
}

// ----- LoopListener -----

void PhoneBatteryIcon::loop(uint micros){
	// While charging or pinned, ignore the BatteryService - the timer
	// (charging) or the user (manual) owns what's on screen.
	if(charging || !autoUpdate) return;

	uint8_t newLevel = Battery.getLevel();
	if(newLevel > CellCount) newLevel = CellCount;
	if(newLevel == level && (animTimer != nullptr) == (newLevel == 0)){
		return;
	}
	level = newLevel;
	evaluateAnimation();
	if(!charging){
		// If the new level is 0, evaluateAnimation() has spun up the
		// pulse timer which will own the redraws from now on. Either
		// way, paint the steady-state once so the very first frame is
		// correct.
		redrawCells(level, level == 0);
	}
}

// ----- public API -----

void PhoneBatteryIcon::setLevel(uint8_t newLevel){
	if(newLevel > CellCount) newLevel = CellCount;
	autoUpdate = false;
	level = newLevel;
	evaluateAnimation();
	if(!charging){
		redrawCells(level, level == 0);
	}
}

void PhoneBatteryIcon::setAutoUpdate(bool on){
	autoUpdate = on;
	if(on && !charging){
		// Snap immediately to the current battery reading so the icon
		// doesn't briefly show whatever value was last manually set.
		uint8_t newLevel = Battery.getLevel();
		if(newLevel > CellCount) newLevel = CellCount;
		level = newLevel;
		evaluateAnimation();
		redrawCells(level, level == 0);
	}
}

void PhoneBatteryIcon::setCharging(bool on){
	if(charging == on) return;
	charging = on;
	animStep = 0;
	evaluateAnimation();
	if(!charging){
		// Snap to whatever the steady-state level is now. Auto-update
		// hosts will catch up on the next loop() tick if the actual
		// reading has drifted while we were charging.
		redrawCells(level, level == 0);
	}
}

// ----- timer plumbing -----

void PhoneBatteryIcon::evaluateAnimation(){
	// Three states want a timer: charging cycle, low-battery pulse, and
	// "neither" (no timer at all). Pick the right one and (re)configure
	// the single owned `lv_timer_t*` accordingly.
	if(charging){
		pulseOn = false;
		startTimer(ChargeIntervalMs);
	}else if(level == 0){
		pulseOn = true;
		startTimer(PulseIntervalMs);
	}else{
		pulseOn = false;
		stopTimer();
	}
}

void PhoneBatteryIcon::startTimer(uint16_t intervalMs){
	// Tear down any existing timer first so a state transition (charge
	// -> low-battery pulse, or vice versa) always lands on a freshly
	// scheduled callback at the new period. Avoids leaning on
	// `lv_timer_set_period` which exists on LVGL 8.3+ but is one more
	// API surface to depend on for a widget this small.
	stopTimer();
	animTimer = lv_timer_create(animTimerCb, intervalMs, this);
}

void PhoneBatteryIcon::stopTimer(){
	if(animTimer != nullptr){
		lv_timer_del(animTimer);
		animTimer = nullptr;
	}
}

void PhoneBatteryIcon::animTimerCb(lv_timer_t* timer){
	auto* self = static_cast<PhoneBatteryIcon*>(timer->user_data);
	if(self == nullptr) return;
	if(self->charging){
		// Cycle 1 -> 2 -> 3 -> 4 -> 1 -> ...  (skip 0 so the icon
		// never flickers fully empty during charge, which would read
		// as "broken" rather than "filling up").
		self->animStep = (uint8_t)((self->animStep + 1) % CellCount);
		self->redrawCells((uint8_t)(self->animStep + 1), false);
	}else if(self->pulseOn){
		// Two-frame pulse: 0 cells, then 1 warning cell. Same cadence
		// as a classic low-battery beep.
		self->animStep ^= 1;
		self->redrawCells(self->animStep ? 1 : 0, true);
	}
}
