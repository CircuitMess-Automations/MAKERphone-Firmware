#include "PhoneActiveCall.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhonePixelAvatar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneIncomingCall.cpp / PhoneDialerScreen.cpp /
// PhoneAppStubScreen.cpp). The active-call screen uses cyan for the
// caption ("calm / connected" reading) so it visually differs from the
// orange "alert" caption of PhoneIncomingCall - a quick way to tell the
// two apart at a glance even before the name / timer are read. Sunset
// orange is reserved for the * MUTED * indicator (the only attention-
// grabbing state the screen has) and the END softkey arrow.
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (END / MUTED)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan (caption)
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (name + timer)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple (unused but kept for symmetry)

PhoneActiveCall::PhoneActiveCall(const char* name,
								 const char* number,
								 uint8_t seed)
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  avatar(nullptr),
		  captionLabel(nullptr),
		  nameLabel(nullptr),
		  timerLabel(nullptr),
		  mutedLabel(nullptr),
		  avatarSeed(seed) {

	// Zero the name / number buffers up front so getCallerName /
	// getCallerNumber return valid c-strings before copyName / copyNumber
	// runs (defensive against an early access from a caller's setOn*).
	callerName[0]   = '\0';
	callerNumber[0] = '\0';

	// Full-screen container, no scrollbars, no inner padding - same
	// blank-canvas pattern as PhoneIncomingCall / PhoneDialerScreen.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. Every
	// other element overlays it without any opacity gymnastics on the
	// parent. Same z-order pattern as PhoneIncomingCall.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall) so the user
	// never loses the device-state context mid-call.
	statusBar = new PhoneStatusBar(obj);

	// Caption + avatar block + name/timer/muted labels, each their own
	// helper to keep the ctor readable.
	buildCaption();
	buildAvatarBlock();
	buildLabels();

	// Copy the caller info now so the on-screen name reflects the values
	// passed in. copyName / copyNumber both bound the destination buffer
	// via MaxNameLen / MaxNumberLen so a long string truncates cleanly.
	copyName(name);
	copyNumber(number);
	refreshNameLabel();

	// Render the timer label at "00:00" up-front so the screen never
	// shows an empty slot during the first 250 ms before the tick timer
	// fires. lastSecond stays 0xFFFFFFFFu so the first real tick will
	// still redraw to the correct value.
	if(timerLabel != nullptr) {
		lv_label_set_text(timerLabel, "00:00");
	}

	// Bottom: feature-phone soft-keys. MUTE on the left (BTN_LEFT) and
	// END on the right (BTN_RIGHT) match the established Phase-D
	// orientation that PhoneDialerScreen / PhoneIncomingCall set up.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("MUTE");
	softKeys->setRight("END");

	// Long-press detection on both BTN_LEFT and BTN_RIGHT so a stuck
	// finger does not double-fire on key release. Same 600 ms threshold
	// as the rest of the MAKERphone shell, so the gesture timings stay
	// uniform across screens.
	setButtonHoldTime(BTN_LEFT,  600);
	setButtonHoldTime(BTN_RIGHT, 600);
}

PhoneActiveCall::~PhoneActiveCall() {
	// Cancel the tick timer ahead of LVGL deleting the label so its
	// callback never fires against a freed screen during teardown.
	stopTimerTick();
	// All other children (wallpaper, statusBar, softKeys, avatar,
	// labels) are parented to obj - LVScreen's destructor frees obj
	// and LVGL recursively frees their lv_obj_t backing storage.
}

void PhoneActiveCall::onStart() {
	Input::getInstance()->addListener(this);

	// Capture the wall-clock start at onStart() rather than ctor so a
	// host that builds the screen during a transition still sees a
	// timer that begins at 00:00 the moment the user actually sees it.
	startMs    = millis();
	lastSecond = 0xFFFFFFFFu;
	refreshTimerLabel();
	startTimerTick();
}

void PhoneActiveCall::onStop() {
	stopTimerTick();
	Input::getInstance()->removeListener(this);
}

// ----- builders -----

void PhoneActiveCall::buildCaption() {
	// "IN CALL" caption in pixelbasic7 cyan, just under the status bar.
	// Cyan (rather than the incoming screen's sunset orange) signals
	// that this is the "connected / calm" state of the call rather than
	// the alert state of an incoming ring. Centred horizontally with a
	// 12 px Y offset so it sits cleanly between the 10 px status bar
	// and the avatar.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "IN CALL");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneActiveCall::buildAvatarBlock() {
	// Avatar centred horizontally, pinned at y = 22 (4 px under the
	// caption baseline). The 32x32 avatar fits inside the screen with
	// (160-32)/2 = 64 px slack on each side. No pulsing ring here -
	// PhoneIncomingCall uses one to signal "ringing", and we want this
	// screen to read as visually calmer to mirror the connected state.
	avatar = new PhonePixelAvatar(obj, avatarSeed);
	lv_obj_t* avObj = avatar->getLvObj();
	lv_obj_set_align(avObj, LV_ALIGN_TOP_MID);
	lv_obj_set_y(avObj, 22);
}

