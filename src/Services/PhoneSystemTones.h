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

	/**
	 * S231 - introspective wrapper around `play(uint8_t id)`. Same
	 * gate / valid-id semantics as `play()` (skip on out-of-range id,
	 * skip on `isSilenced()`, otherwise hand the catalogued Melody to
	 * the engine via `Ringtone.play()`), but reports back to the
	 * caller whether the engine call actually fired. Returns:
	 *
	 *   - `false` if `id` is out of range (>= Count). Nothing plays.
	 *   - `false` if a SILENT / MEETING profile silenced the catalogue
	 *     (i.e. `isSilenced()` returned true). Nothing plays.
	 *   - `true`  if the engine call fired and the LoopManager listener
	 *     was registered. Caller can assume the cue is now in flight.
	 *
	 * Foreshadowed by the S192 / S230 commit bodies' "future Settings
	 * -> Sounds -> System chimes picker" design notes (mirroring the
	 * existing S183 PhoneSoftKeyToneScreen pattern): a preview row in
	 * that picker wants to know whether a tap actually produced audio
	 * so it can fall back to a "(silenced)" caption when the active
	 * profile gates the engine call out -- without re-deriving the
	 * gate from `Settings.get().sound` at the picker level. A future
	 * diag screen (e.g. a "Sound test" entry in PhoneDiagScreen that
	 * walks every chime in turn) wants the same answer for the same
	 * reason.
	 *
	 * `play(uint8_t id)` continues to exist as the cheap fire-and-
	 * forget entry point and is rewired in S231 to `(void)tryPlay(id)`
	 * so the gate / valid-id checks live in exactly one place. The
	 * engine call boundary is byte-identical to the pre-S231 path:
	 * same `valid()` early-return, same `isSilenced()` early-return,
	 * same `Ringtone.play(kMelodies[id])` invocation, same lack of
	 * persisted state. Every existing `PhoneSystemTones::play()` call
	 * site in the firmware (S110 PhoneCallEnded, S134 PhoneSimon,
	 * S141 PhoneAlarmClock, S157 etc.) keeps the same observable
	 * behaviour without any per-site changes.
	 *
	 * `tryPlay()` does NOT subsume `melody(uint8_t id)`: any caller
	 * that PRE-LOADS a melody pointer to fire later (notably
	 * PhoneIncomingCall) still wants the const Melody* directly, not
	 * a "did the engine fire" answer for the catalogue's central
	 * entry point. The two helpers stay deliberately distinct.
	 */
	static bool tryPlay(uint8_t id);

	/**
	 * S232 - total catalogued playback duration of chime `id` in
	 * milliseconds. Computed as the sum of the per-Note `durationMs`
	 * entries plus the inter-note `gapMs` waits that
	 * `PhoneRingtoneEngine::loop()` interleaves between consecutive
	 * notes (one gap is consumed after every note when `gapMs > 0`,
	 * including the last one before the engine transitions to
	 * `stop()`; see `PhoneRingtoneEngine.cpp::loop()` for the
	 * inGap/step-advance state machine). Returns 0 for an invalid
	 * id and for an empty melody, saturates to UINT16_MAX if the
	 * catalogue ever grows large enough to overflow (no current
	 * entry comes close -- the longest, S192 LowBattery, is well
	 * under 500 ms). Profile-state INDEPENDENT: the catalogue
	 * answer is the same on SILENT / MEETING profiles as on
	 * GENERAL / OUTDOOR / HEADSET, so the foreshadowed
	 * "Settings -> Sounds -> System chimes" picker can debounce
	 * repeat row presses by a stable duration regardless of
	 * whether the previous press actually fired the engine (see
	 * S231 `tryPlay` for the gate). Cheap O(notes) sum over the
	 * catalogue's static const Melody pointer with a uint32_t
	 * accumulator; no persisted state, no per-call allocation,
	 * no engine interaction.
	 *
	 * Foreshadowed by the S192 / S230 / S231 commit bodies' "future
	 * Settings -> Sounds -> System chimes picker" design notes: a
	 * picker that drives the engine via `tryPlay(id)` and shows a
	 * "(silenced)" caption fade for SILENT / MEETING profiles wants
	 * to know how long the catalogued cue is so it can schedule the
	 * caption fade-out (and its own row-press debounce) without
	 * registering a LoopManager listener of its own. A future diag
	 * screen that walks every chime in turn (the `PhoneDiagScreen`
	 * "Sound test" entry foreshadowed in S231) wants the same
	 * answer for the same reason -- it can step to the next chime
	 * `durationMs(id)` ms after the previous tryPlay returned true,
	 * matching the engine's natural playback boundary.
	 *
	 * The catalogued answer ignores the engine's `loop` flag (no
	 * S192 chime loops -- looping is reserved for the call ringer
	 * family) so callers do not have to special-case looping
	 * melodies. If a future catalogue entry opts into looping the
	 * helper still reports the duration of one pass, which is the
	 * meaningful answer for a picker preview / diag walk.
	 */
	static uint16_t durationMs(uint8_t id);

	/**
	 * S233 - structural note-count accessor for chime `id`. Returns
	 * the number of catalogued `PhoneRingtoneEngine::Note` entries in
	 * the underlying Melody (i.e. `kMelodies[id].count`). Returns 0
	 * for an out-of-range id and for the (currently impossible)
	 * empty-melody case.
	 *
	 * Foreshadowed by the S192 / S230 / S231 / S232 commit bodies'
	 * "future Settings -> Sounds -> System chimes picker" and "future
	 * `PhoneDiagScreen` Sound test entry that walks every chime in
	 * turn" design notes. Both want to introspect the catalogued
	 * Melody's structural shape without touching the const Melody*
	 * pointer at the call site:
	 *
	 *   - the picker can render a "(N notes, M ms)" caption beside
	 *     each row using `noteCount(id)` + `durationMs(id)` together;
	 *   - the diag walk can drive a per-note pulse indicator (one
	 *     pulse per catalogued note while the engine is firing) by
	 *     dividing `durationMs(id)` evenly across `noteCount(id)`
	 *     animation frames -- LovyanGFX-side, no engine listener
	 *     needed;
	 *   - a future "preview" row can show a tiny dotted timeline
	 *     with one dot per catalogued note for visual differentiation
	 *     between equal-length cues (e.g. Notify and SmsReceived are
	 *     both two-pip pairs but at different pitches; the dot count
	 *     agrees, the diag duration is similar, the pitch is the
	 *     only differentiator -- the foreshadowed picker uses
	 *     `noteCount(id)` for the dot row and a future
	 *     `firstFreqHz(id)` accessor for the pitch indicator).
	 *
	 * Profile-state INDEPENDENT: the catalogued shape is the same on
	 * SILENT / MEETING profiles as on GENERAL / OUTDOOR / HEADSET, so
	 * a picker can lay out its row labels at construction time and
	 * leave them unchanged when the user toggles profiles. The S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that needs to fade in a
	 * "(silenced)" caption.
	 *
	 * Cheap O(1) struct field read; no engine interaction, no
	 * persisted state, no per-call allocation. Header surface grows
	 * by exactly one public symbol (`static uint16_t noteCount`);
	 * the cpp adds a single function next to the existing `count` /
	 * `valid` / `name` / `melody` / `play` / `tryPlay` /
	 * `isSilenced` / `durationMs` cluster. No new includes (the
	 * `Melody` and `Note` types live behind the existing
	 * `PhoneRingtoneEngine.h` include and the `kMelodies` table
	 * already lives in this translation unit's anonymous namespace),
	 * no new const data, no new SPIFFS asset cost. Every existing
	 * call site of the catalogue keeps byte-identical behaviour --
	 * the new helper is purely additive.
	 */
	static uint16_t noteCount(uint8_t id);

	/**
	 * S234 - structural first-note pitch accessor for chime `id`.
	 * Returns the catalogued frequency in Hz of the FIRST
	 * `PhoneRingtoneEngine::Note` entry in the underlying Melody
	 * (i.e. `kMelodies[id].notes[0].freq`). Returns 0 for an
	 * out-of-range id, for the (currently impossible) empty-melody
	 * case, and -- transparently -- for the (currently impossible)
	 * leading-rest case (a Note with `freq == 0` is the catalogue's
	 * encoding for a silent step; no v1 chime opens with a rest, so
	 * the answer collapses to "the catalogued first audible note's
	 * pitch" for every entry that ships today, while staying
	 * unambiguous if a future chime ever opens with a rest -- 0 is
	 * the same value the engine itself uses to mean "no tone is
	 * being driven right now").
	 *
	 * Foreshadowed by the S233 commit body's "future `firstFreqHz(id)`
	 * accessor for the pitch indicator" design note: the picker's
	 * preview row uses `noteCount(id)` for the dotted-timeline dot
	 * row and this accessor for the leading-pitch indicator (a tiny
	 * piano-key glyph or a horizontal bar whose height tracks the
	 * first note's frequency), giving each row a third axis of
	 * differentiation beyond name + duration + note count -- crucial
	 * for the equal-length / equal-shape pip pairs in the catalogue
	 * (Notify is two NOTE_E6 pips, SmsReceived is two NOTE_G6 pips;
	 * `noteCount(id)` and `durationMs(id)` agree, the FIRST note's
	 * frequency is the only catalogued differentiator between them
	 * at construction time). A future `PhoneDiagScreen` "Sound test"
	 * entry that walks every chime in turn wants the same answer for
	 * the same reason -- it can show a per-chime "first pitch: 1318
	 * Hz" caption beside the row to confirm the engine handoff
	 * landed on the catalogued pitch and is the only catalogued
	 * differentiator between same-shape rows.
	 *
	 * Profile-state INDEPENDENT: the catalogued first-note frequency
	 * is the same on SILENT / MEETING profiles as on GENERAL /
	 * OUTDOOR / HEADSET, so a picker can render its pitch indicator
	 * at construction time and leave it unchanged when the user
	 * toggles profiles. The S231 `tryPlay(id)` gate already reports
	 * the silenced answer separately for any caller that wants to
	 * fade the indicator into a "(silenced)" caption.
	 *
	 * Distinct from `PhoneRingtoneEngine::currentFreq()` (the engine
	 * accessor S191 added for the equalizer visualiser): that helper
	 * reports the LIVE frequency the piezo is driving right now (0
	 * during rests, gaps and idle), the catalogue answer reports the
	 * FIRST catalogued note regardless of whether the engine is
	 * playing. Both are useful and live at different layers --
	 * neither subsumes the other.
	 *
	 * Cheap O(1) struct field read; no engine interaction, no
	 * persisted state, no per-call allocation. Header surface grows
	 * by exactly one public symbol (`static uint16_t firstFreqHz`);
	 * the cpp adds a single function next to the existing `count` /
	 * `valid` / `name` / `melody` / `play` / `tryPlay` /
	 * `isSilenced` / `durationMs` / `noteCount` cluster. No new
	 * includes (the `Melody` and `Note` types live behind the
	 * existing `PhoneRingtoneEngine.h` include and the `kMelodies`
	 * table already lives in this translation unit's anonymous
	 * namespace), no new const data, no new SPIFFS asset cost.
	 * Every existing call site of the catalogue keeps byte-identical
	 * behaviour -- the new helper is purely additive.
	 */
	static uint16_t firstFreqHz(uint8_t id);
	/**
	 * S235 - structural last-note pitch accessor for chime `id`.
	 * Returns the catalogued frequency in Hz of the LAST
	 * `PhoneRingtoneEngine::Note` entry in the underlying Melody
	 * (i.e. `kMelodies[id].notes[count - 1].freq`). Returns 0 for
	 * an out-of-range id, for the (currently impossible) empty-
	 * melody case, and -- transparently -- for the (currently
	 * impossible) trailing-rest case (a Note with `freq == 0` is
	 * the catalogue's encoding for a silent step; no v1 chime
	 * closes on a rest, so the answer collapses to "the catalogued
	 * last audible note's pitch" for every entry that ships today,
	 * while staying unambiguous if a future chime ever closes on a
	 * rest -- 0 is the same value the engine itself uses to mean
	 * "no tone is being driven right now").
	 *
	 * Foreshadowed by the S234 commit body's "rising / falling
	 * silhouette" framing: where `firstFreqHz(id)` reports the
	 * leading pitch (so the foreshadowed "Settings -> Sounds ->
	 * System chimes" picker can render a per-row pitch indicator
	 * for visual differentiation between equal-shape pip pairs
	 * like Notify [NOTE_E6, NOTE_E6] and SmsReceived [NOTE_G6,
	 * NOTE_G6]), `lastFreqHz(id)` reports the trailing pitch so
	 * the same picker can render an up / down / level direction
	 * arrow beside each row by comparing the two answers without
	 * walking the catalogued Note array at the call site:
	 *
	 *   - first < last -> ascending silhouette (Success
	 *     [NOTE_C6, NOTE_E6], Unlock [NOTE_C5, NOTE_G5], Save
	 *     [NOTE_C6, NOTE_E6, NOTE_G6], NetworkOk [NOTE_C6,
	 *     NOTE_F6], LevelUp [NOTE_C5, NOTE_E5, NOTE_G5, NOTE_C6],
	 *     AlarmDismiss [NOTE_E5, NOTE_G5]);
	 *   - first > last -> descending silhouette (Error [NOTE_F5,
	 *     NOTE_D5], Lock [NOTE_G5, NOTE_C5], CallEnd [NOTE_E6,
	 *     NOTE_C6, NOTE_A5], DeleteItem [NOTE_E6, NOTE_A5],
	 *     NetworkFail [NOTE_F6, NOTE_C6], LowBattery [NOTE_E5,
	 *     NOTE_D5, NOTE_C5]);
	 *   - first == last -> level silhouette (Notify [NOTE_E6,
	 *     NOTE_E6], Alert [NOTE_A6], SmsReceived [NOTE_G6,
	 *     NOTE_G6], MenuOpen [NOTE_E6], MenuClose [NOTE_C6],
	 *     TimerDone [NOTE_C6, NOTE_C6, NOTE_E6] -- starts and
	 *     ends differ but the catalogue's first==last collapses
	 *     to "level" for the trailing-pitch comparison the picker
	 *     wants).
	 *
	 * That trio of categorisations is exactly the silhouette
	 * grouping `PhoneSystemTones.cpp` documents at the top of the
	 * file ("Positive cues ascend", "Negative cues descend",
	 * "Equal-pitch pip pairs cue something arrived without
	 * picking a direction") -- `firstFreqHz(id)` + `lastFreqHz(id)`
	 * is the smallest accessor pair that lets a picker reproduce
	 * the same grouping at construction time without re-deriving
	 * it from the catalogued Note array. A future
	 * `PhoneDiagScreen` "Sound test" entry that walks every chime
	 * in turn wants the same answer for the same reason -- it
	 * can show a per-chime "first 1318 Hz -> last 1568 Hz" caption
	 * beside the row to confirm the engine handoff landed on the
	 * catalogued endpoints.
	 *
	 * Profile-state INDEPENDENT: the catalogued last-note frequency
	 * is the same on SILENT / MEETING profiles as on GENERAL /
	 * OUTDOOR / HEADSET, so a picker can render its direction
	 * arrow at construction time and leave it unchanged when the
	 * user toggles profiles. The S231 `tryPlay(id)` gate already
	 * reports the silenced answer separately for any caller that
	 * wants to fade the arrow into a "(silenced)" caption.
	 *
	 * Distinct from `PhoneRingtoneEngine::currentFreq()` (the S191
	 * live-piezo accessor): that helper reports the LIVE frequency
	 * the engine is driving right now (0 during rests, gaps and
	 * idle), the catalogue answer reports the LAST catalogued note
	 * regardless of whether the engine is playing. Both are useful
	 * and live at different layers -- neither subsumes the other.
	 *
	 * Cheap O(1) array-tail field read; no engine interaction, no
	 * persisted state, no per-call allocation. Header surface grows
	 * by exactly one public symbol (`static uint16_t lastFreqHz`);
	 * the cpp adds a single function next to the existing `count` /
	 * `valid` / `name` / `melody` / `play` / `tryPlay` /
	 * `isSilenced` / `durationMs` / `noteCount` / `firstFreqHz`
	 * cluster. No new includes (the `Melody` and `Note` types
	 * live behind the existing `PhoneRingtoneEngine.h` include and
	 * the `kMelodies` table already lives in this translation
	 * unit's anonymous namespace), no new const data, no new
	 * SPIFFS asset cost. Every existing call site of the
	 * catalogue keeps byte-identical behaviour -- the new helper
	 * is purely additive.
	 */
	static uint16_t lastFreqHz(uint8_t id);

	/**
	 * S236 - structural inter-note gap accessor for chime `id`.
	 * Returns the catalogued `gapMs` field of the underlying
	 * `PhoneRingtoneEngine::Melody` (i.e. `kMelodies[id].gapMs`),
	 * which is the wait the engine's `loop()` interleaves between
	 * consecutive notes when stepping through the melody. Returns 0
	 * for an out-of-range id (which happens to be the same value the
	 * engine itself uses to mean "no inter-note silence" -- the
	 * Alert / MenuOpen / MenuClose entries already ship with
	 * `gapMs = 0` because they are single-note pulses, so a 0 answer
	 * for an invalid id is indistinguishable from the catalogued
	 * single-pulse entries and the caller's code path collapses
	 * cleanly).
	 *
	 * Foreshadowed by the S232 / S233 / S234 / S235 commit bodies'
	 * "future Settings -> Sounds -> System chimes picker" and
	 * "future `PhoneDiagScreen` Sound test entry that walks every
	 * chime in turn" design notes. The picker's preview row already
	 * pairs `noteCount(id)` (S233) with `durationMs(id)` (S232) for
	 * the "(N notes, M ms)" caption and `firstFreqHz(id)` (S234)
	 * with `lastFreqHz(id)` (S235) for the rising / falling / level
	 * direction arrow; `gapMs(id)` closes the structural-field set
	 * by exposing the third and final catalogued field on the Melody
	 * struct that had not yet surfaced at the accessor layer
	 * (`notes` is exposed via `firstFreqHz` / `lastFreqHz`, `count`
	 * is exposed via `noteCount`, the `loop` flag is moot for the
	 * v1 catalogue -- no system chime loops, looping is reserved
	 * for the call ringer family -- `name` is exposed via `name`,
	 * and `gapMs` was the remaining invisible field). The picker's
	 * preview row can use `gapMs(id)` to render a per-row "tempo"
	 * indicator -- e.g. a tiny dotted timeline whose dots are
	 * spaced by the catalogued gap value rather than evenly --
	 * giving cues like Notify (35 ms gap), SmsReceived (50 ms gap)
	 * and TimerDone (60 ms gap) a third axis of visual
	 * differentiation beyond name + duration + note count + pitch
	 * silhouette. The diag screen's "Sound test" entry can show a
	 * per-chime "gap: 35 ms" caption beside the row to confirm
	 * the engine handoff is honouring the catalogued spacing.
	 *
	 * Distinct from `durationMs(id)` (S232): that helper reports
	 * the TOTAL playback duration including the catalogued gaps
	 * (sum of per-Note `durationMs` plus `count * gapMs` when
	 * `gapMs > 0`), while `gapMs(id)` reports the per-step gap
	 * value as a structural field. A caller that wants to walk
	 * the catalogued cue with its own LovyanGFX-side per-note
	 * pulse animation can pair `noteCount(id)` + `firstFreqHz(id)`
	 * / `lastFreqHz(id)` + `gapMs(id)` to reproduce the engine's
	 * note-by-note rhythm without registering a LoopManager
	 * listener of its own (the S232 `durationMs(id)` helper still
	 * answers the simpler "schedule the next row press this many
	 * ms from now" question for the picker's debounce).
	 *
	 * Profile-state INDEPENDENT: the catalogued gap is the same
	 * on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
	 * HEADSET, so a picker can lay out its tempo indicator at
	 * construction time and leave it unchanged when the user
	 * toggles profiles. The S231 `tryPlay(id)` gate already
	 * reports the silenced answer separately for any caller that
	 * wants to fade the tempo indicator into a "(silenced)"
	 * caption.
	 *
	 * Cheap O(1) struct field read; no engine interaction, no
	 * persisted state, no per-call allocation. Header surface
	 * grows by exactly one public symbol
	 * (`static uint16_t gapMs`); the cpp adds a single function
	 * next to the existing `count` / `valid` / `name` / `melody`
	 * / `play` / `tryPlay` / `isSilenced` / `durationMs` /
	 * `noteCount` / `firstFreqHz` / `lastFreqHz` cluster. No new
	 * includes (the `Melody` type lives behind the existing
	 * `PhoneRingtoneEngine.h` include and the `kMelodies` table
	 * already lives in this translation unit's anonymous
	 * namespace), no new const data, no new SPIFFS asset cost.
	 * Every existing call site of the catalogue keeps byte-
	 * identical behaviour -- the new helper is purely additive.
	 */
	static uint16_t gapMs(uint8_t id);
};

#endif // MAKERPHONE_PHONESYSTEMTONES_H
