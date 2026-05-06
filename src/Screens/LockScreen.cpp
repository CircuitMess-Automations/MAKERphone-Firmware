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
#include "../Elements/PhoneMissedCallFlash.h"
#include "../Services/MissedCallLog.h"
#include "../Services/PhoneOwnerEmoji.h"
#include "../Services/PhoneAlarmService.h"
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
	auto* face = new PhoneClockFace(obj);
	clockFaceObj = face->getLvObj();
	clockFace    = face;

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
	refreshOwnerEmoji();
	layoutForOwner();

	// S158 - "Missed call inverted-color flash on next wake".
	// Built last so its full-screen sheet is the top-most child of
	// `obj` and the pulse covers every other widget (status bar,
	// clock, preview strip, soft-keys, charging chip, charge bars).
	// Hidden by default; onStarting() flips it on whenever the
	// shared MissedCallLog has a pending wake-flash queued up.
	missedFlash = new PhoneMissedCallFlash(obj);

	// S184 - apply the persisted lock-widget composition (ClockDate /
	// ClockOnly / ClockEvent). Done after every widget is built so
	// applyLockWidgetMode() can hide the clock face's date rows and
	// optionally mount the "NEXT ALARM" preview line on top of them.
	applyLockWidgetMode();

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
	refreshOwnerEmoji();
	layoutForOwner();
	// S184 - re-apply the persisted lock-widget mode so a
	// PhoneLockWidgetScreen edit -> pop-back-to-lock cycle picks up
	// the new composition without rebuilding the whole screen.
	applyLockWidgetMode();
	loadUnread();
	if(preview) preview->refresh();
	slide->reset();
	if(lockHint){
		lockHint->setBoost(false);
		lockHint->setActive(true);
	}

	// S158 - "Missed call inverted-color flash on next wake".
	// `consumePendingFlash()` is read-and-clear so even a quick
	// lock -> unlock -> lock cycle can never replay the same
	// flash. We trigger after the layout / preview refresh so the
	// pulse fires over the just-redrawn missed-call line in the
	// `PhoneNotificationPreview` strip - the eye snaps to the
	// flashing screen, then settles on the populated preview row.
	if(missedFlash != nullptr && MissedCallLog::instance().consumePendingFlash()){
		missedFlash->start();
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
	// S158 - cancel any in-flight wake-flash so a screen pop
	// mid-pulse does not leak the animation reference into
	// the LVGL animator.
	if(missedFlash) missedFlash->stop();
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

// ----- S188: owner-emoji glyph ---------------------------------------------

void LockScreen::refreshOwnerEmoji(){
	const uint8_t idx = PhoneOwnerEmoji::clampedId(Settings.get().ownerEmoji);
	const bool    on  = PhoneOwnerEmoji::isVisible(idx);

	if(on){
		// Lazy-construct the host container and its 9x9 cell grid the
		// first time we need them. The container is anchored to the top
		// edge of the screen at x=4 so the glyph sits flush against the
		// left wallpaper margin -- the owner-name label sits centred in
		// the same strip so the two never overlap horizontally (the
		// label dot-truncates at 156 px which already accounts for the
		// 11 px-wide emoji column on the left).
		if(ownerEmojiBox == nullptr){
			ownerEmojiBox = lv_obj_create(obj);
			lv_obj_remove_style_all(ownerEmojiBox);
			lv_obj_add_flag(ownerEmojiBox, LV_OBJ_FLAG_IGNORE_LAYOUT);
			lv_obj_clear_flag(ownerEmojiBox, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_set_size(ownerEmojiBox,
				PhoneOwnerEmoji::Width, PhoneOwnerEmoji::Height);
			lv_obj_set_pos(ownerEmojiBox, 4, 12);
			lv_obj_set_style_pad_all(ownerEmojiBox, 0, 0);
			lv_obj_set_style_bg_opa(ownerEmojiBox, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_width(ownerEmojiBox, 0, 0);

			for(uint8_t y = 0; y < PhoneOwnerEmoji::Height; ++y){
				for(uint8_t x = 0; x < PhoneOwnerEmoji::Width; ++x){
					lv_obj_t* c = lv_obj_create(ownerEmojiBox);
					lv_obj_remove_style_all(c);
					lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
					lv_obj_set_size(c, 1, 1);
					lv_obj_set_pos(c, x, y);
					lv_obj_set_style_radius(c, 0, 0);
					lv_obj_set_style_border_width(c, 0, 0);
					lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
					ownerEmojiCells[y][x] = c;
				}
			}
		}

		// Repaint: walk the catalogue glyph one cell at a time and flip
		// each cell's fill on / off. Set pixels paint in MP_ACCENT
		// (sunset orange) so the small glyph reads as a deliberate
		// identity marker against the synthwave wallpaper.
		for(uint8_t y = 0; y < PhoneOwnerEmoji::Height; ++y){
			for(uint8_t x = 0; x < PhoneOwnerEmoji::Width; ++x){
				lv_obj_t* c = ownerEmojiCells[y][x];
				if(c == nullptr) continue;
				const bool px = PhoneOwnerEmoji::pixelAt(idx, x, y);
				if(px){
					lv_obj_set_style_bg_color(c,
						lv_color_make(255, 140, 30), 0);
					lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
				}else{
					lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
				}
			}
		}

		lv_obj_clear_flag(ownerEmojiBox, LV_OBJ_FLAG_HIDDEN);
	}else{
		// idx == 0 (None) -- hide the glyph so the strip falls back to
		// the pre-S188 owner-name-only / blank layout.
		if(ownerEmojiBox != nullptr){
			lv_obj_add_flag(ownerEmojiBox, LV_OBJ_FLAG_HIDDEN);
		}
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


// ----- S184: lock-widget composition picker --------------------------------

void LockScreen::applyLockWidgetMode(){
	// Defensive read: persisted bytes outside [0..2] clamp to ClockDate,
	// the factory default. Same defensive pattern soundProfile /
	// wallpaperStyle / themeId / keyTicks already use against NVS-resize
	// wipes that read the new byte as uninitialised garbage.
	uint8_t mode = Settings.get().lockWidgetMode;
	if(mode > 2) mode = 0;

	// 1) Date rows on the PhoneClockFace - visible only in ClockDate.
	if(clockFace != nullptr){
		clockFace->setDateVisible(mode == 0);
	}

	// 2) Next-alarm preview line - mounted lazily on first use and
	//    kept around for the rest of the screen's lifetime. Aligned
	//    just under the clock digits so it lands roughly where the
	//    weekday row would otherwise sit; a no-op when ClockOnly /
	//    ClockDate are selected.
	if(mode == 2){
		if(nextEventLabel == nullptr){
			nextEventLabel = lv_label_create(obj);
			lv_obj_add_flag(nextEventLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
			lv_obj_set_style_text_font(nextEventLabel, &pixelbasic7, 0);
			lv_label_set_long_mode(nextEventLabel, LV_LABEL_LONG_DOT);
			lv_obj_set_width(nextEventLabel, 156);
			lv_obj_set_style_text_align(nextEventLabel,
				LV_TEXT_ALIGN_CENTER, 0);
			lv_obj_set_align(nextEventLabel, LV_ALIGN_TOP_MID);
		}

		// Walk the global PhoneAlarmService alarm table and find the
		// next armed alarm by minutes-from-now in [0..1440). The
		// resulting "NEXT ALARM HH:MM" line previews the user's
		// closest pending event the way a Sony-Ericsson agenda widget
		// would. When no alarm is enabled we fall back to
		// "NO ALARMS SET" in the dim caption colour rather than going
		// blank, so the layout stays balanced.
		int32_t bestDelta = -1;
		uint8_t bestHH = 0;
		uint8_t bestMM = 0;

		uint32_t secs   = millis() / 1000UL;
		uint16_t nowTot = (secs / 60UL) % (24UL * 60UL);

		for(uint8_t i = 0; i < PhoneAlarmService::MaxAlarms; ++i){
			PhoneAlarmService::Alarm a = Alarms.getAlarm(i);
			if(!a.enabled) continue;
			int32_t alarmTot = (int32_t)a.hour * 60 + a.minute;
			int32_t delta = alarmTot - (int32_t)nowTot;
			if(delta < 0) delta += 24 * 60;
			if(bestDelta < 0 || delta < bestDelta){
				bestDelta = delta;
				bestHH    = a.hour;
				bestMM    = a.minute;
			}
		}

		if(bestDelta >= 0){
			char buf[24];
			snprintf(buf, sizeof(buf), "NEXT ALARM %02u:%02u",
				(unsigned)bestHH, (unsigned)bestMM);
			lv_label_set_text(nextEventLabel, buf);
			lv_obj_set_style_text_color(nextEventLabel,
				lv_color_make(255, 140, 30), 0);  // sunset orange (MP_ACCENT)
		}else{
			lv_label_set_text(nextEventLabel, "NO ALARMS SET");
			lv_obj_set_style_text_color(nextEventLabel,
				lv_color_make(170, 140, 200), 0); // dim caption (MP_LABEL_DIM)
		}

		// Anchor right under the clock face's HH:MM digits, where the
		// hidden dowLabel would otherwise sit. clockFaceObj's Y was
		// already shifted by ownerStripH in layoutForOwner(), so the
		// preview tracks the owner-name strip too.
		lv_coord_t baseY = (clockFaceObj != nullptr)
			? lv_obj_get_y(clockFaceObj)
			: kClockBaseY + ownerStripH;
		lv_obj_set_y(nextEventLabel, baseY + 18);
		lv_obj_clear_flag(nextEventLabel, LV_OBJ_FLAG_HIDDEN);
		// Make sure the freshly-mounted preview lands above the synthwave
		// wallpaper / clock face but below the missed-call flash.
		lv_obj_move_foreground(nextEventLabel);
		if(missedFlash != nullptr){
			lv_obj_move_foreground(missedFlash->getLvObj());
		}
	}else{
		if(nextEventLabel != nullptr){
			lv_obj_add_flag(nextEventLabel, LV_OBJ_FLAG_HIDDEN);
		}
	}
}