void PhoneActiveCall::buildLabels() {
	// Caller name in pixelbasic7 warm cream - smaller than the incoming
	// screen's pixelbasic16 because the focal element on this screen is
	// the timer, not the name. Centred horizontally and capped at 140 px
	// wide with LABEL_LONG_DOT so a long name truncates with an
	// ellipsis instead of pushing the rest of the layout around.
	// y = 22 (avatar top) + 32 (avatar height) + 4 (gap) = 58.
	nameLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nameLabel, MP_TEXT, 0);
	lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(nameLabel, 140);
	lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(nameLabel, "");
	lv_obj_set_align(nameLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(nameLabel, 58);

	// Timer in pixelbasic16 warm cream - the only large glyph on the
	// screen. Centred horizontally with a fixed 70 px Y offset that
	// leaves an 8 px gap between the name baseline and the timer top.
	// The timer text is short (mm:ss = 5 glyphs) so we let it size
	// itself naturally rather than capping width.
	timerLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(timerLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(timerLabel, MP_TEXT, 0);
	lv_label_set_text(timerLabel, "00:00");
	lv_obj_set_align(timerLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(timerLabel, 70);

	// "* MUTED *" indicator in pixelbasic7 sunset orange, hidden by
	// default. Sits below the timer (y = 70 + 16 + 4 = 90) so it has
	// breathing room from both the timer above and the softkey bar
	// below (118). Hidden via LV_OBJ_FLAG_HIDDEN so the layout reads
	// cleanly during normal (un-muted) operation.
	mutedLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(mutedLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(mutedLabel, MP_ACCENT, 0);
	lv_label_set_text(mutedLabel, "* MUTED *");
	lv_obj_set_align(mutedLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(mutedLabel, 92);
	lv_obj_add_flag(mutedLabel, LV_OBJ_FLAG_HIDDEN);
}

// ----- timer tick -----

void PhoneActiveCall::startTimerTick() {
	// Idempotent: if a previous onStart() left a timer running we
	// reuse it rather than stacking a second one. lv_timer_create
	// pins the user_data slot to `this` so the static callback can
	// route back to the correct instance.
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(onTickTimer, TimerTickMs, this);
}

void PhoneActiveCall::stopTimerTick() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneActiveCall::onTickTimer(lv_timer_t* timer) {
	auto self = static_cast<PhoneActiveCall*>(timer->user_data);
	if(self == nullptr) return;
	self->refreshTimerLabel();
}

void PhoneActiveCall::refreshTimerLabel() {
	if(timerLabel == nullptr) return;

	const uint32_t elapsedSec = getDurationSeconds();
	if(elapsedSec == lastSecond) return;
	lastSecond = elapsedSec;

	// Cap visual rollover at 99:59 so the label never spills over its
	// width budget on a long call (a 100-minute MAKERphone call seems
	// unlikely on a peer-to-peer LoRa link, but the cap costs nothing
	// and keeps the layout deterministic).
	uint32_t capped = elapsedSec;
	if(capped > 99u * 60u + 59u) capped = 99u * 60u + 59u;

	const uint32_t minutes = capped / 60u;
	const uint32_t seconds = capped % 60u;

	char buf[8];
	snprintf(buf, sizeof(buf), "%02u:%02u",
			 (unsigned) minutes, (unsigned) seconds);
	lv_label_set_text(timerLabel, buf);
}

uint32_t PhoneActiveCall::getDurationSeconds() const {
	// startMs = 0 means we have not seen onStart() yet (the screen is
	// constructed but not pushed). Return 0 in that case so a host
	// reading getDurationSeconds() before push() does not see a stale
	// reading from the previous instance's timer.
	if(startMs == 0) return 0;
	const uint32_t now = millis();
	// millis() rolls over after ~49 days; in practice a call will not
	// straddle a rollover, but the unsigned subtraction below is
	// rollover-safe so we do not need a special case.
	return (now - startMs) / 1000u;
}

// ----- copy / refresh helpers -----

void PhoneActiveCall::copyName(const char* src) {
	if(src == nullptr || src[0] == '\0') {
		strncpy(callerName, "UNKNOWN", MaxNameLen);
		callerName[MaxNameLen] = '\0';
		return;
	}
	strncpy(callerName, src, MaxNameLen);
	callerName[MaxNameLen] = '\0';
}

void PhoneActiveCall::copyNumber(const char* src) {
	if(src == nullptr) {
		callerNumber[0] = '\0';
		return;
	}
	strncpy(callerNumber, src, MaxNumberLen);
	callerNumber[MaxNumberLen] = '\0';
}

void PhoneActiveCall::refreshNameLabel() {
	if(nameLabel == nullptr) return;
	lv_label_set_text(nameLabel, callerName);
}

void PhoneActiveCall::refreshMuteVisuals() {
	if(softKeys != nullptr) {
		softKeys->setLeft(muted ? "UNMUTE" : "MUTE");
	}
	if(mutedLabel != nullptr) {
		if(muted) {
			lv_obj_clear_flag(mutedLabel, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(mutedLabel, LV_OBJ_FLAG_HIDDEN);
		}
	}
}

// ----- public API -----

void PhoneActiveCall::setOnMute(MuteHandler cb) { muteCb = cb; }
void PhoneActiveCall::setOnEnd(ActionHandler cb) { endCb = cb; }

void PhoneActiveCall::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneActiveCall::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneActiveCall::setCaption(const char* text) {
	if(captionLabel) lv_label_set_text(captionLabel, text != nullptr ? text : "");
}

void PhoneActiveCall::setCallerName(const char* name) {
	copyName(name);
	refreshNameLabel();
}

void PhoneActiveCall::setCallerNumber(const char* number) {
	copyNumber(number);
	// Number not currently rendered (see header notes); copy is kept
	// so a host reading getCallerNumber() observes whatever it set.
}

void PhoneActiveCall::setAvatarSeed(uint8_t seed) {
	avatarSeed = seed;
	if(avatar) avatar->setSeed(seed);
}

void PhoneActiveCall::setMuted(bool m) {
	if(muted == m) return;
	muted = m;
	refreshMuteVisuals();
}

// ----- action dispatch -----

void PhoneActiveCall::fireMuteToggle() {
	// Visual click first (so a slow handler still gives the user
	// feedback), then flip the local mute state, refresh the label /
	// indicator, and finally notify the host. We notify *after* the
	// visual update so a host that wants to read isMuted() inside the
	// callback observes the new state.
	if(softKeys) softKeys->flashLeft();
	muted = !muted;
	refreshMuteVisuals();
	if(muteCb) muteCb(this, muted);
}

void PhoneActiveCall::fireEnd() {
	// END softkey: flash-then-handler pattern matches PhoneIncomingCall
	// and the dialer. The default fall-through is pop() so a host that
	// just wanted the visual gets sensible behaviour from BTN_BACK
	// alone, and the screen never feels stuck.
	if(softKeys) softKeys->flashRight();
	if(endCb) {
		endCb(this);
	} else {
		pop();
	}
}

// ----- input -----

void PhoneActiveCall::buttonPressed(uint i) {
	switch(i) {
		// MUTE softkey on the left button. Defer the actual handler
		// dispatch to buttonReleased so a long-press that was meant to
		// be cancelled mid-hold (we don't have one wired today, but
		// matches the dialer's BTN_RIGHT pattern for consistency) does
		// not double-fire on key release.
		case BTN_LEFT:
			if(softKeys) softKeys->flashLeft();
			muteLongFired = false;
			break;

		// BTN_ENTER (centre A button) is a friendly second way to
		// toggle mute - matches the way PhoneIncomingCall accepts
		// BTN_ENTER as a second ANSWER path. Fired immediately here
		// (rather than on release) since A is a single-action "tap to
		// confirm" key with no long-press behaviour wired.
		case BTN_ENTER:
			fireMuteToggle();
			break;

		// END softkey on the right button. Same defer-to-release
		// pattern as MUTE.
		case BTN_RIGHT:
			if(softKeys) softKeys->flashRight();
			endLongFired = false;
			break;

		// BTN_BACK is the standard "get me out of here" hardware key
		// on every Chatter screen and feels natural as a second END
		// path. Fired on press so the user gets immediate hang-up
		// without waiting for the release event - a held BTN_BACK
		// during a call is unambiguously "kill the call".
		case BTN_BACK:
			fireEnd();
			break;

		default:
			break;
	}
}

void PhoneActiveCall::buttonReleased(uint i) {
	switch(i) {
		case BTN_LEFT:
			// Short press toggles mute. Long-press path is currently
			// the same effect, but we still gate on the suppression
			// flag so the matching short-press does not double-fire
			// on key release.
			if(!muteLongFired) {
				fireMuteToggle();
			}
			muteLongFired = false;
			break;

		case BTN_RIGHT:
			// Short press ends the call. Same suppression-flag pattern
			// as MUTE for symmetry.
			if(!endLongFired) {
				fireEnd();
			}
			endLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneActiveCall::buttonHeld(uint i) {
	switch(i) {
		// Holding MUTE is treated as a short-press toggle for now (no
		// distinct long-press semantics yet), but we set the
		// suppression flag and flash the softkey so the release
		// handler does not double-fire. Leaves room for a future
		// "long-press to enable speaker" gesture without rewriting the
		// dispatch.
		case BTN_LEFT:
			muteLongFired = true;
			fireMuteToggle();
			break;

		// Holding END is treated as a forced hang-up (handy if the
		// release event never arrives because the user keeps pressing).
		// Same suppression-flag pattern as MUTE.
		case BTN_RIGHT:
			endLongFired = true;
			fireEnd();
			break;

		default:
			break;
	}
}
