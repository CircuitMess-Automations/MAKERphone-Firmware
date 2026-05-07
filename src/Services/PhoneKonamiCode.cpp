#include "PhoneKonamiCode.h"

#include "PhoneRingtoneEngine.h"
#include "../MakerphoneTheme.h"

#include <Arduino.h>
#include <Input/Input.h>
#include <Settings.h>
#include <Notes.h>
#include <Pins.hpp>

// =====================================================================
// S166 - PhoneKonamiCode
//
// See PhoneKonamiCode.h for the full design rationale. Implementation
// is deliberately tiny: one progress counter, one millis timestamp,
// one InputListener subscription. No allocation, no LoopManager
// subscription, no per-screen wiring - the detector lives outside the
// LVScreen stack so the same sequence works on every screen (lock,
// home, dialer, settings, every game) without each screen having to
// care about it.
// =====================================================================

PhoneKonamiCode Konami;

namespace {

// One-shot ascending arpeggio - a brief 'unlock!' confirmation that
// reads as deliberately different from the boot chime (S148, four-note
// G-major), the power-down chime (S149, descending), and the charge-
// complete chime (S150, three-step ascending major-third). Five notes
// stepping up an A-major pentatonic so the family stays consistent
// (the S148 / S149 / S150 chimes all sit in the same major-pentatonic
// vocabulary), but with one extra note + a slightly faster pace so
// the unlock chime reads as 'a celebration' rather than 'a status
// transition'. Total ~610 ms - quick enough that the user hears it as
// a single ear-catching cue rather than a sustained ringtone.
const PhoneRingtoneEngine::Note kUnlockNotes[] = {
		{ NOTE_A5, 90 },
		{ NOTE_CS6, 90 },
		{ NOTE_E6,  90 },
		{ NOTE_A6,  90 },
		{ NOTE_E7,  240 },
};

const PhoneRingtoneEngine::Melody kUnlockMelody = {
		kUnlockNotes,
		sizeof(kUnlockNotes) / sizeof(kUnlockNotes[0]),
		25,        // gapMs - same family as S148/S149/S150 chimes
		false,     // one-shot
		"Konami",
};

} // namespace

// ---------------------------------------------------------------------
// The 10-press Konami sequence in hardware-button form. The first four
// presses use BTN_LEFT / BTN_RIGHT directly because Pins.hpp aliases
// BTN_UP -> BTN_LEFT and BTN_DOWN -> BTN_RIGHT, so 'UP UP DOWN DOWN'
// on the keypad is physically the same as 'LEFT LEFT RIGHT RIGHT'.
// The final two presses use BTN_BACK / BTN_ENTER directly because
// Pins.hpp aliases BTN_B -> BTN_BACK and BTN_A -> BTN_ENTER. So the
// table represents the literal NES Konami sequence as the player
// would mentally enter it on a 1986 Nintendo controller, even though
// the hardware names look hardware-feature-phone shaped.
//
// Cross-checked against libraries/Chatter-Library/src/Pins.hpp:
//   BTN_LEFT  = 4   (== BTN_UP)
//   BTN_RIGHT = 5   (== BTN_DOWN)
//   BTN_BACK  = 7   (== BTN_B)
//   BTN_ENTER = 6   (== BTN_A)
// ---------------------------------------------------------------------
const uint8_t PhoneKonamiCode::Expected[PhoneKonamiCode::ExpectedLen] = {
		BTN_LEFT,  BTN_LEFT,                       // UP UP
		BTN_RIGHT, BTN_RIGHT,                      // DOWN DOWN
		BTN_LEFT,  BTN_RIGHT,                      // LEFT RIGHT
		BTN_LEFT,  BTN_RIGHT,                      // LEFT RIGHT
		BTN_BACK,  BTN_ENTER,                      // B A
};

// ---------- lifecycle ------------------------------------------------

void PhoneKonamiCode::begin() {
	// Idempotent: re-registering with the same listener instance is
	// fine because Input keeps an unordered_set internally.
	Input::getInstance()->addListener(this);
	reset();
}

void PhoneKonamiCode::reset() {
	idx = 0;
	lastHitMs = 0;
}

// ---------- detection ------------------------------------------------

