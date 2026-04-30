#include "PhoneCallEnded.h"

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
// codebase (see PhoneActiveCall.cpp / PhoneIncomingCall.cpp /
// PhoneAppStubScreen.cpp). The call-ended screen uses sunset orange for
// the caption ("alert / state change" reading) so it visually echoes the
// REJECT softkey of the incoming-call screen and signals to the user
// that the call is *over*. The duration glyph stays warm cream so it
// reads as the focal element rather than a status indicator. Dim purple
// is used for the "press any key" hint so it does not compete with the
// duration above.
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (caption)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan (unused but kept for symmetry)
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (name + duration)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple ("press any key")

PhoneCallEnded::PhoneCallEnded(const char* name,
							   uint32_t    seconds,
							   uint8_t     seed)
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  avatar(nullptr),
		  captionLabel(nullptr),
		  nameLabel(nullptr),
		  durationLabel(nullptr),
		  hintLabel(nullptr),
		  avatarSeed(seed),
		  durationSeconds(seconds) {

	// Zero the name buffer up front so getCallerName returns a valid
	// c-string before copyName runs (defensive against an early access
	// from a caller's setOnDismiss).
	callerName[0] = '\0';

	// Full-screen container, no scrollbars, no inner padding - same
	// blank-canvas pattern as every other Phone* screen. Children below
	// are anchored manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. Every
	// other element overlays it without any opacity gymnastics on the
	// parent. Same z-order pattern as PhoneActiveCall / PhoneIncomingCall.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall) so the user
	// keeps their device-state context even on a transient overlay.
	statusBar = new PhoneStatusBar(obj);

	// Caption + avatar block + name/duration/hint labels, each their
	// own helper to keep the ctor readable.
	buildCaption();
	buildAvatarBlock();
	buildLabels();

	// Copy the caller info now so the on-screen labels reflect the
	// values passed in. copyName bounds the destination buffer via
	// MaxNameLen so a long string truncates cleanly rather than blowing
	// the per-screen allocation.
	copyName(name);
	refreshNameLabel();
	refreshDurationLabel();

	// Bottom: feature-phone soft-keys. Only the right "HOME" label is
	// meaningful here (any key dismisses, but HOME is the explicit
	// "send me back" affordance). The left side stays blank so we do
	// not invite the user to press a key with no specific action.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("HOME");
}

PhoneCallEnded::~PhoneCallEnded() {
	// Cancel the dismiss timer ahead of LVGL teardown so its callback
	// never fires against a freed screen during destruction.
	stopDismissTimer();
	// All other children (wallpaper, statusBar, softKeys, avatar,
	// labels) are parented to obj - LVScreen's destructor frees obj
	// and LVGL recursively frees their lv_obj_t backing storage.
}

void PhoneCallEnded::onStart() {
	Input::getInstance()->addListener(this);

	// Reset the dismissed-already flag so a screen reused after a pop
	// (unlikely on this overlay, but cheap to support) starts fresh.
	dismissedAlready = false;
	startDismissTimer();
}

void PhoneCallEnded::onStop() {
	stopDismissTimer();
	Input::getInstance()->removeListener(this);
}

// ----- builders -----

