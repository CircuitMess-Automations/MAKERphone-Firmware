#include "PhoneIncomingCall.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhonePixelAvatar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneRingtoneLibrary.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneMainMenu.cpp / PhoneHomeScreen.cpp / PhoneDialerScreen.cpp /
// PhoneAppStubScreen.cpp). We keep the caller name in warm cream text and
// the caption / number in dim purple so the visual hierarchy of the screen
// reads as: caller name (focal), caption + number (secondary), avatar
// (anchor on the left of the gaze-line). The ANSWER softkey reuses the
// classic feature-phone "green = pick up" cue via lime green; REJECT keeps
// the standard MP_ACCENT sunset orange because we do not ship a true red
// in the MAKERphone palette and a stop-light red would clash with the
// rest of the synthwave theme.
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (REJECT)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple
#define MP_ANSWER_GREEN lv_color_make(120, 220, 100)   // lime green (ANSWER)

// Pulse-ring animation cadence. A 900 ms half-period gives the ring a
// slow phone-ringing breath (matches the cadence of an actual ringtone
// LFO) without competing visually with the menu-tile halo on a screen
// the user might transition back to.
static constexpr uint32_t kRingPulsePeriodMs = 900;

// Forward-decl of the static animation exec callback so the ctor can
// reference it without an explicit cast at the call site.
static void ringPulseExec(void* var, int32_t v);

PhoneIncomingCall::PhoneIncomingCall(const char* name,
									 const char* number,
									 uint8_t seed)
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  avatar(nullptr),
		  captionLabel(nullptr),
		  nameLabel(nullptr),
		  numberLabel(nullptr),
		  ring(nullptr),
		  avatarSeed(seed) {

	// Zero the name / number buffers up front so getCallerName /
	// getCallerNumber return valid c-strings before copyName / copyNumber
	// runs (defensive against an early access from a caller's setOn*).
	callerName[0]   = '\0';
	callerNumber[0] = '\0';

	// Full-screen container, no scrollbars, no inner padding - same
	// blank-canvas pattern as every other Phone* screen. Children below
	// are anchored manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. Every
	// other element overlays it without any opacity gymnastics on the
	// parent. Same z-order pattern as PhoneHomeScreen / PhoneDialerScreen.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall) so the user
	// never loses the device-state context even mid-call.
	statusBar = new PhoneStatusBar(obj);

	// Caption + avatar block + name/number labels, each their own helper
	// to keep the ctor readable.
	buildCaption();
	buildAvatarBlock();
	buildLabels();

	// Copy the caller info now so the on-screen labels reflect the values
	// passed in. copyName / copyNumber both bound the destination buffer
	// via MaxNameLen / MaxNumberLen so a long string truncates cleanly
	// rather than blowing the per-screen allocation.
	copyName(name);
	copyNumber(number);
	refreshNameLabel();
	refreshNumberLabel();

	// Bottom: feature-phone soft-keys. ANSWER on the left (BTN_LEFT) and
	// REJECT on the right (BTN_RIGHT) match the established Phase-D
	// orientation that PhoneDialerScreen set up (CALL on the left).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("ANSWER");
	softKeys->setRight("REJECT");

	// Long-press detection on both BTN_LEFT and BTN_RIGHT so a stuck
	// finger does not double-fire on key release. Same 600 ms threshold
	// as the rest of the MAKERphone shell, so the gesture timings stay
	// uniform across screens.
	setButtonHoldTime(BTN_LEFT,  600);
	setButtonHoldTime(BTN_RIGHT, 600);

	// Kick off the pulsing ring animation. Doing this last (after the
	// ring is built and the screen layout is settled) keeps the very
	// first frame from showing a half-painted ring while LVGL is still
	// laying out the rest of the children.
	startRingAnimation();
}

PhoneIncomingCall::~PhoneIncomingCall() {
	// Cancel the ring animation ahead of LVGL deleting `ring` so the
	// exec callback never fires against a freed object during teardown.
	// `ring` is parented to obj so LVGL frees it recursively right
	// after; explicit anim cancel order is the only thing that matters.
	if(ring != nullptr) {
		lv_anim_del(ring, ringPulseExec);
	}
	// Defensive: hush the piezo if the screen is being
	// torn down without a prior onStop() (rare, but a
	// host that drops the unique_ptr without popping
	// could trigger this). stopRingtone() is a no-op
	// when ringtoneActive is already false.
	stopRingtone();
	// All other children (wallpaper, statusBar, softKeys, avatar,
	// labels) are parented to obj - LVScreen's destructor frees obj
	// and LVGL recursively frees their lv_obj_t backing storage.
}