void PhoneKonamiCode::buttonPressed(uint i) {
	const uint32_t now = millis();

	// Inactivity-window reset. If the user paused longer than ResetMs
	// (4 s) between presses, treat this press as the start of a new
	// attempt rather than a continuation - prevents the detector from
	// accidentally completing across a long span of incidental
	// keypresses.
	if(idx > 0 && (uint32_t)(now - lastHitMs) > ResetMs) {
		idx = 0;
	}

	const uint8_t btn = static_cast<uint8_t>(i);

	if(btn == Expected[idx]) {
		// In-sequence press. Advance the counter; if we just landed
		// on the final position fire the unlock and roll the counter
		// back to 0 so a determined user could enter the sequence
		// again (a no-op at the Settings layer, but cleaner than
		// leaving the detector pinned at idx == ExpectedLen).
		idx++;
		lastHitMs = now;
		if(idx >= ExpectedLen) {
			idx = 0;
			applyUnlock();
		}
		return;
	}

	// Out-of-sequence press. Reset to 0 - but if the rogue press
	// happens to also match Expected[0] (i.e. a stray BTN_LEFT) then
	// give the user the benefit of the doubt and treat it as the
	// first character of a fresh attempt. This is the canonical
	// 'rolling-window' Konami detector behaviour and matches the
	// 1986 prompt's tolerance: pressing UP after a botched sequence
	// counts as the start of a new try, not as a wasted attempt.
	if(btn == Expected[0]) {
		idx = 1;
		lastHitMs = now;
	} else {
		idx = 0;
	}
}

// ---------- unlock side-effect ---------------------------------------

void PhoneKonamiCode::applyUnlock() {
	auto& s = Settings.get();

	const bool wasAlreadyUnlocked = s.rainbowUnlocked;
	const bool wasAlreadyOnRainbow =
			(s.themeId == static_cast<uint8_t>(MakerphoneTheme::Theme::Rainbow));

	// Flip the unlock flag (sticky once true) and force the active
	// theme to Rainbow. Even if the user had previously unlocked it
	// and then picked a different theme via PhoneThemeScreen,
	// re-entering the code snaps them back to Rainbow - the Konami
	// gesture is also a 'turn it on' shortcut, not just a one-time
	// reveal. Persisted via Settings.store() so the next boot opens
	// straight into the rainbow palette without re-entering the code.
	s.rainbowUnlocked = true;
	s.themeId = static_cast<uint8_t>(MakerphoneTheme::Theme::Rainbow);

	// Only commit to NVS when something actually changed; saves a
	// flash-write cycle on the (admittedly rare) repeat-Konami case
	// where the user re-enters the code while already on Rainbow.
	if(!wasAlreadyUnlocked || !wasAlreadyOnRainbow) {
		Settings.store();
	}

	// S229 - SILENT / MEETING profile gate. PhoneRingtoneEngine
	// internally short-circuits the piezo when Settings.sound is
	// false, BUT the engine self-mutes per-loop, so the micro-window
	// between `Ringtone.play()` and the engine's first mute pass is
	// enough for some Chatter units to emit an audible blip before
	// falling silent. Closing the window here mirrors the S205 /
	// S219-S223 / S225 / S226 / S227 / S228 sweep convention: skip
	// the engine call entirely under a silenced profile so the
	// LoopManager listener is never registered. The visual unlock
	// side-effects above (`rainbowUnlocked`, `themeId`,
	// `Settings.store()`) have already landed by this point, so the
	// rainbow theme still flips on for the next screen draw - only
	// the audible confirmation is suppressed.
	if(isSilenced()) return;

	// Audible confirmation. Routed through the global ringtone engine
	// so a fresh GENERAL / OUTDOOR / HEADSET-profile device still
	// hears the chime. The S148 boot chime, S149 power-off arpeggio,
	// and S150 charge-complete chime all share the engine, so a
	// Konami unlock fired during one of those overlapping cues will
	// smoothly pre-empt the in-progress melody - the ringtone engine
	// takes the most recent play() request as the authoritative one.
	Ringtone.play(kUnlockMelody);
}

// S229 - SILENT / MEETING profile gate. PhoneProfileScreen (S159)
// writes `Settings.get().sound = false` for both Silent and Meeting
// profiles and `true` for General / Outdoor / Headset, so reading
// the legacy bool is the cheapest one-read cover for every "should
// the Konami unlock chime drive the piezo right now" case without
// dragging the five-state enum into this service. Same pattern S205 /
// S219-S223 / S225 / S226 / S227 / S228 use for their per-screen /
// per-modal / per-service helpers; PhoneKonamiCode was the last
// surviving service-layer non-alarm `Ringtone.play()` call site that
// the S228 commit message claimed was already covered. The
// PhoneBootSplash boot chime (S148) and PhonePowerDown power-off
// arpeggio (S149) intentionally stay un-gated: the boot chime fires
// before Settings has reliably been hydrated from NVS in some
// failure-recovery boot paths, and the power-off arpeggio is a
// deliberate user-confirmation cue for a destructive action.
// PhoneSystemTones (S192) routes through the engine via a single
// `play(uint8_t id)` entry point and is the next non-alarm surface
// the sweep can revisit; gating that one centrally is a separate
// session because it touches every UI cue at once and wants a slower
// rollout.
bool PhoneKonamiCode::isSilenced(){
	return !Settings.get().sound;
}
