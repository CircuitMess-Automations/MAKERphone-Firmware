#include "LockScreen.h"
#include "../Storage/Storage.h"
#include "../Fonts/font.h"
#include <Settings.h>
#include <string.h>
#include "../Services/SleepService.h"
#include "../Elements/BatteryElement.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneClockFace.h"
#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneLockHint.h"
#include "../Elements/PhoneNotificationPreview.h"
#include "../Elements/PhoneChargingOverlay.h"
#include "../Elements/PhoneChargeBars.h"
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

	// Big retro clock face directly under the status bar. Cached on
	// the screen so S144's layoutForOwner() can re-anchor it when the
	// user sets / clears Settings.ownerName without rebuilding the
	// whole screen.
	auto* clockFace = new PhoneClockFace(obj);
	clockFaceObj = clockFace->getLvObj();

	// S49 lock-screen notifications preview: a compact two-row summary
	// of unread messages + missed calls. Anchored just under the clock
	// face. Self-hides when both streams are empty so the existing
	// "Nothing new" centre label can take the floor.
	preview = new PhoneNotificationPreview(obj);
	lv_obj_set_align(preview->getLvObj(), LV_ALIGN_TOP_MID);
	lv_obj_set_y(preview->getLvObj(), 11 + PhoneClockFace::FaceHeight + 2);

	// 11 px (status bar + 1 px separator) + clock face height + 2 px gap
	// + preview height + 2 px gap. Pushes the unread-message rows below
	// the new preview so the two never overlap.
	lv_obj_set_style_pad_top(
			container,
			11 + PhoneClockFace::FaceHeight + 2 + PhoneNotificationPreview::PreviewHeight + 2,
			0
	);

	// Soft-key bar foreshadows the future Phase 3-4 wiring.
	auto softkeys = new PhoneSoftKeyBar(obj);
	softkeys->setLeft("CALL");
	softkeys->setRight("MENU");

	// S47 phone-style redesign: a code-only "SLIDE TO UNLOCK" hint sweeps
	// three cyan chevrons L->R just above the unlock slide. S48 adds the
	// chord-armed shimmer state on top of this widget.
	// S59 charging overlay - hidden by default, mounts above the lock
	// hint and below the unlock slide. Auto-detect mode lets the widget
	// own its visibility based on a voltage-trend heuristic.
	chargingOverlay = new PhoneChargingOverlay(obj);
	lv_obj_set_align(chargingOverlay->getLvObj(), LV_ALIGN_BOTTOM_MID);
	// 10 (soft-key bar) + 18 (unlock slide gap) + 1 (separator) + HintHeight
	// + 4 (gap) was the original S59 anchor. S155 lifts the chip another
	// PhoneChargeBars::Height + 4 (gap) + 4 (bars-to-chip gap) px so the
	// new wide fill-bars strip can sit immediately under the chip while
	// charging without colliding with the slide-to-unlock hint below it.
	lv_obj_set_y(chargingOverlay->getLvObj(),
		-(int16_t)(PhoneSoftKeyBar::BarHeight + 18 + 1 + PhoneLockHint::HintHeight + 4
			+ PhoneChargeBars::Height + 4 + 4));
	chargingOverlay->setAutoDetect(true);

	// S155 - wide animated fill-bars strip mirrors the chip's
	// isCharging() each loop. Sits 4 px above the slide-to-unlock
	// hint (so the unlock affordance is unobstructed) and 4 px below
	// the chip itself, giving the lock screen the classic Sony-
	// Ericsson "tank fills up" silhouette during charge. Hidden by
	// default; the LoopListener inside PhoneChargeBars flips it on
	// once the chip's auto-detect heuristic agrees the device is
	// charging, so no host wiring beyond construction is required.
	chargeBars = new PhoneChargeBars(obj, chargingOverlay);
	lv_obj_set_align(chargeBars->getLvObj(), LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(chargeBars->getLvObj(),
		-(int16_t)(PhoneSoftKeyBar::BarHeight + 18 + 1 + PhoneLockHint::HintHeight + 4));

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

	// S144 - mount the owner-name strip if Settings.ownerName is set;
	// re-anchor the clock face / preview / unread container Y offsets
	// to match. Safe to call before the screen is start()'d -- the
	// helpers only touch widgets that this constructor has already
	// created.
	refreshOwnerLabel();
	layoutForOwner();

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
	// S144 - re-read Settings.ownerName so an edit made while the user
	// was inside PhoneOwnerNameScreen propagates here on the next
	// onStarting() pass without requiring a screen rebuild.
	refreshOwnerLabel();
	layoutForOwner();
	loadUnread();
	if(preview) preview->refresh();
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

	// S49: poke the summary preview so its empty-state visibility
	// flips in lock-step with the rebuilt row list.
	if(preview){
		preview->refresh();
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

		if(unreads.size() >= 1) break;
	}

	if(unreads.empty()){
		createNoUnreads();
	}
}

