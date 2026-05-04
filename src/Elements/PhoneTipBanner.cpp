#include "PhoneTipBanner.h"
#include "../Fonts/font.h"
#include "../MakerphoneTheme.h"
#include <Loop/LoopManager.h>
#include <Input/Input.h>

// Palette: theme-aware so the banner flips to the active skin
// (Synthwave cream-on-purple by default, Nokia 3310 dark olive on
// pale olive, etc.). The caption draws in MP_TEXT and the slab
// background hugs MP_DIM at a soft opacity so the strip reads as
// "softly-lit chip" on every theme that ships through
// MakerphoneTheme.
#define MP_TEXT  (MakerphoneTheme::text())
#define MP_DIM   (MakerphoneTheme::dim())

// Twelve hand-picked tips, each <= 21 visible characters so the
// caption fits inside the 140 px banner with a touch of horizontal
// breathing room. Every tip references a feature that has already
// shipped through an earlier session, so the rotating pool never
// surfaces a hint for a screen the user can not actually reach.
static const char* const kTips[PhoneTipBanner::TipCount] = {
	"Hold 0: quick dial",      // S22  - homescreen quick-dial gesture
	"Hold Back to lock",       // S22  - homescreen lock-hold gesture
	"Try *#06# on dialer",     // S164 - IMEI Easter egg
	"Try *#0000# for info",    // S165 - firmware-info Easter egg
	"Hold 5 = flashlight",     // S167 - dialer flashlight shortcut
	"L+R shakes the screen",   // S168 - tilt/shake simulator
	"Hold 1-9: speed dial",    // S151 - speed-dial slots
	"Stay curious!",           // generic / brand-voice tip
	"Press any key to wake",   // S154 - the idle-hint cue itself
	"Made by CircuitMess",     // brand tip - safe on every theme
	"Try the Konami code",     // S166 - rainbow theme unlock
	"USB-C to charge me",      // generic battery tip
};

PhoneTipBanner::PhoneTipBanner(lv_obj_t* parent) : LVObject(parent) {
	// Free-floating slab; the home screen handles every other widget
	// in its own column, so the banner anchors itself to TOP_MID at a
	// fixed y rather than joining whatever flex layout the parent
	// happens to be running.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(obj, BannerWidth, BannerHeight);
	lv_obj_set_align(obj, LV_ALIGN_TOP_MID);
	lv_obj_set_y(obj, BannerYOffset);

	// Soft dim background, rounded ends, no border. Boots invisible -
	// the very first loop() tick at most flips nothing. The fade-in
	// only starts after IdleMs of stillness.
	lv_obj_set_style_bg_color(obj, MP_DIM, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(obj, BannerHeight / 2, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_outline_width(obj, 0, 0);

	caption = lv_label_create(obj);
	lv_obj_set_style_text_font(caption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(caption, MP_TEXT, 0);
	lv_obj_set_style_text_letter_space(caption, 1, 0);
	lv_obj_set_style_opa(caption, LV_OPA_TRANSP, 0);
	lv_obj_center(caption);

	// Pick the first tip up front so the very first fade-in already
	// shows a randomized message rather than a hard-coded slot 0.
	pickNewTip();

	lastActivityMs = millis();
	appliedOpa     = 0;
	shownAtMs      = 0;

	LoopManager::addListener(this);
	Input::getInstance()->addListener(this);
}

PhoneTipBanner::~PhoneTipBanner() {
	// Order matters: drop the listener registrations BEFORE the
	// LVObject base destructor frees `obj`, so a frame in flight does
	// not see a dangling caption pointer.
	LoopManager::removeListener(this);
	Input::getInstance()->removeListener(this);
}

void PhoneTipBanner::setActive(bool a) {
	active = a;
	if(!a) {
		// Gate close - snap invisible and rewind the clock so the
		// next `setActive(true)` does not race a half-faded banner
		// into a freshly-visible host widget.
		applyOpa(0);
		shownAtMs      = 0;
		lastActivityMs = millis();
	}
	// Re-arming after a gate close - the user has not just touched a
	// key, so we leave the clock alone. The host can call
	// resetActivity() to start the idle window from scratch when it
	// wants the banner to behave as if the user just walked up.
}

void PhoneTipBanner::resetActivity() {
	lastActivityMs = millis();
	if(appliedOpa != 0) {
		applyOpa(0);
	}
	shownAtMs = 0;
}

void PhoneTipBanner::loop(uint /*micros*/) {
	if(!active) {
		if(appliedOpa != 0) {
			applyOpa(0);
		}
		return;
	}

	const uint32_t now     = millis();
	const uint32_t idleFor = now - lastActivityMs;
	if(idleFor < IdleMs) {
		// Still inside the "user just touched something" window.
		// Keep the banner hidden and the shownAt clock cleared so
		// the next idle cycle starts from a clean 0-opa baseline.
		if(appliedOpa != 0) {
			applyOpa(0);
			shownAtMs = 0;
		}
		return;
	}

	if(shownAtMs == 0) {
		// First frame of this idle cycle - pick a fresh random tip
		// so each fade-in surfaces something new even when the
		// device has been napping all afternoon.
		pickNewTip();
		shownAtMs = now;
	}

	const uint32_t shownFor = now - shownAtMs;
	uint8_t opa;
	if(shownFor < FadeMs) {
		// Linear ramp 0 -> PeakOpa across the fade window.
		opa = (uint8_t)(((uint32_t) PeakOpa * shownFor) / FadeMs);
	} else {
		// Steady-state plateau - the PhoneIdleHint already pulses,
		// so the tip strip stays calmly readable rather than
		// competing with the soft "PRESS ANY KEY" rhythm.
		opa = PeakOpa;
	}
	applyOpa(opa);
}

void PhoneTipBanner::anyKeyPressed() {
	// Any button counts as activity - the same contract every other
	// homescreen idle widget honours. Idle clock resets, banner
	// snaps invisible, and the next idle cycle starts from a clean
	// 0-opa baseline so the user never sees a half-faded ghost on
	// the very next press.
	lastActivityMs = millis();
	if(appliedOpa != 0) {
		applyOpa(0);
	}
	shownAtMs = 0;
}

void PhoneTipBanner::applyOpa(uint8_t opa) {
	if(opa == appliedOpa) {
		return;
	}
	if(caption == nullptr) {
		return;
	}

	// Background hugs ~24 % of the caption opa so the slab reads as a
	// softly-lit strip rather than a hard chrome chip - matches the
	// way the synthwave wallpaper soft-glows the rest of the home
	// screen and keeps the strip from punching a bright bubble out
	// of the wallpaper. Cap at 255 just to be defensive about the
	// uint16 multiplication.
	uint16_t bgOpa = (uint16_t) opa * 60u / 255u;
	if(bgOpa > 255u) bgOpa = 255u;
	lv_obj_set_style_bg_opa(obj, (uint8_t) bgOpa, 0);
	lv_obj_set_style_opa(caption, opa, 0);
	appliedOpa = opa;
}

void PhoneTipBanner::pickNewTip() {
	if(caption == nullptr) {
		return;
	}
	// Arduino's random() is fine - we just want variation per cycle,
	// not crypto-grade entropy. Pull until we land on a slot that is
	// not the one we just showed, so two consecutive idle cycles
	// never surface the same string back-to-back. With TipCount = 12
	// the worst case is one extra modulo bump.
	uint8_t next = (uint8_t) random(TipCount);
	if(TipCount > 1 && next == currentTip) {
		next = (uint8_t)((next + 1u) % TipCount);
	}
	currentTip = next;
	lv_label_set_text(caption, kTips[currentTip]);
}