void PhoneCallEnded::buildCaption() {
	// "CALL ENDED" caption in pixelbasic7 sunset orange, just under the
	// status bar. Orange (rather than the active-call screen's cyan)
	// signals "state change / alert" and visually rhymes with the REJECT
	// softkey on PhoneIncomingCall. Centred horizontally with a 12 px Y
	// offset so it sits cleanly between the 10 px status bar and the
	// avatar, matching PhoneActiveCall's caption position.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_ACCENT, 0);
	lv_label_set_text(captionLabel, "CALL ENDED");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneCallEnded::buildAvatarBlock() {
	// Avatar centred horizontally, pinned at y = 22 (4 px under the
	// caption baseline) - same anchor as PhoneActiveCall so the avatar
	// visually stays put across the active -> ended transition. No
	// pulsing ring (the call is over, the screen should read calm).
	avatar = new PhonePixelAvatar(obj, avatarSeed);
	lv_obj_t* avObj = avatar->getLvObj();
	lv_obj_set_align(avObj, LV_ALIGN_TOP_MID);
	lv_obj_set_y(avObj, 22);
}

void PhoneCallEnded::buildLabels() {
	// Caller name in pixelbasic7 warm cream, mirrors PhoneActiveCall's
	// name placement (y = 58) so the avatar -> name pair stays anchored
	// across the call-flow handoff. LABEL_LONG_DOT + 140 px width cap
	// truncates a long name with an ellipsis instead of pushing the
	// rest of the layout around.
	nameLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nameLabel, MP_TEXT, 0);
	lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(nameLabel, 140);
	lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(nameLabel, "");
	lv_obj_set_align(nameLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(nameLabel, 58);

	// Duration in pixelbasic16 warm cream - the only large glyph on the
	// screen, so the eye locks onto it just like the live timer on
	// PhoneActiveCall. Same y = 70 anchor as the active-call timer so
	// the visual handoff is seamless. The text reads as a noun ("0m 42s")
	// rather than a clock ("00:42") to signal that this is a finished
	// length rather than a live reading.
	durationLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(durationLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(durationLabel, MP_TEXT, 0);
	lv_label_set_text(durationLabel, "0m 0s");
	lv_obj_set_align(durationLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(durationLabel, 70);

	// "press any key" hint in pixelbasic7 dim purple. Sits below the
	// duration (y = 70 + 16 + 4 = 90, then nudged to 96 to give the
	// duration breathing room) and tells the user the overlay can be
	// short-circuited. Subtle so it does not compete with the focal
	// duration glyph above.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "press any key");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 96);
}

// ----- copy / refresh helpers -----

void PhoneCallEnded::copyName(const char* src) {
	if(src == nullptr || src[0] == '\0') {
		strncpy(callerName, "UNKNOWN", MaxNameLen);
		callerName[MaxNameLen] = '\0';
		return;
	}
	strncpy(callerName, src, MaxNameLen);
	callerName[MaxNameLen] = '\0';
}

void PhoneCallEnded::refreshNameLabel() {
	if(nameLabel == nullptr) return;
	lv_label_set_text(nameLabel, callerName);
}

void PhoneCallEnded::refreshDurationLabel() {
	if(durationLabel == nullptr) return;

	// Cap visual rollover at 99m 59s so the label never spills over its
	// width budget on an absurdly long call (a 100-minute MAKERphone
	// call is unlikely on a peer-to-peer LoRa link, but the cap costs
	// nothing and keeps the layout deterministic).
	uint32_t capped = durationSeconds;
	if(capped > 99u * 60u + 59u) capped = 99u * 60u + 59u;

	const uint32_t minutes = capped / 60u;
	const uint32_t seconds = capped % 60u;

	// "Xm Ys" reads as a finished length rather than a clock. We keep
	// seconds zero-padded ("0m 02s" looks more deliberate than "0m 2s")
	// but minutes are not - "0m" / "1m" / "12m" all read naturally.
	char buf[16];
	snprintf(buf, sizeof(buf), "%um %02us",
			 (unsigned) minutes, (unsigned) seconds);
	lv_label_set_text(durationLabel, buf);
}

// ----- public API -----

void PhoneCallEnded::setOnDismiss(ActionHandler cb) { dismissCb = cb; }

void PhoneCallEnded::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneCallEnded::setCaption(const char* text) {
	if(captionLabel) lv_label_set_text(captionLabel, text != nullptr ? text : "");
}

void PhoneCallEnded::setCallerName(const char* name) {
	copyName(name);
	refreshNameLabel();
}

void PhoneCallEnded::setAvatarSeed(uint8_t seed) {
	avatarSeed = seed;
	if(avatar) avatar->setSeed(seed);
}

void PhoneCallEnded::setDurationSeconds(uint32_t seconds) {
	durationSeconds = seconds;
	refreshDurationLabel();
}

void PhoneCallEnded::setAutoDismissMs(uint32_t ms) {
	autoDismissMs = ms;
	// If the screen is already running and the timer is active,
	// restart it so the new delay takes effect immediately. A 0 ms
	// value disables the timer (any-key dismiss only).
	if(dismissTimer != nullptr) {
		stopDismissTimer();
		startDismissTimer();
	}
}

// ----- timer + dismiss dispatch -----

void PhoneCallEnded::startDismissTimer() {
	// Idempotent: if a previous onStart() left a timer running we
	// reuse it rather than stacking a second one. A 0 ms delay
	// disables the auto-dismiss entirely (host opt-in).
	if(dismissTimer != nullptr) return;
	if(autoDismissMs == 0)      return;
	dismissTimer = lv_timer_create(onDismissTimer, autoDismissMs, this);
}

void PhoneCallEnded::stopDismissTimer() {
	if(dismissTimer == nullptr) return;
	lv_timer_del(dismissTimer);
	dismissTimer = nullptr;
}

void PhoneCallEnded::onDismissTimer(lv_timer_t* timer) {
	auto self = static_cast<PhoneCallEnded*>(timer->user_data);
	if(self == nullptr) return;
	self->fireDismiss();
}

void PhoneCallEnded::fireDismiss() {
	// Guard against double-fire: a hardware key press and the auto-
	// dismiss timer can race within a single LVGL tick, and pop()-ing
	// twice from the same screen tears down the parent's state.
	if(dismissedAlready) return;
	dismissedAlready = true;

	// Stop the timer up-front so a slow handler does not let it
	// re-enter fireDismiss via the static callback.
	stopDismissTimer();

	if(softKeys) softKeys->flashRight();

	// Default fall-through is pop() so a host that just wanted the
	// visual still gets sensible behaviour - the screen returns to
	// whoever pushed it (typically PhoneHomeScreen via the active-
	// call screen's end callback).
	if(dismissCb) {
		dismissCb(this);
	} else {
		pop();
	}
}

// ----- input -----

void PhoneCallEnded::buttonPressed(uint i) {
	// Any hardware key short-circuits the auto-dismiss. The screen
	// is a transient overlay so we deliberately do not differentiate
	// keys here - the user pressing anything signals "I'm done with
	// this overlay" and we honour it. A future extension could route
	// BTN_LEFT to "redial" once PhoneCallHistory (S27) lands; for now
	// every key collapses to the same dismiss path so the overlay
	// never feels stuck.
	(void) i;
	fireDismiss();
}
