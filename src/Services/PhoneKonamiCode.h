#ifndef CHATTER_FIRMWARE_PHONE_KONAMI_CODE_H
#define CHATTER_FIRMWARE_PHONE_KONAMI_CODE_H

#include <Arduino.h>
#include <Input/InputListener.h>

/**
 * S166 - PhoneKonamiCode (service)
 *
 * Global Konami-code Easter-egg detector. Listens to every button
 * press across the entire firmware and watches for the canonical
 * 10-press sequence:
 *
 *     UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A
 *
 * On the Chatter hardware this maps mechanically to:
 *
 *     LEFT LEFT RIGHT RIGHT LEFT RIGHT LEFT RIGHT BACK ENTER
 *
 * because Pins.hpp aliases BTN_UP -> BTN_LEFT, BTN_DOWN -> BTN_RIGHT,
 * BTN_B -> BTN_BACK, BTN_A -> BTN_ENTER. So when the user enters the
 * literal Konami code on the keypad they are pressing exactly the
 * 10 hardware buttons we're matching against - the alias makes the
 * code feel 'right' to anyone who grew up with it on the NES, even
 * though our d-pad is technically only left/right.
 *
 * On a successful match the service:
 *
 *   1. Flips Settings.rainbowUnlocked to true (one-way; the unlock
 *      sticks across reboots so the user can re-pick the rainbow
 *      theme from PhoneThemeScreen without re-entering the code).
 *
 *   2. Sets Settings.themeId to MakerphoneTheme::Theme::Rainbow (11)
 *      and persists via Settings.store(). The next screen that
 *      drops a `new PhoneSynthwaveBg(obj)` (i.e. the very next
 *      navigation away from the current screen) picks up the new
 *      palette and re-tints every Phone* widget on top of the
 *      Synthwave wallpaper - the rainbow theme's accent / highlight
 *      / dim role colours come straight from the palette helpers in
 *      MakerphoneTheme.cpp.
 *
 *   3. Plays a brief one-shot ascending arpeggio through the global
 *      PhoneRingtoneEngine so the user hears confirmation. The chime
 *      respects Settings.sound mute exactly the same way the boot
 *      and charge-complete chimes do - silent profiles get the
 *      visual unlock without an audible cue.
 *
 * Idle-cheap: the only state is a small sequence-progress counter and
 * a millis() timestamp of the last accepted press. No allocation, no
 * LoopListener subscription. The detector is purely a passive
 * InputListener.
 *
 * Resets:
 *
 *   - Any out-of-sequence button press resets the progress counter
 *     to 0 (or 1 if the rogue press happens to also match the first
 *     code character - i.e. a stray BTN_LEFT keeps the door open
 *     for the next attempt without forcing a hard restart).
 *
 *   - A pause longer than ResetMs (4 s) between two consecutive
 *     accepted presses also resets the counter. This stops the
 *     detector from accidentally completing across an entire day's
 *     worth of incidental keypresses (a stray BTN_LEFT in the
 *     dialer at 09:00 followed by a stray BTN_ENTER at 17:00 should
 *     not light up rainbow). 4 s is generous enough for a
 *     deliberate input - the original Konami-code prompt on Contra
 *     was effectively also a 'you have to mean it' pause-tolerant
 *     window.
 *
 *   - The detector does NOT reset on a successful match; the
 *     counter just rolls back to 0 so a determined user could in
 *     theory chain a second unlock. Today the second unlock is a
 *     no-op (rainbowUnlocked is already true and themeId is already
 *     Rainbow) so the chime would simply replay - harmless.
 *
 * Persistence:
 *
 *   - The unlock flag and the theme byte both live in SettingsData
 *     and ride the existing Settings.store() NVS pipeline. No
 *     bespoke storage layer.
 */
class PhoneKonamiCode : private InputListener {
public:
	/** Lifecycle. begin() is idempotent. */
	void begin();

	/** Reset the matching state to 'no progress'. Useful for tests. */
	void reset();

	/** Read-only view of the matching progress (0..ExpectedLen). The
	 *  unit-test harness uses this to verify the detector advances
	 *  correctly press-by-press. */
	uint8_t progress() const { return idx; }

	/**
	 * S229 - SILENT / MEETING profile gate. PhoneProfileScreen
	 * (S159) writes `Settings.get().sound = false` for both Silent
	 * and Meeting profiles and `true` for General / Outdoor /
	 * Headset, so reading the legacy bool is the cheapest one-read
	 * cover for every "should the Konami unlock chime drive the
	 * piezo right now" case without dragging the five-state enum
	 * into this service. Mirrors the S228 gate on
	 * PhoneChargeChime::isSilenced(), the S227 gate on
	 * PhoneDeliveredChime, the S226 gate on PhoneBatteryLowModal,
	 * the S225 gate on PhoneCameraScreen, the S219-S223 gates on
	 * the composer / music-player / ringtone-picker family, and
	 * the S205 gate on PhoneRadio.
	 *
	 * `applyUnlock()` consults this helper after running its
	 * sticky `Settings.rainbowUnlocked` / `Settings.themeId` writes
	 * (and the conditional `Settings.store()` flush) but before
	 * handing the ascending arpeggio to the engine. The
	 * PhoneRingtoneEngine already self-mutes per loop tick when
	 * `Settings.sound == false`, but the micro-window between
	 * `Ringtone.play()` and the engine's first mute pass is enough
	 * for some Chatter units to emit an audible blip before falling
	 * silent - exactly the failure mode the S205 / S219-S228 sweep
	 * removed from every screen, modal and chime service that
	 * drives the piezo. The S228 commit body had claimed the sweep
	 * was complete for every non-alarm `Ringtone.play()` call site
	 * in the firmware; that was wrong -
	 * `PhoneKonamiCode::applyUnlock()` was the last surviving
	 * service-layer call site that bypassed the gate. Closing it
	 * here brings the Easter-egg unlock chime into the same
	 * convention.
	 *
	 * The visual unlock side-effects (`rainbowUnlocked = true`,
	 * `themeId = Rainbow`, persisted via `Settings.store()`) still
	 * land regardless of the silenced state - only the audible
	 * confirmation is suppressed. This matches the semantics every
	 * other gated cue uses (the underlying state transition or
	 * one-shot bookkeeping always runs, only the piezo melody is
	 * skipped) so a SILENT-profile user re-entering the code on a
	 * later GENERAL boot still gets the chime, and a GENERAL-profile
	 * user gets the byte-identical pre-S229 behaviour.
	 *
	 * Public so a future themes / unlock debug surface can present
	 * the resolved silenced state without having to re-derive it
	 * from Settings.
	 */
	static bool isSilenced();

private:
	void buttonPressed(uint i) override;

	/** Length of the Konami sequence (10 buttons). */
	static constexpr uint8_t ExpectedLen = 10;

	/** The 10-press Konami sequence in hardware-button form. The
	 *  table is referenced by index against `idx`. */
	static const uint8_t Expected[ExpectedLen];

	/** Inactivity reset window (ms). A pause longer than this between
	 *  two consecutive accepted presses resets the matching state. */
	static constexpr uint32_t ResetMs = 4000;

	uint8_t  idx = 0;
	uint32_t lastHitMs = 0;

	/** Apply the unlock: flip rainbowUnlocked, swap themeId, persist
	 *  via Settings.store(), play the ascending chime. Centralised
	 *  here so the matching logic in buttonPressed() stays readable. */
	void applyUnlock();
};

extern PhoneKonamiCode Konami;

#endif // CHATTER_FIRMWARE_PHONE_KONAMI_CODE_H