void LockScreen::createNoUnreads(){
	clearUnreads();

	// S49: when the summary preview already has missed-call content to
	// render, suppress the centre label so the lock screen reads as
	// "you have notifications" rather than "nothing waiting".
	if(preview && !preview->isEmpty()){
		noUnreads = nullptr;
		return;
	}

	noUnreads = lv_label_create(container);
	lv_label_set_text(noUnreads, "Nothing new.");
	lv_obj_set_style_text_font(noUnreads, &pixelbasic7, 0);
	lv_obj_set_style_text_color(noUnreads, lv_color_make(200, 200, 200), 0);
}


// ----- S144: owner-name strip ----------------------------------------------

void LockScreen::refreshOwnerLabel(){
	const char* name = Settings.get().ownerName;
	const bool  set  = (name != nullptr) && (name[0] != '\0');

	if(set){
		// Lazy-construct the label the first time we need it. Painted
		// in MP_HIGHLIGHT (cyan) on top of the synthwave wallpaper so
		// it reads as a small "phone identity" greeting strip.
		if(ownerLabel == nullptr){
			ownerLabel = lv_label_create(obj);
			lv_obj_add_flag(ownerLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
			lv_obj_set_style_text_font(ownerLabel, &pixelbasic7, 0);
			lv_obj_set_style_text_color(ownerLabel,
				lv_color_make(122, 232, 255), 0);
			lv_label_set_long_mode(ownerLabel, LV_LABEL_LONG_DOT);
			// Slightly less than the full screen width so a too-long
			// name dot-truncates rather than crowding the bezel.
			lv_obj_set_width(ownerLabel, 156);
			lv_obj_set_style_text_align(ownerLabel,
				LV_TEXT_ALIGN_CENTER, 0);
			lv_obj_set_align(ownerLabel, LV_ALIGN_TOP_MID);
			// Sits just below the 10 px status bar.
			lv_obj_set_y(ownerLabel, 11);
		}

		// Defensive: keep the displayed text capped at the 23-char
		// editable budget so a corrupt blob with no nul terminator
		// cannot overrun the buffer when copied through lv_label.
		char safe[24];
		const size_t cap = sizeof(safe) - 1;
		size_t n = 0;
		while(n < cap && name[n] != '\0') ++n;
		memcpy(safe, name, n);
		safe[n] = '\0';
		lv_label_set_text(ownerLabel, safe);
		lv_obj_clear_flag(ownerLabel, LV_OBJ_FLAG_HIDDEN);

		ownerStripH = kOwnerStripH;
	}else{
		// Name unset (factory default) - hide the label and fall back
		// to the pre-S144 layout (clock anchored under the status bar).
		if(ownerLabel != nullptr){
			lv_label_set_text(ownerLabel, "");
			lv_obj_add_flag(ownerLabel, LV_OBJ_FLAG_HIDDEN);
		}
		ownerStripH = 0;
	}
}

void LockScreen::layoutForOwner(){
	// Re-anchor the clock face: y = 11 (base) when the strip is hidden,
	// y = 11 + ownerStripH when the strip is visible. Everything below
	// shifts with it because PhoneNotificationPreview / the unread
	// container are positioned relative to the clock-face base + the
	// PreviewHeight constant.
	if(clockFaceObj != nullptr){
		lv_obj_set_y(clockFaceObj, kClockBaseY + ownerStripH);
	}

	if(preview != nullptr){
		// Original Y = 11 + FaceHeight + 2; the +ownerStripH preserves
		// the same gap-to-clock relationship after the shift.
		lv_obj_set_y(preview->getLvObj(),
			11 + PhoneClockFace::FaceHeight + 2 + ownerStripH);
	}

	if(container != nullptr){
		// Re-derive the container's top padding so the unread-message
		// rows stay tucked under the (shifted) preview.
		lv_obj_set_style_pad_top(
			container,
			11 + PhoneClockFace::FaceHeight + 2
				+ PhoneNotificationPreview::PreviewHeight + 2
				+ ownerStripH,
			0
		);
	}
}
