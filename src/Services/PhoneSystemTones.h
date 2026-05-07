#ifndef MAKERPHONE_PHONESYSTEMTONES_H
#define MAKERPHONE_PHONESYSTEMTONES_H

#include <Arduino.h>
#include "PhoneRingtoneEngine.h"

/**
 * S192 — PhoneSystemTonesLib
 *
 * Code-only catalogue of short system chimes the firmware uses to give
 * every common interaction its own audible voice. Each entry is a
 * tiny `PhoneRingtoneEngine::Melody` (1..6 notes, 25..120 ms each)
 * keyed by an enum so any caller can fire a recognisable cue with one
 * line:
 *
 *     PhoneSystemTones::play(PhoneSystemTones::Notify);
 *
 * The library is intentionally a sibling of the existing chime
 * services (S148 boot, S149 power-off, S150 charge-complete, S157
 * delivered, S183 soft-key tones) rather than a replacement — those
 * services own their own state machines and timing rules and can keep
 * using their bespoke melodies. S192 covers every *other* action that
 * has gone unscored until now: notifications, errors, menu sounds,
 * save/delete confirmations, timer-done, network status, low-battery
 * warnings and so on. Eighteen distinct cues ship in v1, all under
 * 200 ms, all built from `Notes.h` constants — no SPIFFS asset cost.
 *
 * Design constraints (kept identical with the rest of the audio family):
 *
 *  - Playback routes through the global `PhoneRingtoneEngine`, so the
 *    Settings.sound mute gate and the engine's "active melody wins"
 *    semantics are honoured automatically. A ringing call therefore
 *    can NOT be interrupted by, say, a Save chime — the engine simply
 *    replaces the current melody, so latch-on signals like the call
 *    ringer always take precedence by virtue of their cooldown
 *    longevity.
 *
 *  - Every chime is short enough that the audio finishes before a
 *    typical UI animation (toast, confetti, screen-pop) wraps up,
 *    keeping audio + visual feedback in the same perceptual frame.
 *
 *  - Every chime's first note is at least NOTE_C5 (523 Hz) so the
 *    cue reads as a "system" sound rather than a ringtone-grade
 *    melody — bass tones are reserved for the call ringer family.
 *
 *  - Pitches and silhouettes are deliberately mutually distinguishable
 *    even from the next room: ascending two-note for positives,
 *    descending two-note for negatives, identical-pip pairs for
 *    confirmations, single low pulse for warnings. See the
 *    implementation file's note arrays for the per-chime rationale.
 *
 *  - The catalogue is exposed as `Id` (enum) + `count()` + `name()`
 *    + `play()` so future settings UIs ("Settings → Sounds → System
 *    chimes") can introspect and preview without depending on screen
 *    code. Mirrors the S183 PhoneSoftKeyToneLib pattern so the next
 *    picker screen is a copy-paste away.
 */
class PhoneSystemTones {
public:
	enum Id : uint8_t {
		Notify        = 0,   // generic notification ping
		Success       = 1,   // positive confirmation
		Error         = 2,   // generic error / something went wrong
		Alert         = 3,   // urgent alert (more insistent than Error)
		Unlock        = 4,   // lock screen unlocked
		Lock          = 5,   // lock screen locked
		SmsReceived   = 6,   // new incoming SMS arrived
		CallEnd       = 7,   // outbound or inbound call hangs up
		MenuOpen      = 8,   // menu / sub-screen pushed
		MenuClose     = 9,   // menu / sub-screen popped
		Save          = 10,  // settings / draft / contact saved
		DeleteItem    = 11,  // item deleted (contact, message, alarm, etc.)
		TimerDone     = 12,  // pomodoro / countdown / cookie timer fired
		AlarmDismiss  = 13,  // alarm clock snoozed or dismissed
		LevelUp       = 14,  // game level-up / achievement
		NetworkOk     = 15,  // LoRa reconnected after a drop
		NetworkFail   = 16,  // LoRa lost / pairing failed
		LowBattery    = 17,  // low-battery warning
		Count         = 18
	};

	/** Number of catalogued tones (== Id::Count). */
	static uint8_t count();

	/** True iff `id` is in [0..Count-1]. */
	static bool valid(uint8_t id);

	/**
	 * Display name for the picker (uppercase, fits a 70 px column at
	 * pixelbasic7). nullptr is never returned for a valid id; for an
	 * invalid id the function returns "?" so callers don't need to
	 * special-case the lookup result.
	 */
	static const char* name(uint8_t id);

	/**
	 * Play the catalogued tone via the global Ringtone engine. Invalid
	 * ids and the future "Silent" extension are no-ops; callers never
	 * have to special-case them. The engine itself respects the
	 * Settings.sound mute, so this routes are correctly silenced on
	 * Silent / Meeting profiles without any extra work at the call
	 * site.
	 */
	static void play(uint8_t id);

	/**
	 * S230 - SILENT / MEETING profile gate. PhoneProfileScreen (S159)
	 * writes `Settings.get().sound = false` for both Silent and Meeting
	 * profiles and `true` for General / Outdoor / Headset, so reading
	 * the legacy bool is the cheapest one-read cover for every "should
	 * a system chime drive the piezo right now" question without
	 * dragging the five-state `ProfileService::Profile` enum into this
	 * library. Mirrors the S205 (PhoneRadio), S219 (PhoneComposer),
	 * S220 (PhoneMusicPlayer), S221 (PhoneAlarmTonePicker), S222
	 * (PhoneContactRingtonePicker), S223 (PhoneProfileRingtoneScreen),
	 * S225 (PhoneCameraScreen), S226 (PhoneBatteryLowModal), S227
	 * (PhoneDeliveredChime), S228 (PhoneChargeChime) and S229
	 * (PhoneKonamiCode) gates - one shared idiom across every non-
	 * alarm ringtone-engine call site in the firmware.
	 *
	 * `play()` consults this helper as its first action. When silenced
	 * the `Ringtone.play()` call (and therefore the LoopManager
	 * listener registration) is skipped entirely, closing the micro-
	 * window between the engine handoff and the engine's first per-
	 * loop mute pass that some Chatter units render as an audible blip
	 * before the engine catches up. The catalogue itself stays loud
	 * (the `melody()` accessor still returns the same const Melody*
	 * pointers regardless of profile state, so call sites that PRE-
	 * LOAD a melody before firing it - notably PhoneIncomingCall - are
	 * untouched), only the `play(uint8_t id)` entry point gates.
	 *
	 * Public so a future "Settings -> Sounds -> System chimes" picker
	 * can show the resolved silenced state without re-deriving it from
	 * Settings, exactly as S205 / S219-S229 expose theirs.
	 */
	static bool isSilenced();

	/**
	 * Pointer to the underlying Melody so sites like
	 * `PhoneIncomingCall` (which want to PRE-LOAD a melody before
	 * actually firing it) can grab the structure without going
	 * through `play()`. Returns nullptr for invalid ids. The pointer
	 * targets static const storage so it's safe to keep across screen
	 * lifetimes.
	 */
	static const PhoneRingtoneEngine::Melody* melody(uint8_t id);
};

#endif // MAKERPHONE_PHONESYSTEMTONES_H
