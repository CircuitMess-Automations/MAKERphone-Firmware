#include "LockScreen.h"
#include "../Storage/Storage.h"
#include "../Fonts/font.h"
#include "../Services/SleepService.h"
#include "../Elements/BatteryElement.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneClockFace.h"
#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneLockHint.h"
#include <Input/Input.h>
#include <Pins.hpp>

LockScreen* LockScreen::instance = nullptr;

LockScreen::LockScreen() : LVScreen(){
	unreads.reserve(4);

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);

	// Synthwave wallpaper - sits at the back of the z-order so every other
	// widget on this screen overlays it. Constructed first so it is the
	// first child of obj and therefore the bottom of LVGL's draw stack.
	new PhoneSynthwaveBg(obj);

	container = lv_obj_create(obj);
	lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
	lv_obj_set_layout(container, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(container, 3, 0);
	// Let the synthwave wallpaper show through behind the unread-message rows.
	lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);

	// MAKERphone retro status bar (signal | clock | battery) anchored to the top.
	new PhoneStatusBar(obj);

	// Big retro clock face directly under the status bar.
	new PhoneClockFace(obj);

	// 11 px (status bar + 1 px separator) + clock face height + 2 px gap.
	lv_obj_set_style_pad_top(container, 11 + PhoneClockFace::FaceHeight + 2, 0);

	// Soft-key bar foreshadows the future Phase 3-4 wiring.
	auto softkeys = new PhoneSoftKeyBar(obj);
	softkeys->setLeft("CALL");
	softkeys->setRight("MENU");

	// S47 phone-style redesign: a code-only "SLIDE TO UNLOCK" hint sweeps
	// three cyan chevrons L->R just above the unlock slide. S48 adds the
	// chord-armed shimmer state on top of this widget.
	lockHint = new PhoneLockHint(obj);
	lv_obj_set_align(lockHint->getLvObj(), LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(lockHint->getLvObj(), -(PhoneSoftKeyBar::BarHeight + 18 + 1));

	// Reserve enough room at the bottom of the unread-messages container
	// so the message rows never slip behind the hint or the unlock bar.
	lv_obj_set_style_pad_bottom(
			container,
			PhoneSoftKeyBar::BarHeight + 18 + PhoneLockHint::HintHeight + 2,
			0
	);

	slide = new UnlockSlide(obj, [this](){
		stop();
		instance = nullptr;

		auto parent = getParent();
		if(parent){
			parent->start(true, LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
		}

		lv_obj_del_delayed(obj, 500);
	});

	// S48: when the chord-arming window expires without a BTN_RIGHT,
	// drop the matching shimmer boost on the hint so the user is back
	// in the calm idle visual.
	slide->setOnArmTimeout([this](){
		if(lockHint) lockHint->setBoost(false);
	});

	lv_obj_add_flag(slide->getLvObj(), LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_align(slide->getLvObj(), LV_ALIGN_BOTTOM_MID);
	// Lift the unlock slide just above the softkey bar so the two do not overlap.
	lv_obj_set_y(slide->getLvObj(), -PhoneSoftKeyBar::BarHeight);

	instance = this;
}

LockScreen::~LockScreen(){

}

void LockScreen::activate(LVScreen* parent){
	parent->stop();

	if(instance){
		if(parent != instance){
			instance->setParent(parent);
		}

		instance->start();
	}else{
		auto screen = new LockScreen();
		screen->setParent(parent);
		screen->start();
	}
}

void LockScreen::onStarting(){
	loadUnread();
	slide->reset();
	if(lockHint){
		lockHint->setBoost(false);
		lockHint->setActive(true);
	}
}

void LockScreen::onStart(){
	Input::getInstance()->addListener(this);
	Messages.addUnreadListener(this);
}

void LockScreen::onStop(){
	slide->stop();
	if(lockHint){
		lockHint->setBoost(false);
		lockHint->setActive(false);
	}
	Input::getInstance()->removeListener(this);
	Messages.removeUnreadListener(this);
}

void LockScreen::buttonPressed(uint i){
	// BTN_BACK is the universal "go to sleep" key on the lock screen.
	// While a chord is armed, treat it as a cancel instead of immediate
	// sleep so the user can recover from a misfire.
	if(i == BTN_BACK){
		if(slide->isArmed()){
			slide->reset();
			if(lockHint) lockHint->setBoost(false);
			return;
		}
		Sleep.enterSleep();
		return;
	}

	// Legacy fallback: BTN_R hold still slides the lock all the way open.
	if(i == BTN_R){
		if(slide->isArmed()){
			slide->reset();
			if(lockHint) lockHint->setBoost(false);
		}
		slide->start();
		return;
	}

	// S48: BTN_LEFT arms the chord. The slide animates halfway and the
	// hint flips into shimmer-boost mode so the user can see the next
	// expected key (BTN_RIGHT).
	if(i == BTN_LEFT){
		if(slide->isArmed()) return;     // already armed - ignore re-press
		slide->armChord();
		if(lockHint) lockHint->setBoost(true);
		return;
	}

	// S48: BTN_RIGHT after BTN_LEFT completes the chord and unlocks.
	// BTN_RIGHT alone is treated as a wrong-key shake.
	if(i == BTN_RIGHT){
		if(slide->isArmed()){
			slide->completeChord();
			if(lockHint) lockHint->setBoost(false);
			return;
		}
		slide->shake();
		return;
	}

	// Any other key: cancel any armed chord and shake the slide.
	if(slide->isArmed()){
		slide->reset();
		if(lockHint) lockHint->setBoost(false);
	}
	slide->shake();
}

void LockScreen::buttonReleased(uint i){
	if(i != BTN_R) return;

	// Only the legacy hold path is interrupted by release; the chord
	// states ignore stop()/reset() while they own the slide.
	slide->stop();
	slide->reset();
}

void LockScreen::clearUnreads(){
	for(auto el : unreads){
		lv_obj_del(el->getLvObj());
	}
	unreads.clear();

	if(noUnreads){
		lv_obj_del(noUnreads);
		noUnreads = nullptr;
	}
}

void LockScreen::onUnread(bool unread){
	clearUnreads();

	if(unread){
		loadUnread();
	}else{
		createNoUnreads();
	}
}

void LockScreen::loadUnread(){
	clearUnreads();

	auto convos = Storage.Convos.all();
	std::reverse(convos.begin(), convos.end());

	for(auto convo : convos){
		if(!Storage.Convos.get(convo).unread) continue;

		auto fren = Storage.Friends.get(convo);

		auto el = new UserWithMessage(container, fren);
		unreads.push_back(el);

		if(unreads.size() >= 2) break;
	}

	if(unreads.empty()){
		createNoUnreads();
	}
}

void LockScreen::createNoUnreads(){
	clearUnreads();

	noUnreads = lv_label_create(container);
	lv_label_set_text(noUnreads, "You have no new messages.");
	lv_obj_set_style_text_font(noUnreads, &pixelbasic7, 0);
	lv_obj_set_style_text_color(noUnreads, lv_color_make(200, 200, 200), 0);
}