void PhoneIncomingCall::onStart() {
	Input::getInstance()->addListener(this);
	// S41: start ringing as soon as the screen takes over. We
	// always start fresh in onStart() so re-entering the screen
	// (e.g. dismissing a modal that suspended us) restarts the
	// melody from the top — matches feature-phone behaviour.
	startRingtone();
}

void PhoneIncomingCall::onStop() {
	// Hush the piezo before we hand input back to whoever is
	// next on the screen stack. stopRingtone() is idempotent
	// and only acts if THIS screen is currently the engine's
	// driver, so a Settings-driven mute that already silenced
	// playback does not double-stop the next caller.
	stopRingtone();
	Input::getInstance()->removeListener(this);
}

// ----- builders -----

void PhoneIncomingCall::buildCaption() {
	// "INCOMING CALL" caption in pixelbasic7 sunset orange directly under
	// the status bar. Sunset orange (rather than dim purple) because the
	// caption is the *event headline* - we want the user's eye to land
	// on it the moment the screen appears. Centred horizontally with a
	// 12 px Y offset so it sits cleanly between the 10 px status bar
	// and the avatar.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_ACCENT, 0);
	lv_label_set_text(captionLabel, "INCOMING CALL");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneIncomingCall::buildAvatarBlock() {
	// Avatar centred horizontally and pinned at y = 24, leaving room
	// for the caption above and the name/number block below. The 32x32
	// avatar fits inside the screen with comfortable padding (160-32)/2
	// = 64 px on each side.
	avatar = new PhonePixelAvatar(obj, avatarSeed);
	lv_obj_t* avObj = avatar->getLvObj();
	lv_obj_set_align(avObj, LV_ALIGN_TOP_MID);
	lv_obj_set_y(avObj, 24);

	// Pulsing ring around the avatar. 40x40 (avatar + 4 px on each side)
	// rounded to a circle, transparent fill, animated MP_ACCENT border.
	// Built as a sibling of the avatar (not a child) so the ring's
	// border can extend past the avatar's bounds without LVGL clipping
	// it to the avatar's 32x32 frame. We move the ring to the
	// background so it sits *behind* the avatar even though it is added
	// after - matches the PhoneIconTile halo pattern.
	ring = lv_obj_create(obj);
	lv_obj_remove_style_all(ring);
	lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(ring, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(ring, PhonePixelAvatar::AvatarSize + 8,
						   PhonePixelAvatar::AvatarSize + 8);
	lv_obj_set_align(ring, LV_ALIGN_TOP_MID);
	lv_obj_set_y(ring, 24 - 4);
	lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(ring, MP_ACCENT, 0);
	lv_obj_set_style_border_width(ring, 2, 0);
	lv_obj_set_style_border_opa(ring, LV_OPA_30, 0);
	lv_obj_move_background(ring);
}

void PhoneIncomingCall::buildLabels() {
	// Caller display name in pixelbasic16 warm cream text. Centred
	// horizontally and capped to 140 px wide with LABEL_LONG_DOT so a
	// long name truncates with an ellipsis instead of pushing the
	// rest of the layout around. Sits at y = 64 - 12 px under the 32 px
	// avatar (which itself starts at y = 24, so it ends at y = 56). The
	// 8 px gap between the avatar and the name reads as a clear
	// vertical break, not crowded.
	nameLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(nameLabel, MP_TEXT, 0);
	lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(nameLabel, 140);
	lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(nameLabel, "");
	lv_obj_set_align(nameLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(nameLabel, 64);

	// Caller number in pixelbasic7 dim purple - the secondary line. Uses
	// LABEL_LONG_DOT with the same 140 px cap; hidden when the buffer
	// is empty so a contact-only call (no number) renders cleanly with
	// just the name and avatar.
	numberLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(numberLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(numberLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(numberLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(numberLabel, 140);
	lv_obj_set_style_text_align(numberLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(numberLabel, "");
	lv_obj_set_align(numberLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(numberLabel, 84);
	lv_obj_add_flag(numberLabel, LV_OBJ_FLAG_HIDDEN);
}

void PhoneIncomingCall::startRingAnimation() {
	if(ring == nullptr) return;

	// Border opacity ping-pongs between LV_OPA_20 and LV_OPA_90 so the
	// ring breathes around the avatar. Using border_opa rather than the
	// object's overall opacity keeps the avatar itself stable - only
	// the halo pulses, not the face inside it. Same exec-callback
	// pattern as the PhoneIconTile halo pulse so behaviour matches what
	// the rest of the firmware already does for halo effects.
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, ring);
	lv_anim_set_values(&a, LV_OPA_20, LV_OPA_90);
	lv_anim_set_time(&a, kRingPulsePeriodMs);
	lv_anim_set_playback_time(&a, kRingPulsePeriodMs);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_exec_cb(&a, ringPulseExec);
	lv_anim_start(&a);
}

// Static animation exec callback - lives at file scope so we have a
// stable function pointer to hand both lv_anim_set_exec_cb and
// lv_anim_del during teardown. (Member-function pointers cannot be
// passed directly to LVGL's C-style anim API.)
static void ringPulseExec(void* var, int32_t v) {
	auto target = static_cast<lv_obj_t*>(var);
	lv_obj_set_style_border_opa(target, (lv_opa_t) v, 0);
}

// ----- copy / refresh helpers -----

void PhoneIncomingCall::copyName(const char* src) {
	if(src == nullptr || src[0] == '\0') {
		// Fall back to a friendly placeholder so we never render an
		// empty name (which would visually dock the avatar to the
		// number line and look broken).
		strncpy(callerName, "UNKNOWN", MaxNameLen);
		callerName[MaxNameLen] = '\0';
		return;
	}
	strncpy(callerName, src, MaxNameLen);
	callerName[MaxNameLen] = '\0';
}

void PhoneIncomingCall::copyNumber(const char* src) {
	if(src == nullptr) {
		callerNumber[0] = '\0';
		return;
	}
	strncpy(callerNumber, src, MaxNumberLen);
	callerNumber[MaxNumberLen] = '\0';
}

void PhoneIncomingCall::refreshNameLabel() {
	if(nameLabel == nullptr) return;
	lv_label_set_text(nameLabel, callerName);
}

void PhoneIncomingCall::refreshNumberLabel() {
	if(numberLabel == nullptr) return;
	if(callerNumber[0] == '\0') {
		lv_label_set_text(numberLabel, "");
		lv_obj_add_flag(numberLabel, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_label_set_text(numberLabel, callerNumber);
		lv_obj_clear_flag(numberLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

// ----- public API -----

void PhoneIncomingCall::setOnAnswer(ActionHandler cb) { answerCb = cb; }
void PhoneIncomingCall::setOnReject(ActionHandler cb) { rejectCb = cb; }

void PhoneIncomingCall::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneIncomingCall::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneIncomingCall::setCaption(const char* text) {
	if(captionLabel) lv_label_set_text(captionLabel, text != nullptr ? text : "");
}

void PhoneIncomingCall::setCallerName(const char* name) {
	copyName(name);
	refreshNameLabel();
}

void PhoneIncomingCall::setCallerNumber(const char* number) {
	copyNumber(number);
	refreshNumberLabel();
}

void PhoneIncomingCall::setAvatarSeed(uint8_t seed) {
	avatarSeed = seed;
	if(avatar) avatar->setSeed(seed);
}

// ----- ringtone (S41) -----

// S41 wires the existing PhoneRingtoneEngine (S39) and the five
// default melodies (S40) into the incoming-call screen so the user
// finally hears their phone ring. Selection rule:
//   1. setRingtone() override (if the host wired one)
//   2. PhoneRingtoneLibrary::Synthwave as the default — picked
//      because it loops cleanly, fits the synthwave palette, and
//      its A-minor arpeggio is recognisable but not annoying when
//      it repeats while the call is unanswered.
// The selection is resolved lazily in startRingtone() rather than
// in the constructor so a host that calls setRingtone() between
// `new PhoneIncomingCall(...)` and the screen actually starting can
// override the default without paying for an immediate first-melody
// fetch.

void PhoneIncomingCall::setRingtone(const PhoneRingtoneEngine::Melody* melody) {
	ringtone = melody;
	// If the screen is already audible, switch melodies live so the
	// caller can preview their pick without leaving the screen. We
	// only retune if WE were the engine driver — otherwise some
	// other component owns the piezo and we would clobber its sound.
	if(ringtoneActive && ringtoneEnabled) {
		if(melody != nullptr) {
			Ringtone.play(*melody);
		} else {
			Ringtone.stop();
		}
	}
}

void PhoneIncomingCall::setRingtoneEnabled(bool enabled) {
	if(ringtoneEnabled == enabled) return;
	ringtoneEnabled = enabled;
	if(!ringtoneEnabled) {
		// Disabling silences immediately, but we still consider the
		// screen the active driver until onStop() — so re-enabling
		// before the screen pops resumes ringing.
		if(ringtoneActive) {
			Ringtone.stop();
		}
	} else if(ringtoneActive) {
		// Re-enable mid-screen — restart the melody from the top
		// so the user hears it again even if Ringtone.stop() above
		// already drained the engine state.
		startRingtone();
	}
	// If we were never active (screen not yet onStart()-ed), the
	// flag is sufficient — the next onStart() will pick it up.
}

void PhoneIncomingCall::startRingtone() {
	if(!ringtoneEnabled) return;

	// Lazy default — first time the screen actually rings, fall back
	// to the Synthwave ringtone unless a host overrode it. The
	// PhoneRingtoneLibrary returns a reference to a static const
	// Melody, so storing the address is safe for the screen lifetime
	// and beyond.
	if(ringtone == nullptr) {
		ringtone = &PhoneRingtoneLibrary::get(PhoneRingtoneLibrary::Synthwave);
	}

	Ringtone.play(*ringtone);
	ringtoneActive = true;
}

void PhoneIncomingCall::stopRingtone() {
	// Only stop the engine if WE started it. Belt-and-braces against
	// a future caller that re-uses Ringtone for a foreground music
	// player while the call screen is still alive in the background.
	if(!ringtoneActive) return;
	ringtoneActive = false;
	Ringtone.stop();
}

// ----- action dispatch -----

void PhoneIncomingCall::fireAnswer() {
	// ANSWER softkey: visual click first (so a slow handler still gives
	// the user feedback), then the host callback if one is wired. With
	// no callback we just pop() so the screen does not feel "stuck".
	// The actual call-state push (PhoneActiveCall) lives in S25 and is
	// wired via setOnAnswer() by the LoRa-side handler.
	// S41: stop the ringer before we hand off — the next screen
	// (PhoneActiveCall, or whatever the host pushes) should never
	// inherit a still-playing melody.
	stopRingtone();
	if(softKeys) softKeys->flashLeft();
	if(answerCb) {
		answerCb(this);
	} else {
		pop();
	}
}

void PhoneIncomingCall::fireReject() {
	// REJECT softkey: same flash-then-handler pattern as ANSWER. The
	// default fall-through is pop() so a host that just wanted the
	// visual gets sensible behaviour from BTN_BACK alone.
	// S41: stop the ringer before we hand off / pop, so the
	// piezo never lingers into whatever screen comes next.
	stopRingtone();
	if(softKeys) softKeys->flashRight();
	if(rejectCb) {
		rejectCb(this);
	} else {
		pop();
	}
}

// ----- input -----

void PhoneIncomingCall::buttonPressed(uint i) {
	switch(i) {
		// ANSWER softkey on the left button. Defer the actual handler
		// dispatch to buttonReleased so a long-press that was meant to
		// be cancelled mid-hold (we don't have one here, but matches
		// the dialer's BTN_RIGHT pattern for consistency) does not
		// double-fire on key release.
		case BTN_LEFT:
			if(softKeys) softKeys->flashLeft();
			answerLongFired = false;
			break;

		// BTN_ENTER (the centre A button) is a friendly second way to
		// answer the call - matches the way PhoneAppStubScreen accepts
		// both BTN_BACK and BTN_ENTER as exit keys. Fired immediately
		// here (rather than on release) since A is a single-action
		// "tap to confirm" key with no long-press behaviour wired.
		case BTN_ENTER:
			fireAnswer();
			break;

		// REJECT softkey on the right button. Same defer-to-release
		// pattern as ANSWER.
		case BTN_RIGHT:
			if(softKeys) softKeys->flashRight();
			rejectLongFired = false;
			break;

		// BTN_BACK is the standard "get me out of here" hardware key on
		// every Chatter screen and feels natural as a second REJECT
		// path. Fired on press so the user gets immediate dismissal
		// without waiting for the release event.
		case BTN_BACK:
			fireReject();
			break;

		default:
			break;
	}
}

void PhoneIncomingCall::buttonReleased(uint i) {
	switch(i) {
		case BTN_LEFT:
			// Short press on the ANSWER softkey actually picks up the
			// call. The long-press path (held >= 600 ms) currently has
			// the same effect, but we still gate on the suppression
			// flag so a future "press-and-hold to silent answer" can
			// be added without rewriting this dispatch.
			if(!answerLongFired) {
				fireAnswer();
			}
			answerLongFired = false;
			break;

		case BTN_RIGHT:
			// Short press on the REJECT softkey rejects the call.
			// Same suppression-flag pattern as ANSWER for symmetry.
			if(!rejectLongFired) {
				fireReject();
			}
			rejectLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneIncomingCall::buttonHeld(uint i) {
	switch(i) {
		// Holding ANSWER is treated identically to a short press today,
		// but we set the suppression flag and flash the softkey so the
		// release handler does not double-fire. This split also leaves
		// room for a future "long-press to silent answer" gesture.
		case BTN_LEFT:
			answerLongFired = true;
			fireAnswer();
			break;

		// Holding REJECT is treated as a forced reject (handy if the
		// release event never arrives because the user keeps pressing).
		// Same suppression flag pattern as ANSWER.
		case BTN_RIGHT:
			rejectLongFired = true;
			fireReject();
			break;

		default:
			break;
	}
}
