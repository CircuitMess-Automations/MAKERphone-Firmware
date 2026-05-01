#include "PhoneBatteryLowModal.h"

#include <Battery/BatteryService.h>
#include <Notes.h>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneBatteryIcon.h"
#include "../Fonts/font.h"
#include "../Services/PhoneRingtoneEngine.h"

// MAKERphone retro palette - inlined per the established pattern in
// every other Phone* widget / screen so the modal reads as a member of
// the same visual family as PhoneStatusBar, PhoneSoftKeyBar,
// PhoneBatteryIcon, PhoneSynthwaveBg and friends.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple slab fill
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange (caption + border)
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream (percent)
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple ("Charge soon")

// ----- Static "Charge me" chirp melody -----
//
// 3-note descending arpeggio. C5 -> A4 -> E4 reads as a polite "uh-oh"
// rather than a panicked alarm; the inter-note gap matches the
// PhoneBatteryIcon::PulseIntervalMs cadence so the audio + visual cues
// land on the same beat. Total runtime ~ 3 * 240 + 2 * 80 = 880 ms,
// which fits comfortably inside the 4 s auto-dismiss window. Lives in
// flash because the array is `static const`.
static const PhoneRingtoneEngine::Note ChirpNotes[] = {
		{ NOTE_C5, 240 },
		{ NOTE_A4, 240 },
		{ NOTE_E4, 240 },
};

static const PhoneRingtoneEngine::Melody ChirpMelody = {
		ChirpNotes,
		sizeof(ChirpNotes) / sizeof(ChirpNotes[0]),
		80,         // gapMs
		false,      // do not loop - we want a single "chirp", not a siren
		"BattLow",
};

// ----- ctor / dtor -----

PhoneBatteryLowModal::PhoneBatteryLowModal(LVScreen* parent, uint8_t percentArg)
		: LVModal(parent) {

	// Default percent reads the live value at construction so the modal
	// always shows what the user has *now* (rather than a value passed
	// through ages ago). The Battery service is a global so this is
	// always safe to call - it just returns 0 if begin() never ran.
	if(percentArg == 0xff){
		percent = Battery.getPercentage();
	}else{
		percent = percentArg;
	}

	buildSlab();
	buildContent();

	// Catch any key press as "dismiss". Same pattern as the legacy
	// BatteryNotification: bind a KEY + PRESSED handler to the modal
	// container after adding it to the input group, so LVGL will
	// route hardware keys through the focus path the moment LVModal
	// swaps the indev group on start().
	lv_group_add_obj(inputGroup, obj);
	lv_obj_add_event_cb(obj, &PhoneBatteryLowModal::onKeyEvent, LV_EVENT_KEY,     this);
	lv_obj_add_event_cb(obj, &PhoneBatteryLowModal::onKeyEvent, LV_EVENT_PRESSED, this);
}

PhoneBatteryLowModal::~PhoneBatteryLowModal() {
	// Cancel timers up-front so they cannot fire against a freed
	// instance during destruction. Order matters: the dismiss timer
	// chains into the self-destroy timer, so kill the dismiss timer
	// first, then the self-destroy.
	stopDismissTimer();
	if(selfDestroyTimer != nullptr){
		lv_timer_del(selfDestroyTimer);
		selfDestroyTimer = nullptr;
	}
	// `batteryIcon` is parented to `obj` via PhoneBatteryIcon's
	// constructor, so LVGL's recursive child cleanup tears it down
	// when LVModal destroys `obj`. We `delete` the C++ wrapper here
	// so its LoopListener subscription drops cleanly.
	delete batteryIcon;
	batteryIcon = nullptr;
}

// ----- builders -----

void PhoneBatteryLowModal::buildSlab() {
	// Slab itself: 130 x 64 deep-purple panel, 1 px MP_ACCENT border,
	// 4 px rounded corners. Centring is handled by LVModal's container
	// (LV_ALIGN_CENTER on the floating wrapper) so we don't need to
	// position obj manually.
	lv_obj_set_size(obj, ModalWidth, ModalHeight);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_radius(obj, 4, 0);
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_color(obj, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
	// Disable the focus outline that the chatter theme normally adds
	// so the modal stays visually clean while the input group is
	// focused on it - the orange border is already the focus signal.
	lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED);
}

void PhoneBatteryLowModal::buildContent() {
	// "BATTERY LOW" caption: pixelbasic7, sunset orange. Pinned 6 px
	// from the top so the slab feels like a shallow header band.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_ACCENT, 0);
	lv_label_set_text(captionLabel, "BATTERY LOW");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 6);

	// Animated battery icon. PhoneBatteryIcon already pulses red when
	// `setLevel(0)` is called and auto-update is off, which is exactly
	// the behaviour we want for a low-battery nag. Manual placement:
	// 16 px wide icon at x = 30, vertical centre 30 px down (slab is
	// 64 tall, icon is 9 tall, header is 6 px top + ~9 px caption =
	// caption baseline ~21; centre the icon at y = 30 so there's a
	// clear gap between it and the caption above + the hint below).
	batteryIcon = new PhoneBatteryIcon(obj);
	{
		lv_obj_t* iconObj = batteryIcon->getLvObj();
		lv_obj_set_pos(iconObj, 30, 30);
	}
	batteryIcon->setLevel(0);          // disables auto-update + arms low-battery pulse

	// Percent readout in pixelbasic16 warm cream. Reads as the focal
	// element - the user's eye lands on the percentage first, then
	// drifts up to the caption + down to the hint. Aligned right of
	// the battery icon with a comfortable gutter (icon ends at x=46,
	// percent left edge at x=58 leaves a 12 px gap that mirrors the
	// caption-to-icon spacing above).
	percentLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(percentLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(percentLabel, MP_TEXT, 0);
	lv_obj_set_pos(percentLabel, 58, 22);
	refreshPercentLabel();

	// "Charge soon" hint in pixelbasic7 dim purple, near the bottom
	// of the slab. Subtle so it does not compete with the percent.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "Charge soon");
	lv_obj_set_align(hintLabel, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(hintLabel, -6);
}

void PhoneBatteryLowModal::refreshPercentLabel() {
	if(percentLabel == nullptr) return;
	// Cap to 99 so a freshly-booted reading of 100 (above the warning
	// threshold but possible if the modal is opened manually for QA)
	// still fits the layout without spilling.
	uint8_t shown = percent;
	if(shown > 99) shown = 99;
	char buf[8];
	snprintf(buf, sizeof(buf), "%u%%", (unsigned) shown);
	lv_label_set_text(percentLabel, buf);
}

// ----- public API -----

void PhoneBatteryLowModal::setPercent(uint8_t newPercent) {
	percent = newPercent;
	refreshPercentLabel();
}

void PhoneBatteryLowModal::setAutoDismissMs(uint32_t ms) {
	autoDismissMs = ms;
	if(dismissTimer != nullptr) {
		// Restart the timer with the new period so a dynamic override
		// (e.g. QA wants a longer modal for screenshots) takes effect
		// immediately.
		stopDismissTimer();
		startDismissTimer();
	}
}

// ----- lifecycle (LVModal hooks) -----

void PhoneBatteryLowModal::onStart() {
	// Reset the once-only flag on every (re)start so a host that reuses
	// the same instance for a second nag still gets a full lifecycle.
	dismissed = false;

	// Focus the modal so the input group routes hardware keys through
	// our LV_EVENT_KEY / LV_EVENT_PRESSED callbacks. Editing mode is
	// what makes the encoder-style input forward LV_KEY_* events into
	// the focused obj (matching the legacy BatteryNotification).
	lv_group_focus_obj(obj);
	lv_group_set_editing(inputGroup, true);

	// Audio chirp via the global ringtone engine. The engine respects
	// Settings.sound internally, so a muted device stays silent without
	// us having to ask. Replacing whatever was playing is fine - the
	// battery warning is, by definition, urgent enough to interrupt a
	// background ringtone preview, and the engine restarts cleanly on
	// the next call after we stop().
	Ringtone.play(ChirpMelody);

	startDismissTimer();
}

void PhoneBatteryLowModal::onStop() {
	stopDismissTimer();
	// Silence the chirp the moment the modal is dismissed - a slow
	// melody outlasting a quick dismiss feels broken.
	Ringtone.stop();

	// Hand the editing flag back so the parent screen's focus model
	// is restored. LVModal::stop() already swaps the indev group back
	// to the parent, but it does not clear editing mode on the modal's
	// own group, and a leftover edit flag would surprise the next
	// modal in the stack.
	lv_group_set_editing(inputGroup, false);

	// Self-destroy after a short delay. We can't `delete this` from
	// inside onStop() because LVModal::stop() is still on the call
	// stack; defer it to a one-shot lv_timer fired on the next tick.
	scheduleSelfDestroy();
}

// ----- dismiss / timer plumbing -----

void PhoneBatteryLowModal::startDismissTimer() {
	if(dismissTimer != nullptr) return;
	if(autoDismissMs == 0)      return;
	dismissTimer = lv_timer_create(onDismissTimer, autoDismissMs, this);
}

void PhoneBatteryLowModal::stopDismissTimer() {
	if(dismissTimer == nullptr) return;
	lv_timer_del(dismissTimer);
	dismissTimer = nullptr;
}

void PhoneBatteryLowModal::fireDismiss() {
	// Guard against double-fire: a hardware key press and the auto-
	// dismiss timer can race within a single LVGL tick.
	if(dismissed) return;
	dismissed = true;

	stopDismissTimer();
	stop();   // LVModal::stop() will call our onStop()
}

void PhoneBatteryLowModal::scheduleSelfDestroy() {
	if(selfDestroyTimer != nullptr) return;
	// 80 ms gives LVGL one redraw cycle (the modal is HIDDEN by
	// LVModal::stop() before we delete) so the slab cleanly fades
	// to "gone" before the obj tree underneath is torn down.
	selfDestroyTimer = lv_timer_create(onSelfDestroyTimer, 80, this);
	lv_timer_set_repeat_count(selfDestroyTimer, 1);
}

// ----- static dispatchers -----

void PhoneBatteryLowModal::onDismissTimer(lv_timer_t* timer) {
	auto* self = static_cast<PhoneBatteryLowModal*>(timer->user_data);
	if(self == nullptr) return;
	self->fireDismiss();
}

void PhoneBatteryLowModal::onSelfDestroyTimer(lv_timer_t* timer) {
	auto* self = static_cast<PhoneBatteryLowModal*>(timer->user_data);
	if(self == nullptr) return;
	// Null the pointer in the instance before deleting so the dtor
	// does not try to lv_timer_del() the timer that is currently
	// firing - LVGL is not re-entrant on its own timer list.
	self->selfDestroyTimer = nullptr;
	delete self;
}

void PhoneBatteryLowModal::onKeyEvent(lv_event_t* event) {
	auto* self = static_cast<PhoneBatteryLowModal*>(lv_event_get_user_data(event));
	if(self == nullptr || !self->isActive()) return;
	self->fireDismiss();
}
