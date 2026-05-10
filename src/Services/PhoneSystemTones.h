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

	/**
	 * S237 - structural loop-flag accessor for chime `id`. Returns the
	 * catalogued `loop` field of the underlying
	 * `PhoneRingtoneEngine::Melody` (i.e. `kMelodies[id].loop`), the
	 * boolean the engine consults at the end of one playthrough to
	 * decide whether to restart the melody from step 0 (loop the cue
	 * indefinitely) or call `stop()` and release the LoopManager
	 * listener (one-shot cue). Returns `false` for an out-of-range
	 * id, which is the same answer a non-existent chime would
	 * naturally give (a no-op cannot loop) and matches the catalogued
	 * answer for every v1 entry today -- no system chime loops, by
	 * design (looping is reserved for the call-ringer family in
	 * `PhoneRingtones.cpp`, where the engine's loop semantics are
	 * actually wanted; the eighteen v1 entries here are short cues
	 * that fire once per UI event and stop).
	 *
	 * S236 closed by noting that the `loop` field of the Melody
	 * struct (one of the five catalogued fields: `notes`, `count`,
	 * `gapMs`, `loop`, `name`) had been left without an accessor of
	 * its own because no v1 chime opted into looping and so the
	 * field's answer was uniformly false across the catalogue. S237
	 * promotes that deferred field to first-class accessor parity
	 * with the rest of the structural surface for two concrete
	 * reasons:
	 *
	 *   - the foreshadowed "Settings -> Sounds -> System chimes"
	 *     picker now has the FULL Melody-struct field set behind
	 *     dedicated accessors -- `noteCount(id)` (S233) for `count`,
	 *     `firstFreqHz(id)` / `lastFreqHz(id)` (S234 / S235) for the
	 *     leading and trailing entries of `notes`, `gapMs(id)`
	 *     (S236) for `gapMs`, `name(id)` (S192) for `name`, and now
	 *     `loops(id)` (S237) for `loop`. Picker preview rows can
	 *     introspect the catalogue without ever dereferencing the
	 *     const Melody* pointer at the call site, which is the
	 *     design property that S233-S236 had been incrementally
	 *     building toward.
	 *
	 *   - the foreshadowed `PhoneDiagScreen` "Sound test" entry that
	 *     walks every chime in turn (foreshadowed in S231 / S232 /
	 *     S233 / S234 / S235 / S236 commit bodies) wants to schedule
	 *     the row-press debounce by `durationMs(id)` -- the catalogued
	 *     length of one playthrough -- but ONLY if the chime
	 *     completes after one playthrough. A future catalogue entry
	 *     that opts into looping would never complete on its own and
	 *     the diag walk would hang waiting for a stop() that never
	 *     arrives. With `loops(id)` exposed, the diag walk can fall
	 *     back to a fixed preview window (e.g. 600 ms) for looping
	 *     entries and the natural `durationMs(id)` window for one-
	 *     shot entries, without re-deriving the answer from the
	 *     const Melody* pointer at the call site.
	 *
	 * Distinct from `PhoneRingtoneEngine::isLooping()` (a hypothetical
	 * live-engine accessor): even if such a helper existed it would
	 * report whether the engine is CURRENTLY in a looping playback
	 * state, while the catalogue answer reports whether the
	 * underlying Melody opted into looping at construction time
	 * regardless of whether the engine is playing. Both are useful
	 * and live at different layers; the catalogue answer is the one
	 * the picker / diag walk wants because it lets them lay out
	 * their UI at construction time and leave it unchanged across
	 * profile toggles and engine state transitions.
	 *
	 * Profile-state INDEPENDENT: the catalogued loop flag is the
	 * same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
	 * HEADSET, so the foreshadowed picker can render its "loops"
	 * indicator at construction time and leave it unchanged when
	 * the user toggles profiles (the S231 `tryPlay(id)` gate
	 * already reports the silenced answer separately for any caller
	 * that wants to fade the indicator into a "(silenced)" caption).
	 *
	 * Cheap O(1) struct field read; no engine interaction, no
	 * persisted state, no per-call allocation. Header surface grows
	 * by exactly one public symbol (`static bool loops`); the cpp
	 * adds a single function next to the existing `count` /
	 * `valid` / `name` / `melody` / `play` / `tryPlay` /
	 * `isSilenced` / `durationMs` / `noteCount` / `firstFreqHz` /
	 * `lastFreqHz` / `gapMs` cluster. No new includes (the `Melody`
	 * type lives behind the existing `PhoneRingtoneEngine.h`
	 * include and the `kMelodies` table already lives in this
	 * translation unit's anonymous namespace), no new const data,
	 * no new SPIFFS asset cost. Every existing call site of the
	 * catalogue keeps byte-identical behaviour -- the new helper is
	 * purely additive.
	 */
	static bool loops(uint8_t id);

	/**
	 * S238 - derived ascending / level / descending pitch-direction
	 * accessor for chime `id`. Returns the catalogued silhouette of
	 * the underlying `PhoneRingtoneEngine::Melody` as an `int8_t`:
	 *
	 *   - `+1` if the catalogued first note's pitch is LOWER than the
	 *     catalogued last note's pitch (ascending silhouette --
	 *     Success, Unlock, Save, NetworkOk, LevelUp, AlarmDismiss,
	 *     TimerDone today);
	 *   - `-1` if the catalogued first note's pitch is HIGHER than the
	 *     catalogued last note's pitch (descending silhouette -- Error,
	 *     Lock, CallEnd, DeleteItem, NetworkFail, LowBattery today);
	 *   - `0` otherwise (level silhouette: same first / last pitch --
	 *     Notify, Alert, SmsReceived, MenuOpen, MenuClose today;
	 *     also the answer for an out-of-range id, which collapses
	 *     cleanly to the level case so a non-existent chime is
	 *     indistinguishable from a level entry at the call site and
	 *     a picker rendering a direction arrow does not need to
	 *     special-case the invalid id path).
	 *
	 * Foreshadowed by the S234 / S235 commit bodies' "rising / falling
	 * silhouette" framing -- where `firstFreqHz(id)` (S234) reports the
	 * leading pitch and `lastFreqHz(id)` (S235) reports the trailing
	 * pitch, the foreshadowed "Settings -> Sounds -> System chimes"
	 * picker uses the comparison `firstFreqHz(id)` vs `lastFreqHz(id)`
	 * to render an up / down / level direction arrow beside each row.
	 * S238 promotes that caller-side arithmetic step ("every picker /
	 * diag-walk caller would have to spell out `firstFreqHz(id) <
	 * lastFreqHz(id) ? 1 : firstFreqHz(id) > lastFreqHz(id) ? -1 : 0`")
	 * to a dedicated derived accessor that returns the catalogued
	 * direction in one call. Mirrors the S232 `durationMs(id)`
	 * precedent of exposing a derived answer (a sum over the catalogued
	 * Note array) rather than a raw struct field where the derived
	 * form is the one the caller actually wants -- here the picker /
	 * diag walk wants the silhouette, not two raw frequencies plus
	 * the comparison arithmetic.
	 *
	 * The trio of categorisations matches the silhouette grouping
	 * `PhoneSystemTones.cpp` already documents at the top of its file:
	 * "Positive cues ascend / Negative cues descend / Equal-pitch pip
	 * pairs cue something arrived without picking a direction". The
	 * accessor reproduces that grouping at the API layer so a picker
	 * can colour-code its row labels by silhouette (green for
	 * ascending positives, red for descending negatives, amber for
	 * level pips) without re-deriving the answer from the catalogued
	 * Note array, and a future `PhoneDiagScreen` "Sound test" entry
	 * that walks every chime in turn can do the same colouring at the
	 * diag layer.
	 *
	 * Distinct from `firstFreqHz(id)` / `lastFreqHz(id)` themselves:
	 * those helpers stay on the header so a caller that wants the
	 * absolute pitch values (e.g. a per-row "1318 Hz -> 1568 Hz"
	 * caption) can still read them directly. The two layers coexist
	 * by design -- the structural accessors expose the raw catalogued
	 * field values, the derived accessor exposes the semantic
	 * relationship between them.
	 *
	 * Profile-state INDEPENDENT: the catalogued silhouette is the
	 * same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
	 * HEADSET, so the foreshadowed picker can lay out its direction
	 * arrow at construction time and leave it unchanged when the user
	 * toggles profiles (the S231 `tryPlay(id)` gate already reports
	 * the silenced answer separately for any caller that wants to
	 * fade the arrow into a "(silenced)" caption).
	 *
	 * Cheap O(1) two-field comparison; no engine interaction, no
	 * persisted state, no per-call allocation. Header surface grows
	 * by exactly one public symbol (`static int8_t silhouette`); the
	 * cpp adds a single function next to the existing `count` /
	 * `valid` / `name` / `melody` / `play` / `tryPlay` /
	 * `isSilenced` / `durationMs` / `noteCount` / `firstFreqHz` /
	 * `lastFreqHz` / `gapMs` / `loops` cluster. No new includes (the
	 * `Melody` and `Note` types live behind the existing
	 * `PhoneRingtoneEngine.h` include and the `kMelodies` table
	 * already lives in this translation unit's anonymous namespace),
	 * no new const data, no new SPIFFS asset cost. Every existing
	 * call site of the catalogue keeps byte-identical behaviour --
	 * the new helper is purely additive.
	 */
	static int8_t silhouette(uint8_t id);

	/**
	 * S239 - derived pitch-span accessor for chime `id`. Returns the
	 * absolute frequency interval, in Hz, between the catalogued first
	 * note and the catalogued last note of the underlying
	 * `PhoneRingtoneEngine::Melody` -- i.e. `|firstFreqHz(id) -
	 * lastFreqHz(id)|`. Returns 0 for an out-of-range id, for the
	 * (currently impossible) empty-melody case, and for every level
	 * silhouette in the catalogue (Notify, Alert, SmsReceived,
	 * MenuOpen, MenuClose -- where `firstFreqHz(id) == lastFreqHz(id)`
	 * by construction). Returns the unsigned magnitude of the catalogued
	 * interval for every ascending or descending silhouette regardless
	 * of direction (Unlock and Lock both report ~131 Hz -- a perfect
	 * fifth from NOTE_C5 to NOTE_G5; NetworkOk and NetworkFail both
	 * report ~350 Hz -- a perfect fourth from NOTE_C6 to NOTE_F6;
	 * Success and Error report different magnitudes because they use
	 * different intervals).
	 *
	 * Foreshadowed by the S238 commit body's "direction is one half of
	 * the silhouette, magnitude is the other" framing. Where S238
	 * returns the SIGN of the catalogued first / last comparison
	 * (+1 ascending / 0 level / -1 descending), S239 returns the
	 * MAGNITUDE of the same comparison. The two derived accessors
	 * together describe the catalogued silhouette completely without
	 * either subsuming the other: a caller that wants only the
	 * direction (e.g. picking a colour or an arrow glyph for a row)
	 * stays on `silhouette(id)`, a caller that wants only the
	 * magnitude (e.g. driving the height of a per-row pitch-bar
	 * indicator or the curve sharpness of a per-row sparkline) stays
	 * on `pitchSpanHz(id)`, and a caller that wants both reads them
	 * separately rather than re-deriving one from the other. Mirrors
	 * the S232 `durationMs(id)` precedent of exposing a derived answer
	 * (the sum over the catalogued Note array plus interleaved gaps)
	 * rather than a raw struct field where the derived form is the
	 * one the caller actually wants -- here the picker / diag walk
	 * wants the catalogued interval magnitude, not two raw frequencies
	 * plus the absolute-difference arithmetic.
	 *
	 * Concretely the foreshadowed "Settings -> Sounds -> System chimes"
	 * picker can use `pitchSpanHz(id)` to render a per-row pitch-bar
	 * indicator whose width / height tracks the catalogued span:
	 * single-note pulses (Alert, MenuOpen, MenuClose) and equal-pip
	 * pairs (Notify, SmsReceived) collapse to a flat tick (span 0 Hz),
	 * narrow intervals render as small rises / falls, dramatic
	 * intervals (LevelUp's full octave from NOTE_C5 to NOTE_C6 is
	 * ~262 Hz between first and last, NetworkOk's perfect fourth at
	 * ~350 Hz, Save's major sixth at ~523 Hz between NOTE_C6 and
	 * NOTE_G6) render as tall rises / falls. The picker pairs the
	 * bar's TILT (from S238 `silhouette(id)`) with its HEIGHT (from
	 * this accessor) to give the user a glanceable visual abstraction
	 * of the catalogued cue without registering a LoopManager listener
	 * of its own. The foreshadowed `PhoneDiagScreen` "Sound test"
	 * entry can use the same accessor to add a per-row
	 * "span: 350 Hz" caption beside the row to confirm the engine
	 * handoff landed on the catalogued endpoints (the absolute
	 * difference is the smallest single number that summarises the
	 * catalogued first / last pair).
	 *
	 * Distinct from `firstFreqHz(id)` / `lastFreqHz(id)` themselves:
	 * those helpers stay on the header so a caller that wants the
	 * absolute pitch values (e.g. a per-row "1318 Hz -> 1568 Hz"
	 * caption) can still read them directly. Distinct from
	 * `silhouette(id)` (S238): that helper reports the direction of
	 * the catalogued span, this helper reports the magnitude. The
	 * three accessors coexist by design -- structural pair exposes
	 * the raw catalogued endpoints, derived sign helper exposes the
	 * direction of the comparison, derived magnitude helper exposes
	 * the size of the comparison.
	 *
	 * Profile-state INDEPENDENT: the catalogued pitch span is the
	 * same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
	 * HEADSET, so the foreshadowed picker can lay out its pitch-bar
	 * height at construction time and leave it unchanged when the
	 * user toggles profiles (the S231 `tryPlay(id)` gate already
	 * reports the silenced answer separately for any caller that
	 * wants to fade the bar into a "(silenced)" caption).
	 *
	 * Cheap O(1) two-field absolute-difference; no engine
	 * interaction, no persisted state, no per-call allocation. Header
	 * surface grows by exactly one public symbol (`static uint16_t
	 * pitchSpanHz`); the cpp adds a single function next to the
	 * existing `count` / `valid` / `name` / `melody` / `play` /
	 * `tryPlay` / `isSilenced` / `durationMs` / `noteCount` /
	 * `firstFreqHz` / `lastFreqHz` / `gapMs` / `loops` /
	 * `silhouette` cluster. No new includes (the `Melody` and `Note`
	 * types live behind the existing `PhoneRingtoneEngine.h` include
	 * and the `kMelodies` table already lives in this translation
	 * unit's anonymous namespace), no new const data, no new SPIFFS
	 * asset cost. Every existing call site of the catalogue keeps
	 * byte-identical behaviour -- the new helper is purely additive.
	 */
	static uint16_t pitchSpanHz(uint8_t id);

	/**
	 * S240 - derived catalogue-wide maximum-pitch accessor for chime
	 * `id`. Returns the highest catalogued frequency, in Hz, across
	 * every `PhoneRingtoneEngine::Note` entry in the underlying
	 * Melody -- i.e. `max(kMelodies[id].notes[i].freq)` for `i` in
	 * `[0..count-1]`, skipping rest-encoded `freq == 0` entries so a
	 * future leading / trailing / interior rest does not collapse
	 * the answer to zero by accident. Returns 0 for an out-of-range
	 * id, for the (currently impossible) empty-melody case, and for
	 * the (currently impossible) all-rests-melody case.
	 *
	 * Foreshadowed by the S234 / S235 / S238 / S239 commit bodies'
	 * progressive build-up of the catalogue pitch surface: where
	 * `firstFreqHz(id)` (S234) and `lastFreqHz(id)` (S235) expose
	 * the catalogued endpoints and `silhouette(id)` (S238) /
	 * `pitchSpanHz(id)` (S239) expose the relationship between those
	 * endpoints, S240 exposes the GLOBAL maximum the cue reaches at
	 * any step. For every monotonic melody in the v1 catalogue
	 * (Success ascends to NOTE_E6, Save ascends to NOTE_G6, Unlock
	 * ascends to NOTE_G5, etc.) the answer agrees with whichever
	 * endpoint `silhouette(id)` points at; for non-monotonic future
	 * entries (a rising-then-falling siren, a "ka-ching" with an
	 * interior peak, etc.) the answer reports the catalogued
	 * ceiling regardless of which step it lands on, which is the
	 * visually-meaningful one for the picker / diag walk. TimerDone
	 * [NOTE_C6, NOTE_C6, NOTE_E6] is level by silhouette
	 * (first==last==NOTE_C6) but its peak NOTE_E6 sits above either
	 * endpoint, so the catalogued ceiling is the only catalogued
	 * differentiator between TimerDone and the genuinely-level pip
	 * pairs (Notify, SmsReceived) at the picker layer.
	 *
	 * The foreshadowed "Settings -> Sounds -> System chimes" picker
	 * can pair the bar's TILT (silhouette), HEIGHT (pitchSpanHz) and
	 * CEILING (peakFreqHz) to render a per-row pitch bar whose top
	 * traces the catalogued maximum -- a glanceable visual
	 * abstraction of the catalogued cue without registering a
	 * LoopManager listener of its own. The foreshadowed
	 * `PhoneDiagScreen` "Sound test" entry can use the same accessor
	 * to show a per-chime "peak: 1318 Hz" caption beside the row to
	 * confirm the engine handoff is honouring the catalogued
	 * ceiling.
	 *
	 * Distinct from `firstFreqHz(id)` / `lastFreqHz(id)`: those
	 * helpers report catalogued ENDPOINTS, S240 reports the
	 * catalogued CEILING regardless of step. Distinct from
	 * `pitchSpanHz(id)` (S239): that helper reports the absolute
	 * difference between catalogued endpoints, S240 reports the
	 * absolute maximum across every step. Distinct from
	 * `PhoneRingtoneEngine::currentFreq()` (the S191 live-piezo
	 * accessor): that helper reports the LIVE frequency the engine
	 * is driving right now (0 during rests, gaps and idle), the
	 * catalogue answer reports the catalogued ceiling regardless of
	 * whether the engine is playing.
	 *
	 * Profile-state INDEPENDENT: the catalogued peak is the same on
	 * SILENT / MEETING profiles as on GENERAL / OUTDOOR / HEADSET,
	 * so the picker can lay out its bar ceiling at construction
	 * time and leave it unchanged when the user toggles profiles
	 * (the S231 `tryPlay(id)` gate already reports the silenced
	 * answer separately for any caller that wants to fade the bar
	 * into a "(silenced)" caption).
	 *
	 * Cheap O(notes) linear scan with a uint16_t accumulator; no
	 * engine interaction, no persisted state, no per-call
	 * allocation. Header surface grows by exactly one public symbol
	 * (`static uint16_t peakFreqHz`); the cpp adds a single
	 * function next to the existing `count` / `valid` / `name` /
	 * `melody` / `play` / `tryPlay` / `isSilenced` / `durationMs` /
	 * `noteCount` / `firstFreqHz` / `lastFreqHz` / `gapMs` /
	 * `loops` / `silhouette` / `pitchSpanHz` cluster. No new
	 * includes (the `Melody` and `Note` types live behind the
	 * existing `PhoneRingtoneEngine.h` include and the `kMelodies`
	 * table already lives in this translation unit's anonymous
	 * namespace), no new const data, no new SPIFFS asset cost.
	 * Every existing call site of the catalogue keeps byte-
	 * identical behaviour -- the new helper is purely additive.
	 */
	static uint16_t peakFreqHz(uint8_t id);

	/**
	 * S241 - derived catalogue-wide minimum-pitch accessor for chime
	 * `id`. Returns the lowest catalogued audible frequency, in Hz,
	 * across every `PhoneRingtoneEngine::Note` entry in the underlying
	 * Melody -- i.e. `min(kMelodies[id].notes[i].freq)` for `i` in
	 * `[0..count-1]`, ignoring rest-encoded `freq == 0` entries (a
	 * Note with `freq == 0` is the catalogue's encoding for a silent
	 * step; folding rests into the minimum would collapse every melody
	 * containing one to zero, which is not the visually-meaningful
	 * answer for the picker / diag walk -- the meaningful answer is
	 * the catalogued FLOOR of the audible pitches, exactly mirroring
	 * the way `peakFreqHz(id)` (S240) skips rests when computing the
	 * catalogued CEILING). Returns 0 for an out-of-range id, for the
	 * (currently impossible) empty-melody case, and for the (currently
	 * impossible) all-rests-melody case -- the same three "no answer"
	 * cases S240 already collapses to 0.
	 *
	 * Foreshadowed by the S240 commit body's "S240 exposes the GLOBAL
	 * maximum the cue reaches at any step" framing: where
	 * `peakFreqHz(id)` (S240) reports the catalogued CEILING across
	 * every step, S241 reports the catalogued FLOOR across every step.
	 * The two derived accessors together describe the catalogued
	 * pitch envelope completely without either subsuming the other --
	 * a caller that wants only the ceiling (e.g. driving the top of a
	 * per-row pitch bar in the foreshadowed "Settings -> Sounds ->
	 * System chimes" picker) stays on `peakFreqHz(id)`, a caller that
	 * wants only the floor (e.g. driving the bottom of the same bar)
	 * stays on `troughFreqHz(id)`, and a caller that wants both reads
	 * them separately rather than re-deriving one from the other.
	 * Mirrors the S238 / S239 (silhouette / pitchSpanHz) precedent of
	 * shipping the two halves of a catalogued shape as separate
	 * accessors so each call site can pick exactly the half it wants
	 * without paying for the other.
	 *
	 * For every monotonic melody in the v1 catalogue (Success ascends
	 * from NOTE_C6, Save ascends from NOTE_C6, Unlock ascends from
	 * NOTE_C5, Error descends to NOTE_D5, Lock descends to NOTE_C5,
	 * LowBattery descends to NOTE_C5, etc.) the trough answer agrees
	 * with whichever endpoint sits BELOW the other -- the opposite
	 * endpoint of `peakFreqHz(id)` for monotonic shapes. For non-
	 * monotonic future entries (a fall-then-rise valley, a "u-turn"
	 * with an interior low, etc.) the answer reports the catalogued
	 * floor regardless of which step it lands on, which is the
	 * visually-meaningful one for the picker / diag walk. The
	 * pitch-bar abstraction the foreshadowed picker renders -- TILT
	 * (silhouette), HEIGHT (pitchSpanHz), CEILING (peakFreqHz),
	 * FLOOR (troughFreqHz) -- now has all four catalogued anchor
	 * points exposed at the API layer, so the picker can lay out the
	 * bar's bottom edge at construction time without re-deriving the
	 * catalogued floor from the const Melody* pointer at the call site.
	 *
	 * The pair `peakFreqHz(id) - troughFreqHz(id)` answers a question
	 * neither helper alone can -- the GLOBAL pitch span across every
	 * step in the melody (as opposed to S239 `pitchSpanHz(id)` which
	 * answers the ENDPOINT pitch span between the catalogued first
	 * and last). For monotonic melodies the two questions agree; for
	 * non-monotonic future entries they diverge -- TimerDone's level
	 * silhouette has endpoint span 0 Hz but global span ~262 Hz (the
	 * NOTE_E6 peak above the NOTE_C6 endpoints), and a future
	 * fall-then-rise siren would have its endpoint span and global
	 * span agree only at the trough. The picker can render BOTH spans
	 * (endpoint span as a horizontal bar, global span as the bar's
	 * full height) to cue the user that the chime visits pitches
	 * outside its endpoints. The foreshadowed `PhoneDiagScreen`
	 * "Sound test" entry can show a per-chime "trough: 1047 Hz"
	 * caption beside the row to confirm the engine handoff is
	 * honouring the catalogued floor.
	 *
	 * Distinct from `firstFreqHz(id)` / `lastFreqHz(id)`: those
	 * helpers report catalogued ENDPOINTS, S241 reports the
	 * catalogued FLOOR regardless of step. Distinct from
	 * `pitchSpanHz(id)` (S239): that helper reports the absolute
	 * difference between catalogued endpoints, S241 reports the
	 * absolute minimum across every step. Distinct from
	 * `peakFreqHz(id)` (S240): that helper reports the catalogued
	 * ceiling, S241 reports the catalogued floor. Distinct from
	 * `PhoneRingtoneEngine::currentFreq()` (the S191 live-piezo
	 * accessor): that helper reports the LIVE frequency the engine
	 * is driving right now (0 during rests, gaps and idle), the
	 * catalogue answer reports the catalogued floor regardless of
	 * whether the engine is playing.
	 *
	 * Profile-state INDEPENDENT: the catalogued trough is the same
	 * on SILENT / MEETING profiles as on GENERAL / OUTDOOR / HEADSET,
	 * so the picker can lay out its bar floor at construction time
	 * and leave it unchanged when the user toggles profiles (the
	 * S231 `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the bar into a
	 * "(silenced)" caption).
	 *
	 * Cheap O(notes) linear scan with a uint16_t accumulator (the
	 * mirror of S240's loop, with `<` substituted for `>` and an
	 * "uninitialised-yet" sentinel because there is no natural
	 * starting value for a min-search across uint16_t). No engine
	 * interaction, no persisted state, no per-call allocation. Header
	 * surface grows by exactly one public symbol (`static uint16_t
	 * troughFreqHz`); the cpp adds a single function next to the
	 * existing `count` / `valid` / `name` / `melody` / `play` /
	 * `tryPlay` / `isSilenced` / `durationMs` / `noteCount` /
	 * `firstFreqHz` / `lastFreqHz` / `gapMs` / `loops` /
	 * `silhouette` / `pitchSpanHz` / `peakFreqHz` cluster. No new
	 * includes (the `Melody` and `Note` types live behind the
	 * existing `PhoneRingtoneEngine.h` include and the `kMelodies`
	 * table already lives in this translation unit's anonymous
	 * namespace), no new const data, no new SPIFFS asset cost. Every
	 * existing call site of the catalogue keeps byte-identical
	 * behaviour -- the new helper is purely additive.
	 */
	static uint16_t troughFreqHz(uint8_t id);

	/**
	 * S242 -- Derived catalogue-wide arithmetic-mean pitch accessor
	 * for chime `id`. Returns the integer-rounded average of every
	 * audible (non-rest) catalogued frequency in the underlying
	 * Melody, in Hz:
	 *
	 *     meanFreqHz(id) = round(sum(notes[i].freq for non-rest i) /
	 *                            count_of_non_rest_notes)
	 *
	 * Rest-encoded `freq == 0` entries are skipped so a future
	 * leading / trailing / interior rest does not pull the mean
	 * toward zero by accident -- exactly mirroring the rest-skipping
	 * rule S240 (peakFreqHz) and S241 (troughFreqHz) already use for
	 * their catalogued ceiling and floor scans.
	 *
	 * Returns 0 for an out-of-range id, for the (currently
	 * impossible) empty-melody case, and for the (currently
	 * impossible) all-rests-melody case -- the same three "no
	 * answer" cases S240 / S241 already collapse to 0, so the picker
	 * / diag walk does not have to special-case "no answer" three
	 * different ways across the catalogued-pitch trio.
	 *
	 * Where `peakFreqHz(id)` (S240) reports the catalogued CEILING
	 * across every step and `troughFreqHz(id)` (S241) reports the
	 * catalogued FLOOR across every step, S242 reports the
	 * catalogued CENTRE -- the arithmetic mean of every audible
	 * step. The trio (CEILING, FLOOR, CENTRE) describes the
	 * catalogued pitch envelope's vertical anchors completely
	 * without any helper subsuming the others -- a caller that
	 * wants only the ceiling stays on `peakFreqHz(id)`, a caller
	 * that wants only the floor stays on `troughFreqHz(id)`, and a
	 * caller that wants only the centre stays on `meanFreqHz(id)`
	 * rather than re-deriving any of the three from a const Melody*
	 * pointer at the call site. For a strictly monotonic melody
	 * with `n` evenly-spaced steps the mean lands halfway between
	 * the catalogued first and last; for non-monotonic future
	 * entries (a fall-then-rise valley, an up-down-up "wave", a
	 * peak-and-return cue, etc.) the mean weights every step
	 * equally and so does NOT in general agree with the midpoint
	 * between `peakFreqHz(id)` and `troughFreqHz(id)`. That makes
	 * the mean a third independent answer -- the catalogued
	 * "average pitch the cue spends time near" -- complementing the
	 * pair of extremes already shipped.
	 *
	 * The pitch-bar abstraction the foreshadowed "Settings -> Sounds
	 * -> System chimes" picker renders -- TILT (S238 silhouette),
	 * HEIGHT (S239 pitchSpanHz), CEILING (S240 peakFreqHz), FLOOR
	 * (S241 troughFreqHz), CENTRE (S242 meanFreqHz) -- now has all
	 * five catalogued anchor points exposed at the API layer, so
	 * the picker can paint a third tick mark (the catalogued mean)
	 * inside the bar between the floor and the ceiling at
	 * construction time without re-walking the const Melody*
	 * pointer at the call site. The foreshadowed `PhoneDiagScreen`
	 * "Sound test" entry can show a per-chime "mean: 1175 Hz"
	 * caption beside the existing "trough: 1047 Hz" / "peak: 1319
	 * Hz" captions to confirm the engine handoff is honouring the
	 * catalogued centre as well as the catalogued extremes. A
	 * future "loudness curve" calibration helper that wants a
	 * single representative pitch per chime (rather than the pair
	 * of extremes) reads `meanFreqHz(id)` once at boot and feeds
	 * the result into its A-weighting table without ever opening
	 * the catalogue's const data table.
	 *
	 * Distinct from `firstFreqHz(id)` / `lastFreqHz(id)`
	 * (catalogued endpoints), distinct from `pitchSpanHz(id)` (S239
	 * -- absolute difference between catalogued endpoints), distinct
	 * from `peakFreqHz(id)` (S240 -- catalogued ceiling), distinct
	 * from `troughFreqHz(id)` (S241 -- catalogued floor), and
	 * distinct from `PhoneRingtoneEngine::currentFreq()` (the S191
	 * live-piezo accessor). Profile-state INDEPENDENT: the
	 * catalogued mean is the same on SILENT / MEETING profiles as
	 * on GENERAL / OUTDOOR / HEADSET, so the picker can lay out its
	 * centre tick at construction time and leave it unchanged when
	 * the user toggles profiles (the S231 `tryPlay(id)` gate
	 * already reports the silenced answer separately for any caller
	 * that wants to fade the bar into a "(silenced)" caption).
	 *
	 * Cheap O(notes) linear scan with a uint32_t accumulator (so
	 * the running sum cannot overflow even on a hypothetical 65535
	 * x ~13000 Hz worst case) and a uint16_t non-rest counter; the
	 * final integer-rounded division uses the standard +half-divisor
	 * trick to avoid pulling in <math.h>. No engine interaction,
	 * no persisted state, no per-call allocation. Header surface
	 * grows by exactly one public symbol (`static uint16_t
	 * meanFreqHz`); the cpp adds a single function next to the
	 * existing `count` / `valid` / `name` / `melody` / `play` /
	 * `tryPlay` / `isSilenced` / `durationMs` / `noteCount` /
	 * `firstFreqHz` / `lastFreqHz` / `gapMs` / `loops` /
	 * `silhouette` / `pitchSpanHz` / `peakFreqHz` / `troughFreqHz`
	 * cluster. No new includes (the `Melody` and `Note` types live
	 * behind the existing `PhoneRingtoneEngine.h` include and the
	 * `kMelodies` table already lives in this translation unit's
	 * anonymous namespace), no new const data, no new SPIFFS asset
	 * cost. Every existing call site of the catalogue keeps
	 * byte-identical behaviour -- the new helper is purely
	 * additive.
	 */
	static uint16_t meanFreqHz(uint8_t id);

	/**
	 * S243 - derived structural audible-step accessor for chime `id`.
	 * Returns the number of catalogued `PhoneRingtoneEngine::Note`
	 * entries in the underlying Melody whose `freq != 0` -- i.e. the
	 * count of NON-rest steps that the engine will actually drive the
	 * piezo for. Rest-encoded `freq == 0` entries are skipped so a
	 * future leading / trailing / interior rest does not inflate the
	 * answer past the audible step count, exactly mirroring the rest-
	 * skipping rule S240 (peakFreqHz), S241 (troughFreqHz) and S242
	 * (meanFreqHz) already use for the catalogued ceiling, floor and
	 * centre scans. Returns 0 for an out-of-range id and for the
	 * (currently impossible) empty-melody case.
	 *
	 * Where `noteCount(id)` (S233) reports the catalogued TOTAL step
	 * count (audible notes + any future rests), `audibleNoteCount(id)`
	 * reports the catalogued AUDIBLE step count -- the number of
	 * catalogued steps that will actually drive the piezo when the
	 * engine plays back the cue. For every v1 catalogue entry today
	 * the two accessors agree (no v1 chime currently uses rests), so
	 * the picker / diag walk gets byte-identical behaviour today; they
	 * diverge only when a future entry interleaves a rest, at which
	 * point the picker can render captions like
	 * "(3 notes, 1 rest, 240 ms)" using the difference
	 * `noteCount(id) - audibleNoteCount(id)` without re-walking the
	 * const Melody* pointer at the call site.
	 *
	 * Foreshadowed by the S242 commit body's "uint16_t non-rest
	 * counter" framing: the divisor S242 already computes internally
	 * for `meanFreqHz` is exactly `audibleNoteCount(id)`, so a caller
	 * that wants both the catalogued mean and the divisor (e.g. a
	 * "mean of N audible steps" caption on the picker pitch bar)
	 * reads the two accessors separately rather than re-scanning the
	 * catalogue twice.
	 *
	 * Distinct from `noteCount(id)` (S233 -- catalogued TOTAL step
	 * count, includes rests), distinct from `firstFreqHz` /
	 * `lastFreqHz` (catalogued endpoints), distinct from `peakFreqHz`
	 * / `troughFreqHz` / `meanFreqHz` (catalogued ceiling / floor /
	 * centre), and distinct from `PhoneRingtoneEngine::currentFreq()`
	 * (live-piezo accessor S191). Profile-state INDEPENDENT: the
	 * catalogued audible-step count is the same on SILENT / MEETING
	 * profiles as on GENERAL / OUTDOOR / HEADSET (the S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form).
	 *
	 * Cheap O(notes) linear scan with a uint16_t counter; no
	 * arithmetic, no rounding, no per-call allocation. Header surface
	 * grows by exactly one public symbol (`static uint16_t
	 * audibleNoteCount`); the cpp adds a single function next to the
	 * existing `count` / `valid` / `name` / `melody` / `play` /
	 * `tryPlay` / `isSilenced` / `durationMs` / `noteCount` /
	 * `firstFreqHz` / `lastFreqHz` / `gapMs` / `loops` / `silhouette`
	 * / `pitchSpanHz` / `peakFreqHz` / `troughFreqHz` / `meanFreqHz`
	 * cluster. No new includes (the `Melody` and `Note` types live
	 * behind the existing `PhoneRingtoneEngine.h` include and the
	 * `kMelodies` table already lives in this translation unit's
	 * anonymous namespace), no new const data, no new SPIFFS asset
	 * cost. Every existing call site of the catalogue keeps byte-
	 * identical behaviour -- the new helper is purely additive.
	 */
	static uint16_t audibleNoteCount(uint8_t id);

	/**
	 * S244 - derived structural rest-step accessor for chime `id`.
	 * Returns the number of catalogued `PhoneRingtoneEngine::Note`
	 * entries in the underlying Melody whose `freq == 0` -- i.e. the
	 * count of REST steps that the engine encounters but does NOT
	 * drive the piezo for. Exact complement of `audibleNoteCount(id)`
	 * (S243): for every catalogued chime
	 *     restNoteCount(id) + audibleNoteCount(id) == noteCount(id)
	 * so a caller that wants both halves of the split (e.g. a picker
	 * row caption like "(3 notes, 1 rest, 240 ms)") can read the
	 * dedicated accessor instead of computing the difference
	 * `noteCount(id) - audibleNoteCount(id)` at the call site that
	 * S243's commit body explicitly foreshadowed. Returns 0 for an
	 * out-of-range id and for the (currently impossible) empty-melody
	 * case; returns 0 for every v1 catalogue entry today (no v1 chime
	 * uses rests), so the picker / diag walk gets byte-identical
	 * behaviour today and only starts diverging from a constant zero
	 * when a future v2+ entry interleaves a rest.
	 *
	 * Where `noteCount(id)` (S233) reports the catalogued TOTAL step
	 * count and `audibleNoteCount(id)` (S243) reports the catalogued
	 * AUDIBLE step count, `restNoteCount(id)` reports the catalogued
	 * REST step count -- the third leg of the same partition, so the
	 * picker can render the full caption directly without subtraction
	 * arithmetic at the call site. This rounds out the structural-
	 * count cluster (TOTAL / AUDIBLE / REST) the same way S239-S242
	 * rounded out the structural-pitch cluster (SPAN / PEAK / TROUGH
	 * / MEAN) on top of the S234 / S235 endpoint pair.
	 *
	 * Distinct from `noteCount(id)` (S233 -- catalogued TOTAL step
	 * count, includes rests), distinct from `audibleNoteCount(id)`
	 * (S243 -- catalogued non-rest step count), distinct from
	 * `firstFreqHz` / `lastFreqHz` (catalogued endpoints), distinct
	 * from `peakFreqHz` / `troughFreqHz` / `meanFreqHz` (catalogued
	 * ceiling / floor / centre), and distinct from
	 * `PhoneRingtoneEngine::currentFreq()` (the S191 live-piezo
	 * accessor). Profile-state INDEPENDENT: the catalogued rest-step
	 * count is the same on SILENT / MEETING profiles as on GENERAL /
	 * OUTDOOR / HEADSET (the S231 `tryPlay(id)` gate already reports
	 * the silenced answer separately for any caller that wants to
	 * fade the row caption into a "(silenced)" form).
	 *
	 * Cheap O(notes) linear scan with a uint16_t counter (saturates
	 * well below the uint16_t ceiling for any plausible v1+v2 melody
	 * length); no arithmetic, no rounding, no per-call allocation.
	 * Header surface grows by exactly one public symbol (`static
	 * uint16_t restNoteCount`); the cpp adds a single function next
	 * to the existing `count` / `valid` / `name` / `melody` / `play`
	 * / `tryPlay` / `isSilenced` / `durationMs` / `noteCount` /
	 * `firstFreqHz` / `lastFreqHz` / `gapMs` / `loops` / `silhouette`
	 * / `pitchSpanHz` / `peakFreqHz` / `troughFreqHz` / `meanFreqHz`
	 * / `audibleNoteCount` cluster. No new includes (the `Melody`
	 * and `Note` types live behind the existing `PhoneRingtoneEngine.h`
	 * include and the `kMelodies` table already lives in this
	 * translation unit's anonymous namespace), no new const data, no
	 * new SPIFFS asset cost. Every existing call site of the
	 * catalogue keeps byte-identical behaviour -- the new helper is
	 * purely additive.
	 */
	static uint16_t restNoteCount(uint8_t id);

	/**
	 * S246 - derived structural audible-drive-time accessor for chime
	 * `id`. Returns the sum of `durationMs` across every catalogued
	 * `PhoneRingtoneEngine::Note` entry in the underlying Melody whose
	 * `freq != 0` -- i.e. the wall-clock time, in ms, that the engine
	 * actually drives the piezo for one playback of the chime, NOT
	 * counting rest-step durations and NOT counting the per-step
	 * inter-loop `gapMs` filler that `durationMs(id)` (S232) folds into
	 * its TOTAL answer.
	 *
	 * The duration-side complement of `audibleNoteCount(id)` (S243):
	 * where S243 reports "how MANY of the catalogued steps drive the
	 * piezo," S246 reports "for HOW LONG the piezo is driven across
	 * those steps." Together with `restNoteCount(id)` (S244 -- count
	 * of REST steps) and a future `restDurationMs(id)` (the silence-
	 * side complement that the next session in this cluster will
	 * promote), the catalogue exposes both halves of the audible /
	 * rest split on both axes (count and duration), so a future
	 * "Settings -> Sounds -> System chimes" picker can render a row
	 * caption like "(3 notes, 1 rest, 180 ms audible / 60 ms silent)"
	 * by reading dedicated accessors instead of walking the
	 * `kMelodies` const Note* pointer at the call site.
	 *
	 * Returns 0 for an out-of-range id, for the (currently impossible)
	 * empty-melody case, and for the (also currently impossible) all-
	 * rests catalogue entry. For every v1 catalogue entry today (no v1
	 * chime uses rests) `audibleDurationMs(id)` collapses to the same
	 * "sum of durationMs across every step" answer that the audible-
	 * step branch of `durationMs(id)` already produces, MINUS the
	 * gapMs filler that `durationMs(id)` adds back in -- so v1 callers
	 * see byte-identical behaviour today and only start diverging from
	 * `durationMs(id) - (gapMs(id) * noteCount(id))` when a future v2+
	 * entry interleaves a rest. Saturates at `0xFFFF` ms (the same
	 * uint16_t ceiling `durationMs(id)` already uses) so the picker
	 * can render the value as a four-digit integer without an int
	 * cast at the call site; in practice no plausible v1+v2 chime
	 * approaches that ceiling (the longest catalogued chime today is
	 * a few hundred ms).
	 *
	 * Where `durationMs(id)` (S232) reports the catalogued TOTAL
	 * playback duration (rest-step durations + audible-step durations
	 * + per-step gapMs filler) and `gapMs(id)` (S236) reports the
	 * per-step filler component in isolation, `audibleDurationMs(id)`
	 * reports the audible-step component in isolation -- the third
	 * leg of the same partition that the picker can mix and match
	 * to render any of the natural row captions the future picker
	 * needs (TOTAL, AUDIBLE, GAP, REST). Distinct from `durationMs`
	 * (TOTAL incl. rests + gaps), distinct from `gapMs` (per-step
	 * filler in isolation), distinct from `noteCount` /
	 * `audibleNoteCount` / `restNoteCount` (catalogued step COUNTS,
	 * not durations), and distinct from
	 * `PhoneRingtoneEngine::isPlaying()` / `currentFreq()` (S191
	 * live-piezo accessors that report runtime playback state, not
	 * catalogued shape).
	 *
	 * Profile-state INDEPENDENT: the catalogued audible-drive
	 * duration is the same on SILENT / MEETING profiles as on
	 * GENERAL / OUTDOOR / HEADSET (the S231 `tryPlay(id)` gate
	 * already reports the silenced answer separately for any caller
	 * that wants to fade the row caption into a "(silenced)" form).
	 * Cheap O(notes) linear scan with a uint32_t accumulator clamped
	 * to a uint16_t return; no rounding, no per-call allocation.
	 * Header surface grows by exactly one public symbol (`static
	 * uint16_t audibleDurationMs`); the cpp adds a single function
	 * next to the existing `count` / `valid` / `name` / `melody` /
	 * `play` / `tryPlay` / `isSilenced` / `durationMs` / `noteCount`
	 * / `firstFreqHz` / `lastFreqHz` / `gapMs` / `loops` /
	 * `silhouette` / `pitchSpanHz` / `peakFreqHz` / `troughFreqHz`
	 * / `meanFreqHz` / `audibleNoteCount` / `restNoteCount`
	 * cluster. No new includes (the `Melody` and `Note` types live
	 * behind the existing `PhoneRingtoneEngine.h` include and the
	 * `kMelodies` table already lives in this translation unit's
	 * anonymous namespace), no new const data, no new SPIFFS asset
	 * cost. Every existing call site of the catalogue keeps byte-
	 * identical behaviour -- the new helper is purely additive.
	 */
	static uint16_t audibleDurationMs(uint8_t id);

	/**
	 * S247 - derived structural rest-step-duration accessor for
	 * chime `id`. Returns the sum of `durationMs` across every
	 * catalogued `PhoneRingtoneEngine::Note` entry in the underlying
	 * Melody whose `freq == 0` -- i.e. the wall-clock time, in ms,
	 * that the engine spends sitting on REST steps without driving
	 * the piezo for one playback of the chime, NOT counting the
	 * audible-step durations that `audibleDurationMs(id)` (S246)
	 * reports and NOT counting the per-step inter-loop `gapMs`
	 * filler that `durationMs(id)` (S232) folds into its TOTAL
	 * answer.
	 *
	 * The silence-side complement of `audibleDurationMs(id)` (S246)
	 * and the duration-side complement of `restNoteCount(id)`
	 * (S244): where S246 reports "for HOW LONG the piezo is driven
	 * across the audible steps" and S244 reports "how MANY of the
	 * catalogued steps are RESTS," `restDurationMs(id)` reports
	 * "for HOW LONG the catalogued REST steps hold the piezo
	 * silent." The four leaves of the catalogue's audible / rest
	 * split now exist as dedicated accessors on both axes:
	 * `audibleNoteCount(id)` (S243) + `restNoteCount(id)` (S244) on
	 * the COUNT axis, `audibleDurationMs(id)` (S246) +
	 * `restDurationMs(id)` (S247) on the DURATION axis. Together
	 * with `gapMs(id)` (S236) and `noteCount(id)` (S233) the
	 * catalogue exposes the full structural-duration partition that
	 * `durationMs(id)` (S232) folds together: for every catalogued
	 * chime
	 *     audibleDurationMs(id) + restDurationMs(id)
	 *       + gapMs(id) * noteCount(id)
	 *       == durationMs(id)
	 * (modulo the uint16_t saturation that all four duration
	 * accessors share), so a future "Settings -> Sounds -> System
	 * chimes" picker row caption like "(3 notes, 1 rest, 180 ms
	 * audible / 60 ms silent)" can read dedicated accessors instead
	 * of walking the const `Note*` pointer at the call site or
	 * computing the complement via subtraction.
	 *
	 * Returns 0 for an out-of-range id, for the (currently
	 * impossible) empty-melody case, for any catalogue entry with
	 * no rest steps, and -- byte-identically -- for every v1
	 * catalogue entry today (no v1 chime uses rests), so v1 callers
	 * see a constant 0 today and only see `restDurationMs(id)`
	 * diverge from 0 when a future v2+ entry interleaves a rest.
	 * Saturates at `0xFFFF` ms (the same uint16_t ceiling
	 * `durationMs(id)` and `audibleDurationMs(id)` already use) so
	 * a picker row caption can render the value as a four-digit
	 * integer without an int cast at the call site.
	 *
	 * Distinct from `durationMs` (TOTAL incl. audible + rests +
	 * gaps), distinct from `audibleDurationMs` (audible-step
	 * component in isolation), distinct from `gapMs` (per-step
	 * filler in isolation), distinct from `noteCount` /
	 * `audibleNoteCount` / `restNoteCount` (catalogued step COUNTS,
	 * not durations), and distinct from
	 * `PhoneRingtoneEngine::isPlaying()` / `currentFreq()` (the
	 * S191 live-piezo accessors that report runtime playback
	 * state, not catalogued shape). Profile-state INDEPENDENT: the
	 * catalogued rest-step duration is the same on SILENT /
	 * MEETING profiles as on GENERAL / OUTDOOR / HEADSET (the S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form). Cheap O(notes) linear scan with
	 * a uint32_t accumulator clamped to a uint16_t return; no
	 * rounding, no per-call allocation. Header surface grows by
	 * exactly one public symbol (`static uint16_t restDurationMs`);
	 * the cpp adds a single function next to the existing `count`
	 * / `valid` / `name` / `melody` / `play` / `tryPlay` /
	 * `isSilenced` / `durationMs` / `noteCount` / `firstFreqHz` /
	 * `lastFreqHz` / `gapMs` / `loops` / `silhouette` /
	 * `pitchSpanHz` / `peakFreqHz` / `troughFreqHz` / `meanFreqHz`
	 * / `audibleNoteCount` / `restNoteCount` / `audibleDurationMs`
	 * cluster. No new includes (the `Melody` and `Note` types live
	 * behind the existing `PhoneRingtoneEngine.h` include and the
	 * `kMelodies` table already lives in this translation unit's
	 * anonymous namespace), no new const data, no new SPIFFS asset
	 * cost. Every existing call site of the catalogue keeps byte-
	 * identical behaviour -- the new helper is purely additive.
	 */
	static uint16_t restDurationMs(uint8_t id);

	/**
	 * S248 - derived structural inter-step gap-total accessor for chime
	 * `id`. Returns the catalogued per-step `gapMs` filler the engine
	 * inserts BETWEEN consecutive `PhoneRingtoneEngine::Note` steps for
	 * one playback of the chime, summed across every step (i.e.
	 * `gapMs(id) * noteCount(id)`, saturated at the uint16_t ceiling
	 * the rest of the duration cluster shares). Where `gapMs(id)`
	 * (S236) reports the per-step filler component in isolation (the
	 * single catalogued `kMelodies[id].gapMs` field, in ms, the
	 * engine waits between two consecutive steps), `gapTotalMs(id)`
	 * reports the same component IN AGGREGATE across the whole
	 * playback -- the third leg of the structural-duration partition
	 * that `durationMs(id)` (S232) folds together.
	 *
	 * The aggregate-side complement of `gapMs(id)` (S236), and the
	 * gap-axis sibling of `audibleDurationMs(id)` (S246) and
	 * `restDurationMs(id)` (S247): every catalogued step's wall-clock
	 * cost partitions cleanly into one of three buckets -- the
	 * audible-step durations the piezo is driven for (S246), the
	 * rest-step durations the piezo holds silent for (S247), and the
	 * inter-step gap filler the engine waits between consecutive
	 * steps (S248). Together with `noteCount(id)` (S233) the
	 * catalogue exposes the full structural-duration partition that
	 * `durationMs(id)` folds together: for every catalogued chime
	 *     audibleDurationMs(id) + restDurationMs(id) + gapTotalMs(id)
	 *       == durationMs(id)
	 * (modulo the uint16_t saturation that all four duration
	 * accessors share), so a future "Settings -> Sounds -> System
	 * chimes" picker row caption like "(3 notes, 1 rest, 180 ms
	 * audible / 60 ms silent / 90 ms gaps)" can read a dedicated
	 * accessor for the gap-axis leg instead of computing
	 * `gapMs(id) * noteCount(id)` (with a uint16_t-saturation guard)
	 * at the call site or recovering it via subtraction from
	 * `durationMs(id) - audibleDurationMs(id) - restDurationMs(id)`.
	 *
	 * Returns 0 for an out-of-range id, for the (currently
	 * impossible) empty-melody case, and -- byte-identically -- for
	 * every v1 catalogue entry whose `kMelodies[id].gapMs == 0` (i.e.
	 * the single-pulse chimes that don't space their steps with a
	 * gap), so the v1 callers that today read `gapMs(id)` and see 0
	 * see the same 0 from `gapTotalMs(id)`. Saturates at `0xFFFF` ms
	 * (the same uint16_t ceiling `durationMs(id)`,
	 * `audibleDurationMs(id)`, and `restDurationMs(id)` already use)
	 * so a picker row caption can render the value as a four-digit
	 * integer without an int cast at the call site; in practice no
	 * plausible v1+v2 chime approaches that ceiling (the longest
	 * catalogued chime today is a few hundred ms and the per-step
	 * gap is bounded by `gapMs`'s uint16_t storage, so the product
	 * stays well clear of the ceiling for any realistic count).
	 *
	 * Distinct from `gapMs` (per-step filler in isolation), distinct
	 * from `durationMs` (TOTAL incl. audible + rests + gaps),
	 * distinct from `audibleDurationMs` (audible-step component in
	 * isolation), distinct from `restDurationMs` (rest-step
	 * component in isolation), distinct from `noteCount` /
	 * `audibleNoteCount` / `restNoteCount` (catalogued step COUNTS,
	 * not durations), and distinct from
	 * `PhoneRingtoneEngine::isPlaying()` / `currentFreq()` (the
	 * S191 live-piezo accessors that report runtime playback state,
	 * not catalogued shape). Profile-state INDEPENDENT: the
	 * catalogued inter-step gap total is the same on SILENT /
	 * MEETING profiles as on GENERAL / OUTDOOR / HEADSET (the S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form). Cheap O(1) -- two field reads, one
	 * uint32_t multiply, one saturate-to-uint16_t -- no per-call
	 * allocation, no scan of the underlying `Note*` array. Header
	 * surface grows by exactly one public symbol (`static uint16_t
	 * gapTotalMs`); the cpp adds a single function next to the
	 * existing `count` / `valid` / `name` / `melody` / `play` /
	 * `tryPlay` / `isSilenced` / `durationMs` / `noteCount` /
	 * `firstFreqHz` / `lastFreqHz` / `gapMs` / `loops` /
	 * `silhouette` / `pitchSpanHz` / `peakFreqHz` / `troughFreqHz`
	 * / `meanFreqHz` / `audibleNoteCount` / `restNoteCount` /
	 * `audibleDurationMs` / `restDurationMs` cluster. No new
	 * includes (the `Melody` type lives behind the existing
	 * `PhoneRingtoneEngine.h` include and the `kMelodies` table
	 * already lives in this translation unit's anonymous
	 * namespace), no new const data, no new SPIFFS asset cost.
	 * Every existing call site of the catalogue keeps byte-
	 * identical behaviour -- the new helper is purely additive.
	 */
	static uint16_t gapTotalMs(uint8_t id);

	/**
	 * S249 - derived per-audible-step mean-duration accessor for chime
	 * `id`. Returns the catalogued audible-step durations the engine
	 * holds the piezo at a tone for (the sum `audibleDurationMs(id)`
	 * (S246) reports) divided by the count of those audible steps (the
	 * count `audibleNoteCount(id)` (S243) reports), i.e. the mean
	 * wall-clock duration per AUDIBLE catalogued step, in ms, rounded
	 * toward zero by integer division. Where `audibleDurationMs(id)`
	 * reports the SUM of the catalogued audible-step durations and
	 * `audibleNoteCount(id)` reports the COUNT of those steps,
	 * `meanNoteDurationMs(id)` collapses the pair into a per-step
	 * CENTRE -- the duration-axis sibling of `meanFreqHz(id)` (S242)
	 * on the pitch axis.
	 *
	 * So a future "Settings -> Sounds -> System chimes" picker row
	 * caption like "(3 notes, ~60 ms each)" can read a dedicated
	 * accessor for the per-audible-step centre instead of computing
	 * `audibleDurationMs(id) / audibleNoteCount(id)` (with a
	 * divide-by-zero guard) at the call site. Returns 0 for an
	 * out-of-range id, for the (currently impossible) empty-melody
	 * case, for the all-rests-melody case (where
	 * `audibleNoteCount(id) == 0` and the divisor would be zero) --
	 * exactly mirroring the rest-skipping rule the pitch-axis trio
	 * `peakFreqHz` (S240) / `troughFreqHz` (S241) / `meanFreqHz`
	 * (S242) already use for the catalogued ceiling, floor and
	 * centre, and the same three "no answer" cases all three pitch
	 * accessors already collapse to 0.
	 *
	 * Saturates at `0xFFFF` ms (the same uint16_t ceiling the duration
	 * cluster `durationMs` / `audibleDurationMs` / `restDurationMs` /
	 * `gapTotalMs` already share) so a picker row caption can render
	 * the value as a four-digit integer without an int cast at the
	 * call site; in practice no realistic per-audible-step duration
	 * approaches that ceiling, but the saturate-on-overflow guard
	 * keeps the return type honest.
	 *
	 * Distinct from `audibleDurationMs` (audible-step SUM in
	 * isolation), distinct from `durationMs` (TOTAL incl. audible +
	 * rests + gaps), distinct from `restDurationMs` (rest-step
	 * component in isolation), distinct from `gapTotalMs` (inter-step
	 * gap component in isolation), distinct from `noteCount` /
	 * `audibleNoteCount` / `restNoteCount` (catalogued step COUNTS,
	 * not durations), distinct from `gapMs` (per-step filler in
	 * isolation, not per-step audible centre), and distinct from
	 * `PhoneRingtoneEngine::isPlaying()` / `currentFreq()` (the S191
	 * live-piezo accessors that report runtime playback state, not
	 * catalogued shape). Profile-state INDEPENDENT: the catalogued
	 * per-audible-step mean is the same on SILENT / MEETING profiles
	 * as on GENERAL / OUTDOOR / HEADSET (the S231 `tryPlay(id)` gate
	 * already reports the silenced answer separately for any caller
	 * that wants to fade the row caption into a "(silenced)" form).
	 * Cheap O(notes) linear scan with two uint32_t accumulators
	 * (audible-step duration sum + audible-step counter) and one
	 * final divide-and-saturate; no per-call allocation, no recursion
	 * into the existing `audibleDurationMs` / `audibleNoteCount`
	 * accessors -- the loop fuses the two passes so the catalogued
	 * `Note*` array is walked exactly once per call. Header surface
	 * grows by exactly one public symbol (`static uint16_t
	 * meanNoteDurationMs`); the cpp adds a single function next to
	 * the existing `count` / `valid` / `name` / `melody` / `play` /
	 * `tryPlay` / `isSilenced` / `durationMs` / `noteCount` /
	 * `firstFreqHz` / `lastFreqHz` / `gapMs` / `loops` /
	 * `silhouette` / `pitchSpanHz` / `peakFreqHz` / `troughFreqHz` /
	 * `meanFreqHz` / `audibleNoteCount` / `restNoteCount` /
	 * `audibleDurationMs` / `restDurationMs` / `gapTotalMs` cluster.
	 * No new includes (the `Melody` and `Note` types live behind the
	 * existing `PhoneRingtoneEngine.h` include and the `kMelodies`
	 * table already lives in this translation unit's anonymous
	 * namespace), no new const data, no new SPIFFS asset cost.
	 * Every existing call site of the catalogue keeps byte-identical
	 * behaviour -- the new helper is purely additive.
	 */
	static uint16_t meanNoteDurationMs(uint8_t id);

	/**
	 * S250 - derived per-audible-step maximum-duration accessor for
	 * chime `id`. Returns the longest catalogued audible-step
	 * `durationMs` value the engine holds the piezo at a tone for,
	 * in ms. Where `audibleDurationMs(id)` (S246) reports the SUM of
	 * the catalogued audible-step durations, `audibleNoteCount(id)`
	 * (S243) reports the COUNT of those steps, and
	 * `meanNoteDurationMs(id)` (S249) collapses the SUM/COUNT pair
	 * into a per-step CENTRE, `peakNoteDurationMs(id)` reports the
	 * per-step CEILING -- the duration-axis sibling of
	 * `peakFreqHz(id)` (S240) on the pitch axis.
	 *
	 * So a future "Settings -> Sounds -> System chimes" picker row
	 * caption like "(3 notes, ~60 ms each, longest 80 ms)" can read
	 * a dedicated accessor for the per-audible-step ceiling instead
	 * of walking the catalogued `Note*` pointer at the call site or
	 * computing the maximum via a per-row accumulator in the picker.
	 *
	 * Returns 0 for an out-of-range id, for the (currently
	 * impossible) empty-melody case, and for the all-rests-melody
	 * case (where `audibleNoteCount(id) == 0`, no audible step
	 * exists, and the ceiling is undefined) -- the same three "no
	 * answer" cases the pitch-axis trio `peakFreqHz` (S240) /
	 * `troughFreqHz` (S241) / `meanFreqHz` (S242) and the
	 * duration-axis centre accessor `meanNoteDurationMs` (S249)
	 * already collapse to 0, so the picker / diag walk does not
	 * have to special-case the all-rest melody before calling.
	 *
	 * Saturates at `0xFFFF` ms (the same uint16_t ceiling the
	 * duration cluster `durationMs` / `audibleDurationMs` /
	 * `restDurationMs` / `gapTotalMs` / `meanNoteDurationMs`
	 * already share); since the catalogued per-step duration is
	 * itself a uint16_t the cap is in practice unreachable, but the
	 * saturate-on-overflow guard keeps the return type honest.
	 *
	 * Distinct from `audibleDurationMs` (audible-step SUM, not
	 * ceiling), distinct from `meanNoteDurationMs` (audible-step
	 * CENTRE, not ceiling), distinct from `durationMs` (TOTAL incl.
	 * audible + rests + gaps), distinct from `restDurationMs`
	 * (rest-step component in isolation), distinct from
	 * `gapTotalMs` (inter-step gap component in isolation),
	 * distinct from `gapMs` (per-step filler in isolation, not
	 * per-step audible ceiling), distinct from `noteCount` /
	 * `audibleNoteCount` / `restNoteCount` (catalogued step
	 * COUNTS, not durations), and distinct from
	 * `PhoneRingtoneEngine::isPlaying()` / `currentFreq()` (the
	 * S191 live-piezo accessors that report runtime playback state,
	 * not catalogued shape). Profile-state INDEPENDENT: the
	 * catalogued per-audible-step ceiling is the same on SILENT /
	 * MEETING profiles as on GENERAL / OUTDOOR / HEADSET (the S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form).
	 *
	 * Cheap O(notes) linear scan with a single uint16_t running
	 * max that skips rest steps (`freq == 0`) so the ceiling
	 * reports the longest AUDIBLE step rather than the longest
	 * catalogued step -- matching the rest-skipping rule the
	 * pitch-axis trio `peakFreqHz` / `troughFreqHz` / `meanFreqHz`
	 * and the duration-axis centre `meanNoteDurationMs` already
	 * use; no per-call allocation, no recursion into the existing
	 * `audibleDurationMs` / `audibleNoteCount` /
	 * `meanNoteDurationMs` accessors. Header surface grows by
	 * exactly one public symbol; the cpp adds a single function
	 * next to the existing `count` / `valid` / `name` / `melody` /
	 * `play` / `tryPlay` / `isSilenced` / `durationMs` /
	 * `noteCount` / `firstFreqHz` / `lastFreqHz` / `gapMs` /
	 * `loops` / `silhouette` / `pitchSpanHz` / `peakFreqHz` /
	 * `troughFreqHz` / `meanFreqHz` / `audibleNoteCount` /
	 * `restNoteCount` / `audibleDurationMs` / `restDurationMs` /
	 * `gapTotalMs` / `meanNoteDurationMs` cluster. No new includes,
	 * no new const data, no new SPIFFS asset cost. Every existing
	 * call site of the catalogue keeps byte-identical behaviour --
	 * the new helper is purely additive.
	 */
	static uint16_t peakNoteDurationMs(uint8_t id);

	/**
	 * S251 - derived per-audible-step minimum-duration accessor for
	 * chime `id`. Returns the shortest catalogued audible-step
	 * `durationMs` value the engine holds the piezo at a tone for,
	 * in ms. Where `audibleDurationMs(id)` (S246) reports the SUM of
	 * the catalogued audible-step durations, `audibleNoteCount(id)`
	 * (S243) reports the COUNT of those steps,
	 * `meanNoteDurationMs(id)` (S249) collapses the SUM/COUNT pair
	 * into a per-step CENTRE, and `peakNoteDurationMs(id)` (S250)
	 * reports the per-step CEILING, `troughNoteDurationMs(id)`
	 * reports the per-step FLOOR -- the duration-axis sibling of
	 * `troughFreqHz(id)` (S241) on the pitch axis, completing the
	 * duration-axis (CEILING, FLOOR, CENTRE) trio that mirrors the
	 * pitch-axis (CEILING, FLOOR, CENTRE) trio of S240 / S241 /
	 * S242.
	 *
	 * So a future "Settings -> Sounds -> System chimes" picker row
	 * caption like "(3 notes, 60-80 ms each, ~70 ms mean)" can read
	 * a dedicated accessor for the per-audible-step floor instead of
	 * walking the catalogued `Note*` pointer at the call site or
	 * computing the minimum via a per-row accumulator in the picker.
	 *
	 * Returns 0 for an out-of-range id, for the (currently
	 * impossible) empty-melody case, and for the all-rests-melody
	 * case (where `audibleNoteCount(id) == 0`, no audible step
	 * exists, and the floor is undefined) -- the same three "no
	 * answer" cases the pitch-axis trio `peakFreqHz` (S240) /
	 * `troughFreqHz` (S241) / `meanFreqHz` (S242) and the
	 * duration-axis CENTRE / CEILING pair `meanNoteDurationMs`
	 * (S249) / `peakNoteDurationMs` (S250) already collapse to 0.
	 *
	 * Saturates at `0xFFFF` ms (the same uint16_t ceiling the
	 * duration cluster `durationMs` / `audibleDurationMs` /
	 * `restDurationMs` / `gapTotalMs` / `meanNoteDurationMs` /
	 * `peakNoteDurationMs` already share); since the catalogued
	 * per-step duration is itself a uint16_t the cap is in practice
	 * unreachable, but the saturate-on-overflow guard keeps the
	 * return type honest.
	 *
	 * Distinct from `peakNoteDurationMs` (audible-step CEILING, not
	 * floor), distinct from `meanNoteDurationMs` (audible-step
	 * CENTRE, not floor), distinct from `audibleDurationMs`
	 * (audible-step SUM, not floor), distinct from `durationMs`
	 * (TOTAL incl. audible + rests + gaps), distinct from
	 * `restDurationMs` (rest-step component in isolation), distinct
	 * from `gapTotalMs` (inter-step gap component in isolation),
	 * distinct from `gapMs` (per-step filler in isolation, not
	 * per-step audible floor), distinct from `noteCount` /
	 * `audibleNoteCount` / `restNoteCount` (catalogued step COUNTS,
	 * not durations), and distinct from
	 * `PhoneRingtoneEngine::isPlaying()` / `currentFreq()` (the
	 * S191 live-piezo accessors that report runtime playback state,
	 * not catalogued shape). Profile-state INDEPENDENT: the
	 * catalogued per-audible-step floor is the same on SILENT /
	 * MEETING profiles as on GENERAL / OUTDOOR / HEADSET (the S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form).
	 *
	 * Cheap O(notes) linear scan with a single uint16_t running min
	 * guarded by a `found` sentinel -- mirroring `troughFreqHz(id)`
	 * (S241) exactly with `<` substituted for `>` and a `found`
	 * flag because there is no natural starting value for a min-
	 * search across uint16_t -- and it skips rest steps (`freq ==
	 * 0`) so the floor reports the shortest AUDIBLE step rather
	 * than the shortest catalogued step, matching the rest-skipping
	 * rule the pitch-axis trio `peakFreqHz` / `troughFreqHz` /
	 * `meanFreqHz` and the duration-axis centre/ceiling pair
	 * `meanNoteDurationMs` / `peakNoteDurationMs` already use; no
	 * per-call allocation, no recursion into the existing
	 * `audibleDurationMs` / `audibleNoteCount` /
	 * `meanNoteDurationMs` / `peakNoteDurationMs` accessors. Header
	 * surface grows by exactly one public symbol; the cpp adds a
	 * single function next to the existing `count` / `valid` /
	 * `name` / `melody` / `play` / `tryPlay` / `isSilenced` /
	 * `durationMs` / `noteCount` / `firstFreqHz` / `lastFreqHz` /
	 * `gapMs` / `loops` / `silhouette` / `pitchSpanHz` /
	 * `peakFreqHz` / `troughFreqHz` / `meanFreqHz` /
	 * `audibleNoteCount` / `restNoteCount` / `audibleDurationMs` /
	 * `restDurationMs` / `gapTotalMs` / `meanNoteDurationMs` /
	 * `peakNoteDurationMs` cluster. No new includes, no new const
	 * data, no new SPIFFS asset cost. Every existing call site of
	 * the catalogue keeps byte-identical behaviour -- the new
	 * helper is purely additive.
	 */
	static uint16_t troughNoteDurationMs(uint8_t id);
	/**
	 * S252 - structural first-note duration accessor for chime
	 * `id`. Returns the catalogued duration in ms of the FIRST
	 * `PhoneRingtoneEngine::Note` entry in the underlying Melody
	 * (i.e. `kMelodies[id].notes[0].durationMs`). The duration-axis
	 * sibling of `firstFreqHz(id)` (S234) on the pitch axis -- where
	 * `firstFreqHz(id)` reports the catalogued LEADING pitch the
	 * engine drives the piezo at on the first audible step,
	 * `firstNoteDurationMs(id)` reports the catalogued LEADING
	 * step-duration the engine holds that pitch for. Returns 0 for
	 * an out-of-range id and for the (currently impossible)
	 * empty-melody case -- the same two "no answer" cases the
	 * structural-pair `firstFreqHz` / `lastFreqHz` already collapse
	 * to 0, so a caller does not have to special-case the empty /
	 * out-of-range melody before reading.
	 *
	 * Distinct from `peakNoteDurationMs` (audible-step CEILING, not
	 * leading endpoint), distinct from `troughNoteDurationMs`
	 * (audible-step FLOOR, not leading endpoint), distinct from
	 * `meanNoteDurationMs` (audible-step CENTRE, not leading
	 * endpoint), distinct from `audibleDurationMs` (audible-step
	 * SUM, not leading endpoint), distinct from `durationMs` (TOTAL
	 * incl. audible + rests + gaps), distinct from `restDurationMs`
	 * (rest-step component in isolation), distinct from
	 * `gapTotalMs` (inter-step gap component in isolation), and
	 * distinct from `gapMs` (per-step filler in isolation, not
	 * leading audible step). Like `firstFreqHz(id)` /
	 * `lastFreqHz(id)`, this is a STRUCTURAL accessor (it reads the
	 * array endpoint regardless of whether the first step is an
	 * audible note or a rest) -- in the v1 catalogue no chime opens
	 * with a rest so the answer collapses transparently to "the
	 * leading audible step's duration" for every entry that ships
	 * today. Foreshadowed by the duration cluster's progressive
	 * build-up: `audibleDurationMs(id)` (S246) reports the audible-
	 * step SUM, `audibleNoteCount(id)` (S243) reports the audible-
	 * step COUNT, `meanNoteDurationMs(id)` (S249) collapses the
	 * SUM/COUNT pair into a per-step CENTRE, `peakNoteDurationMs(id)`
	 * (S250) reports the per-step CEILING, `troughNoteDurationMs(id)`
	 * (S251) reports the per-step FLOOR, and `firstNoteDurationMs(id)`
	 * reports the LEADING ENDPOINT -- the first half of the
	 * structural-pair (LEADING, TRAILING) that mirrors the pitch-axis
	 * structural pair `firstFreqHz` / `lastFreqHz` (S234 / S235).
	 *
	 * So a future "Settings -> Sounds -> System chimes" picker row
	 * caption like "(opens 80 ms)" or a `PhoneDiagScreen` "Sound
	 * test" walk that wants to render a leading-step-duration tick
	 * beside the leading-pitch indicator can read a dedicated
	 * accessor for the catalogued leading endpoint instead of
	 * walking the catalogued `Note*` pointer at the call site or
	 * pulling the answer out of the per-step CEILING / FLOOR /
	 * CENTRE accessors that already exist (those report the
	 * extrema across all audible steps, not the catalogued leading
	 * step). Profile-state INDEPENDENT: the catalogued leading
	 * step-duration is the same on SILENT / MEETING profiles as on
	 * GENERAL / OUTDOOR / HEADSET (the S231 `tryPlay(id)` gate
	 * already reports the silenced answer separately for any caller
	 * that wants to fade the row caption into a "(silenced)" form).
	 *
	 * Distinct from `PhoneRingtoneEngine::isPlaying()` /
	 * `currentFreq()` (the S191 live-piezo accessors that report
	 * runtime playback state, not catalogued shape) -- both are
	 * useful and live at different layers, neither subsumes the
	 * other.
	 *
	 * Cheap O(1) struct field read; no engine interaction, no
	 * persisted state, no per-call allocation; mirrors the existing
	 * `firstFreqHz(id)` (S234) implementation pattern with
	 * `durationMs` substituted for `freq`. Header surface grows by
	 * exactly one public symbol (`static uint16_t
	 * firstNoteDurationMs`); the cpp adds a single function next
	 * to the existing `count` / `valid` / `name` / `melody` /
	 * `play` / `tryPlay` / `isSilenced` / `durationMs` /
	 * `noteCount` / `firstFreqHz` / `lastFreqHz` / `gapMs` /
	 * `loops` / `silhouette` / `pitchSpanHz` / `peakFreqHz` /
	 * `troughFreqHz` / `meanFreqHz` / `audibleNoteCount` /
	 * `restNoteCount` / `audibleDurationMs` / `restDurationMs` /
	 * `gapTotalMs` / `meanNoteDurationMs` / `peakNoteDurationMs` /
	 * `troughNoteDurationMs` cluster. No new includes, no new const
	 * data, no new SPIFFS asset cost. Every existing call site of
	 * the catalogue keeps byte-identical behaviour -- the new
	 * helper is purely additive.
	 */
	static uint16_t firstNoteDurationMs(uint8_t id);

	/**
	 * S253 -- Structural last-note duration accessor for chime
	 * `id`. Returns the catalogued duration in ms of the LAST
	 * `PhoneRingtoneEngine::Note` entry in the underlying Melody
	 * (i.e. `kMelodies[id].notes[m.count - 1].durationMs`). The
	 * trailing-endpoint sibling of `firstNoteDurationMs(id)` (S252)
	 * on the duration axis, and the duration-axis sibling of
	 * `lastFreqHz(id)` (S235) on the pitch axis -- where
	 * `lastFreqHz(id)` reports the catalogued TRAILING pitch the
	 * engine drives the piezo at on the final audible step,
	 * `lastNoteDurationMs(id)` reports the catalogued TRAILING
	 * step-duration the engine holds that pitch for. With S252 it
	 * closes the duration-axis structural pair (LEADING, TRAILING)
	 * so the duration axis now matches the pitch axis exactly:
	 * `firstFreqHz` / `lastFreqHz` (S234 / S235) on the pitch axis
	 * <-> `firstNoteDurationMs` / `lastNoteDurationMs` (S252 /
	 * S253) on the duration axis. Returns 0 for an out-of-range id
	 * and for the (currently impossible) empty-melody case -- the
	 * same two "no answer" cases the structural-pair `firstFreqHz`
	 * / `lastFreqHz` and `firstNoteDurationMs` already collapse to
	 * 0, so a caller does not have to special-case the empty /
	 * out-of-range melody before reading.
	 *
	 * Distinct from `firstNoteDurationMs` (LEADING endpoint, not
	 * trailing), distinct from `peakNoteDurationMs` (audible-step
	 * CEILING, not trailing endpoint), distinct from
	 * `troughNoteDurationMs` (audible-step FLOOR, not trailing
	 * endpoint), distinct from `meanNoteDurationMs` (audible-step
	 * CENTRE, not trailing endpoint), distinct from
	 * `audibleDurationMs` (audible-step SUM, not trailing endpoint),
	 * distinct from `durationMs` (TOTAL incl. audible + rests +
	 * gaps), distinct from `restDurationMs` (rest-step component
	 * in isolation), distinct from `gapTotalMs` (inter-step gap
	 * component in isolation), and distinct from `gapMs` (per-step
	 * filler in isolation, not trailing audible step). Like
	 * `firstFreqHz(id)` / `lastFreqHz(id)` / `firstNoteDurationMs(id)`,
	 * this is a STRUCTURAL accessor (it reads the array endpoint
	 * regardless of whether the last step is an audible note or a
	 * rest) -- in the v1 catalogue no chime closes with a rest so
	 * the answer collapses transparently to "the trailing audible
	 * step's duration" for every entry that ships today.
	 *
	 * So a future "Settings -> Sounds -> System chimes" picker row
	 * caption like "(closes 120 ms)" or a `PhoneDiagScreen` "Sound
	 * test" walk that wants to render a trailing-step-duration tick
	 * beside the trailing-pitch indicator can read a dedicated
	 * accessor for the catalogued trailing endpoint instead of
	 * walking the catalogued `Note*` pointer at the call site or
	 * pulling the answer out of the per-step CEILING / FLOOR /
	 * CENTRE accessors that already exist (those report the
	 * extrema across all audible steps, not the catalogued trailing
	 * step). The (LEADING, TRAILING) pair on the duration axis
	 * also lets a caller compute a per-chime step-duration
	 * direction sign (open-vs-close, "the chime accelerates" /
	 * "the chime decelerates" / "the chime holds" tempo silhouette)
	 * the same way the (LEADING, TRAILING) pair on the pitch axis
	 * already powers the rising / falling / level pitch silhouette
	 * via `silhouette(id)` (S237). Profile-state INDEPENDENT: the
	 * catalogued trailing step-duration is the same on SILENT /
	 * MEETING profiles as on GENERAL / OUTDOOR / HEADSET (the S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form).
	 *
	 * Distinct from `PhoneRingtoneEngine::isPlaying()` /
	 * `currentFreq()` (the S191 live-piezo accessors that report
	 * runtime playback state, not catalogued shape) -- both are
	 * useful and live at different layers, neither subsumes the
	 * other.
	 *
	 * Cheap O(1) struct field read; no engine interaction, no
	 * persisted state, no per-call allocation; mirrors the existing
	 * `lastFreqHz(id)` (S235) implementation pattern with
	 * `durationMs` substituted for `freq`. Header surface grows by
	 * exactly one public symbol (`static uint16_t
	 * lastNoteDurationMs`); the cpp adds a single function next
	 * to the existing `count` / `valid` / `name` / `melody` /
	 * `play` / `tryPlay` / `isSilenced` / `durationMs` /
	 * `noteCount` / `firstFreqHz` / `lastFreqHz` / `gapMs` /
	 * `loops` / `silhouette` / `pitchSpanHz` / `peakFreqHz` /
	 * `troughFreqHz` / `meanFreqHz` / `audibleNoteCount` /
	 * `restNoteCount` / `audibleDurationMs` / `restDurationMs` /
	 * `gapTotalMs` / `meanNoteDurationMs` / `peakNoteDurationMs` /
	 * `troughNoteDurationMs` / `firstNoteDurationMs` cluster. No
	 * new includes, no new const data, no new SPIFFS asset cost.
	 * Every existing call site of the catalogue keeps byte-
	 * identical behaviour -- the new helper is purely additive.
	 * Closes the duration-axis structural endpoint pair foreshadowed
	 * by the S252 commit body's "the natural follow-up is the
	 * trailing-endpoint sibling (`lastNoteDurationMs(id)`) to close
	 * out the (LEADING, TRAILING) structural pair on the duration
	 * axis" framing.
	 */
	static uint16_t lastNoteDurationMs(uint8_t id);

	/**
	 * S254 - derived catalogue-wide endpoint-magnitude accessor on
	 * the DURATION axis for chime `id`. Returns the unsigned
	 * absolute difference, in ms, between the catalogued LEADING
	 * step-duration and the catalogued TRAILING step-duration of
	 * the underlying Melody --
	 * `|firstNoteDurationMs(id) - lastNoteDurationMs(id)|`. The
	 * duration-axis sibling of `pitchSpanHz(id)` (S239) on the
	 * pitch axis: where `pitchSpanHz(id)` reports the magnitude of
	 * the (LEADING, TRAILING) pitch-endpoint pair so a caller can
	 * render a pitch-shape height tick without re-deriving the
	 * answer at every call site, `durationSpanMs(id)` reports the
	 * magnitude of the (LEADING, TRAILING) duration-endpoint pair
	 * so the same caller can render a tempo-shape height tick on
	 * the duration axis. Builds on the S252 / S253 endpoint pair
	 * (`firstNoteDurationMs` / `lastNoteDurationMs`) that just
	 * closed the structural endpoint pair on the duration axis;
	 * S254 promotes that pair to a derived MAGNITUDE accessor in
	 * the same way S239 promoted the S234 / S235 pitch endpoint
	 * pair (`firstFreqHz` / `lastFreqHz`) to `pitchSpanHz`. The
	 * duration-axis structural / derived shape now matches the
	 * pitch-axis structural / derived shape exactly: `firstFreqHz`
	 * / `lastFreqHz` (S234 / S235) <-> `firstNoteDurationMs` /
	 * `lastNoteDurationMs` (S252 / S253) at the structural-
	 * endpoint layer, and `pitchSpanHz` (S239) <-> `durationSpanMs`
	 * (S254) at the derived endpoint-magnitude layer. So a future
	 * "Settings -> Sounds -> System chimes" picker row caption
	 * like "(opens 60 ms longer than it closes)" or a
	 * `PhoneDiagScreen` "Sound test" walk that wants to render a
	 * tempo-shape glyph (rising tempo / falling tempo / level
	 * tempo) alongside the pitch-shape glyph already powered by
	 * `silhouette(id)` / `pitchSpanHz(id)` can read a dedicated
	 * derived accessor for the duration-axis endpoint magnitude
	 * instead of computing `abs(firstNoteDurationMs(id) -
	 * lastNoteDurationMs(id))` at the call site. Returns 0 for an
	 * out-of-range id (collapsing transparently because both
	 * `firstNoteDurationMs(id)` and `lastNoteDurationMs(id)`
	 * already collapse to 0 there), for the (currently impossible)
	 * empty-melody case, and -- naturally -- for any chime whose
	 * leading and trailing step-durations are equal (i.e. opens
	 * and closes on the same beat-length). Distinct from
	 * `peakNoteDurationMs` / `troughNoteDurationMs` (audible-step
	 * CEILING / FLOOR across every step, not the structural
	 * endpoints), distinct from `meanNoteDurationMs` (audible-step
	 * CENTRE, not the endpoint magnitude), distinct from
	 * `audibleDurationMs` / `restDurationMs` / `gapTotalMs` (per-
	 * component sums, not endpoint magnitude), distinct from
	 * `durationMs` (TOTAL incl. all components), distinct from
	 * `firstNoteDurationMs` / `lastNoteDurationMs` (catalogued
	 * endpoints, not the magnitude between them). Distinct from
	 * `pitchSpanHz` (pitch axis, not duration axis). Profile-
	 * state INDEPENDENT: the catalogued endpoint magnitude is the
	 * same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
	 * HEADSET (the S231 `tryPlay(id)` gate already reports the
	 * silenced answer separately for any caller that wants to
	 * fade the row caption into a "(silenced)" form). Cheap O(1):
	 * two struct field reads + one subtraction; no engine
	 * interaction, no persisted state, no per-call allocation;
	 * mirrors `pitchSpanHz(id)` (S239) exactly with `durationMs`
	 * substituted for `freq` and `firstNoteDurationMs` /
	 * `lastNoteDurationMs` substituted for `firstFreqHz` /
	 * `lastFreqHz`. Header surface grows by exactly one public
	 * symbol (`static uint16_t durationSpanMs`); the cpp adds a
	 * single function next to the existing `count` / `valid` /
	 * `name` / `melody` / `play` / `tryPlay` / `isSilenced` /
	 * `durationMs` / `noteCount` / `firstFreqHz` / `lastFreqHz` /
	 * `gapMs` / `loops` / `silhouette` / `pitchSpanHz` /
	 * `peakFreqHz` / `troughFreqHz` / `meanFreqHz` /
	 * `audibleNoteCount` / `restNoteCount` / `audibleDurationMs` /
	 * `restDurationMs` / `gapTotalMs` / `meanNoteDurationMs` /
	 * `peakNoteDurationMs` / `troughNoteDurationMs` /
	 * `firstNoteDurationMs` / `lastNoteDurationMs` cluster. No new
	 * includes, no new const data, no new SPIFFS asset cost. Every
	 * existing call site of the catalogue keeps byte-identical
	 * behaviour -- the new helper is purely additive. Closes the
	 * derived endpoint-magnitude axis on the duration side at full
	 * symmetry with the pitch side.
	 */
	static uint16_t durationSpanMs(uint8_t id);

	/**
	 * S255 - derived audible-CEILING-FLOOR magnitude accessor on the
	 * PITCH axis for chime `id`. Returns the unsigned absolute
	 * difference, in Hz, between the catalogued audible CEILING and
	 * the catalogued audible FLOOR of the underlying Melody --
	 * `peakFreqHz(id) - troughFreqHz(id)` (always non-negative by
	 * construction, since `peakFreqHz` is a max-search and
	 * `troughFreqHz` is a min-search across the same audible-step
	 * subset, so `peak >= trough` is invariant). The audible-axis
	 * pitch sibling of `pitchSpanHz(id)` (S239) on the structural-
	 * endpoint axis: where `pitchSpanHz(id)` reports the magnitude
	 * of the (LEADING, TRAILING) STRUCTURAL endpoint pair --
	 * `|firstFreqHz(id) - lastFreqHz(id)|` -- so a caller can
	 * render an opens-vs-closes pitch tick, `audiblePitchSpanHz(id)`
	 * reports the magnitude of the (CEILING, FLOOR) AUDIBLE
	 * envelope -- `peakFreqHz(id) - troughFreqHz(id)` -- so the
	 * same caller can render an audible-pitch-range tick at the
	 * full envelope of the cue, not just at its endpoints. Builds
	 * directly on the S240 / S241 pair (`peakFreqHz` /
	 * `troughFreqHz`) that closed the audible-CEILING / FLOOR pair
	 * on the pitch axis; S255 promotes that pair to a derived
	 * MAGNITUDE accessor in the same way S239 promoted the S234 /
	 * S235 endpoint pair (`firstFreqHz` / `lastFreqHz`) to
	 * `pitchSpanHz`. The pitch-axis structural / audible / derived
	 * shape now matches: `firstFreqHz` / `lastFreqHz` (S234 /
	 * S235) at the structural endpoints, `peakFreqHz` /
	 * `troughFreqHz` (S240 / S241) at the audible CEILING /
	 * FLOOR, `pitchSpanHz` (S239) at the structural endpoint
	 * magnitude, and now `audiblePitchSpanHz` (S255) at the
	 * audible CEILING / FLOOR magnitude. So a future
	 * `PhoneDiagScreen` "Sound test" walk that wants to render
	 * the audible-pitch range of a cue (e.g. the full vertical
	 * extent of the pitch-bar between top tick and bottom tick) or
	 * a future "Settings -> Sounds -> System chimes" picker row
	 * caption like "(spans 880 Hz)" can read a dedicated derived
	 * accessor for the audible-axis envelope magnitude instead of
	 * computing `peakFreqHz(id) - troughFreqHz(id)` at the call
	 * site. Returns 0 for an out-of-range id (collapsing
	 * transparently because both `peakFreqHz(id)` and
	 * `troughFreqHz(id)` already collapse to 0 there), for the
	 * (currently impossible) empty-melody case, for the (currently
	 * impossible) all-rests-melody case (both sides collapse to 0),
	 * and -- naturally -- for any flat-pitch chime whose audible
	 * CEILING and FLOOR coincide (i.e. every audible step lands on
	 * the same frequency). Distinct from `pitchSpanHz` (structural
	 * endpoint magnitude on the pitch axis, not audible CEILING /
	 * FLOOR magnitude), distinct from `peakFreqHz` /
	 * `troughFreqHz` (the catalogued CEILING / FLOOR themselves,
	 * not the magnitude between them), distinct from `meanFreqHz`
	 * (audible-step CENTRE, not the audible-step envelope
	 * magnitude), distinct from `firstFreqHz` / `lastFreqHz`
	 * (structural endpoints, not audible bounds), distinct from
	 * `durationSpanMs` (duration axis, not pitch axis), distinct
	 * from `silhouette` (signed tilt sign, not absolute audible
	 * magnitude). Profile-state INDEPENDENT: the catalogued
	 * audible envelope magnitude is the same on SILENT / MEETING
	 * profiles as on GENERAL / OUTDOOR / HEADSET (the S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form). Cheap O(notes): two linear scans
	 * (one max, one min, mirroring the S240 / S241 implementations
	 * exactly) plus one subtraction; no engine interaction, no
	 * persisted state, no per-call allocation; mirrors
	 * `pitchSpanHz(id)` (S239) exactly with `peakFreqHz` /
	 * `troughFreqHz` substituted for `firstFreqHz` /
	 * `lastFreqHz`. Header surface grows by exactly one public
	 * symbol (`static uint16_t audiblePitchSpanHz`); the cpp adds
	 * a single function next to the existing `count` / `valid` /
	 * `name` / `melody` / `play` / `tryPlay` / `isSilenced` /
	 * `durationMs` / `noteCount` / `firstFreqHz` / `lastFreqHz` /
	 * `gapMs` / `loops` / `silhouette` / `pitchSpanHz` /
	 * `peakFreqHz` / `troughFreqHz` / `meanFreqHz` /
	 * `audibleNoteCount` / `restNoteCount` / `audibleDurationMs` /
	 * `restDurationMs` / `gapTotalMs` / `meanNoteDurationMs` /
	 * `peakNoteDurationMs` / `troughNoteDurationMs` /
	 * `firstNoteDurationMs` / `lastNoteDurationMs` /
	 * `durationSpanMs` cluster. No new includes, no new const
	 * data, no new SPIFFS asset cost. Every existing call site of
	 * the catalogue keeps byte-identical behaviour -- the new
	 * helper is purely additive. Opens the derived audible-
	 * envelope-magnitude axis on the pitch side, so the pitch axis
	 * now has its derived endpoint-magnitude accessor
	 * (`pitchSpanHz`) AND its derived audible-envelope-magnitude
	 * accessor (`audiblePitchSpanHz`) -- a level the duration axis
	 * has not yet reached on the audible side (`durationSpanMs`
	 * is the structural-endpoint sibling; the audible-envelope
	 * magnitude on the duration side, `audibleDurationSpanMs`,
	 * is the natural follow-up).
	 */
	static uint16_t audiblePitchSpanHz(uint8_t id);

	/**
	 * S256 -- derived audible-CEILING-FLOOR magnitude accessor on
	 * the DURATION axis for chime `id`. Returns the unsigned
	 * absolute difference, in milliseconds, between the catalogued
	 * audible CEILING note duration and the catalogued audible
	 * FLOOR note duration of the underlying Melody --
	 * `peakNoteDurationMs(id) - troughNoteDurationMs(id)` (always
	 * non-negative by construction, since `peakNoteDurationMs` is
	 * a max-search and `troughNoteDurationMs` is a min-search
	 * across the same audible-step subset, so `peak >= trough` is
	 * invariant). The audible-axis duration sibling of
	 * `durationSpanMs(id)` (S254) on the structural-endpoint axis:
	 * where `durationSpanMs(id)` reports the magnitude of the
	 * (LEADING, TRAILING) STRUCTURAL endpoint pair --
	 * `|firstNoteDurationMs(id) - lastNoteDurationMs(id)|` -- so
	 * a caller can render an opens-vs-closes duration tick,
	 * `audibleDurationSpanMs(id)` reports the magnitude of the
	 * (CEILING, FLOOR) AUDIBLE envelope --
	 * `peakNoteDurationMs(id) - troughNoteDurationMs(id)` -- so
	 * the same caller can render an audible-duration-range tick
	 * at the full envelope of the cue, not just at its structural
	 * endpoints. Builds directly on the S250 / S251 pair
	 * (`peakNoteDurationMs` / `troughNoteDurationMs`) that closed
	 * the audible-CEILING / FLOOR pair on the duration axis; S256
	 * promotes that pair to a derived MAGNITUDE accessor in the
	 * same way S254 promoted the S252 / S253 endpoint pair
	 * (`firstNoteDurationMs` / `lastNoteDurationMs`) to
	 * `durationSpanMs`, and in the same way S239 promoted the
	 * S234 / S235 pair (`firstFreqHz` / `lastFreqHz`) to
	 * `pitchSpanHz` and S255 promoted the S240 / S241 pair
	 * (`peakFreqHz` / `troughFreqHz`) to `audiblePitchSpanHz`.
	 * The duration-axis structural / audible / derived shape now
	 * matches the pitch-axis shape: `firstNoteDurationMs` /
	 * `lastNoteDurationMs` (S252 / S253) at the structural
	 * endpoints, `peakNoteDurationMs` / `troughNoteDurationMs`
	 * (S250 / S251) at the audible CEILING / FLOOR,
	 * `durationSpanMs` (S254) at the structural endpoint
	 * magnitude, and now `audibleDurationSpanMs` (S256) at the
	 * audible CEILING / FLOOR magnitude -- closing the symmetry
	 * with the pitch axis, where `pitchSpanHz` (S239) and
	 * `audiblePitchSpanHz` (S255) already cover the same two
	 * derived corners. So a future `PhoneDiagScreen` "Sound test"
	 * walk that wants to render the audible-duration range of a
	 * cue (e.g. the full horizontal extent of a duration-bar
	 * between longest-note tick and shortest-note tick) or a
	 * future "Settings -> Sounds -> System chimes" picker row
	 * caption like "(spans 320 ms)" can read a dedicated derived
	 * accessor for the audible-axis envelope magnitude instead of
	 * computing `peakNoteDurationMs(id) - troughNoteDurationMs(id)`
	 * at the call site. Returns 0 for an out-of-range id
	 * (collapsing transparently because both
	 * `peakNoteDurationMs(id)` and `troughNoteDurationMs(id)`
	 * already collapse to 0 there), for the (currently impossible)
	 * empty-melody case, for the (currently impossible) all-rests-
	 * melody case (both sides collapse to 0), and -- naturally --
	 * for any flat-duration chime whose audible CEILING and FLOOR
	 * note durations coincide (i.e. every audible step lasts the
	 * same number of milliseconds). Distinct from `durationSpanMs`
	 * (structural endpoint magnitude on the duration axis, not
	 * audible CEILING / FLOOR magnitude), distinct from
	 * `peakNoteDurationMs` / `troughNoteDurationMs` (the catalogued
	 * CEILING / FLOOR themselves, not the magnitude between them),
	 * distinct from `meanNoteDurationMs` (audible-step CENTRE,
	 * not envelope magnitude), distinct from `firstNoteDurationMs`
	 * / `lastNoteDurationMs` (structural endpoints, not audible
	 * bounds), distinct from `audiblePitchSpanHz` (pitch axis,
	 * not duration axis), distinct from `audibleDurationMs`
	 * (audible-step duration TOTAL, not envelope magnitude),
	 * distinct from `silhouette` (signed tilt sign, not absolute
	 * audible magnitude). Profile-state INDEPENDENT: the
	 * catalogued audible envelope magnitude is the same on SILENT
	 * / MEETING profiles as on GENERAL / OUTDOOR / HEADSET (the
	 * S231 `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form). Cheap O(notes): two linear scans
	 * (one max, one min, mirroring the S250 / S251 implementations
	 * exactly) plus one subtraction; no engine interaction, no
	 * persisted state, no per-call allocation; mirrors
	 * `audiblePitchSpanHz(id)` (S255) exactly with
	 * `peakNoteDurationMs` / `troughNoteDurationMs` substituted
	 * for `peakFreqHz` / `troughFreqHz`. Header surface grows by
	 * exactly one public symbol (`static uint16_t
	 * audibleDurationSpanMs`); the cpp adds a single function
	 * next to the existing `count` / `valid` / `name` / `melody` /
	 * `play` / `tryPlay` / `isSilenced` / `durationMs` /
	 * `noteCount` / `firstFreqHz` / `lastFreqHz` / `gapMs` /
	 * `loops` / `silhouette` / `pitchSpanHz` / `peakFreqHz` /
	 * `troughFreqHz` / `meanFreqHz` / `audibleNoteCount` /
	 * `restNoteCount` / `audibleDurationMs` / `restDurationMs` /
	 * `gapTotalMs` / `meanNoteDurationMs` / `peakNoteDurationMs` /
	 * `troughNoteDurationMs` / `firstNoteDurationMs` /
	 * `lastNoteDurationMs` / `durationSpanMs` /
	 * `audiblePitchSpanHz` cluster. No new includes, no new const
	 * data, no new SPIFFS asset cost. Every existing call site of
	 * the catalogue keeps byte-identical behaviour -- the new
	 * helper is purely additive. Closes the derived audible-
	 * envelope-magnitude axis on the duration side at full
	 * symmetry with the pitch side already closed by
	 * `audiblePitchSpanHz`; the duration axis now has its derived
	 * endpoint-magnitude accessor (`durationSpanMs`) AND its
	 * derived audible-envelope-magnitude accessor
	 * (`audibleDurationSpanMs`), bringing the duration axis level
	 * with the pitch axis at the four-corner derived-magnitude
	 * grid (structural-endpoint + audible-envelope on each axis).
	 */
	static uint16_t audibleDurationSpanMs(uint8_t id);

	/**
	 * S257 - derived per-rest-step mean-duration accessor for chime
	 * `id`. Returns the catalogued rest-step durations the engine
	 * holds the piezo SILENT for (the sum `restDurationMs(id)` (S247)
	 * reports) divided by the count of those rest steps (the count
	 * `restNoteCount(id)` (S244) reports), i.e. the mean wall-clock
	 * duration per REST catalogued step, in ms, rounded toward zero
	 * by integer division. Where `restDurationMs(id)` reports the
	 * SUM of the catalogued rest-step durations and
	 * `restNoteCount(id)` reports the COUNT of those steps,
	 * `meanRestDurationMs(id)` collapses the pair into a per-step
	 * CENTRE -- the rest-axis sibling of `meanNoteDurationMs(id)`
	 * (S249) on the audible-step axis, exactly how
	 * `restDurationMs(id)` (S247) is the rest-axis sibling of
	 * `audibleDurationMs(id)` (S246) on the audible-step axis.
	 *
	 * So a future "Settings -> Sounds -> System chimes" picker row
	 * caption like "(2 rests, ~80 ms each)" or a future
	 * `PhoneDiagScreen` chime-roster footer line that wants to
	 * render the mean per-rest hold of a cue can read a dedicated
	 * accessor for the per-rest-step centre instead of computing
	 * `restDurationMs(id) / restNoteCount(id)` (with a
	 * divide-by-zero guard) at the call site. Returns 0 for an
	 * out-of-range id, for the (currently impossible) empty-melody
	 * case, for the no-rests-melody case (where
	 * `restNoteCount(id) == 0` and the divisor would be zero) -- so
	 * a flat audible-only cue with no rests, or an out-of-range id,
	 * collapse to the same 0 the catalogued rest-axis pair
	 * `restNoteCount` (S244) / `restDurationMs` (S247) already
	 * collapse to.
	 *
	 * Saturates at `0xFFFF` ms (the same uint16_t ceiling the
	 * duration cluster `durationMs` / `audibleDurationMs` /
	 * `restDurationMs` / `gapTotalMs` / `meanNoteDurationMs` /
	 * `peakNoteDurationMs` / `troughNoteDurationMs` /
	 * `firstNoteDurationMs` / `lastNoteDurationMs` /
	 * `durationSpanMs` / `audibleDurationSpanMs` already share) so
	 * a picker row caption can render the value as a four-digit
	 * integer without an int cast at the call site; in practice no
	 * realistic per-rest-step duration approaches that ceiling, but
	 * the saturate-on-overflow guard keeps the return type honest.
	 *
	 * Distinct from `restDurationMs` (rest-step SUM in isolation,
	 * not per-step centre), distinct from `restNoteCount` (rest-step
	 * COUNT in isolation, not per-step centre), distinct from
	 * `meanNoteDurationMs` (audible-step CENTRE on the
	 * audible-duration axis, not rest-duration axis), distinct from
	 * `audibleDurationMs` (audible-step SUM in isolation), distinct
	 * from `durationMs` (TOTAL incl. audible + rests + gaps),
	 * distinct from `gapTotalMs` (inter-step gap component in
	 * aggregate, not per-rest centre), distinct from `gapMs`
	 * (per-step inter-step filler in isolation, not per-rest
	 * centre), distinct from `peakNoteDurationMs` /
	 * `troughNoteDurationMs` (audible-axis CEILING / FLOOR per-step
	 * extents, not rest-axis CENTRE), distinct from
	 * `firstNoteDurationMs` / `lastNoteDurationMs` (structural
	 * endpoints, not rest-step centre), distinct from
	 * `durationSpanMs` / `audibleDurationSpanMs` (derived endpoint
	 * / envelope MAGNITUDES on the audible-duration axis, not
	 * rest-axis centre), and distinct from
	 * `PhoneRingtoneEngine::isPlaying()` / `currentFreq()` (the
	 * S191 live-piezo accessors that report runtime playback state,
	 * not catalogued shape). Profile-state INDEPENDENT: the
	 * catalogued per-rest-step mean is the same on SILENT / MEETING
	 * profiles as on GENERAL / OUTDOOR / HEADSET (the S231
	 * `tryPlay(id)` gate already reports the silenced answer
	 * separately for any caller that wants to fade the row caption
	 * into a "(silenced)" form).
	 *
	 * Cheap O(notes) linear scan with two uint32_t accumulators
	 * (rest-step duration sum + rest-step counter) and one final
	 * divide-and-saturate; no per-call allocation, no recursion
	 * into the existing `restDurationMs` / `restNoteCount`
	 * accessors -- the loop fuses the two passes so the catalogued
	 * `Note*` array is walked exactly once per call. Mirrors
	 * `meanNoteDurationMs(id)` (S249) exactly with the rest-step
	 * predicate (`freq == 0`) substituted for the audible-step
	 * predicate (`freq != 0`). Header surface grows by exactly one
	 * public symbol (`static uint16_t meanRestDurationMs`); the cpp
	 * adds a single function next to the existing `count` /
	 * `valid` / `name` / `melody` / `play` / `tryPlay` /
	 * `isSilenced` / `durationMs` / `noteCount` / `firstFreqHz` /
	 * `lastFreqHz` / `gapMs` / `loops` / `silhouette` /
	 * `pitchSpanHz` / `peakFreqHz` / `troughFreqHz` / `meanFreqHz`
	 * / `audibleNoteCount` / `restNoteCount` / `audibleDurationMs`
	 * / `restDurationMs` / `gapTotalMs` / `meanNoteDurationMs` /
	 * `peakNoteDurationMs` / `troughNoteDurationMs` /
	 * `firstNoteDurationMs` / `lastNoteDurationMs` /
	 * `durationSpanMs` / `audiblePitchSpanHz` /
	 * `audibleDurationSpanMs` cluster. No new includes, no new
	 * const data, no new SPIFFS asset cost. Every existing call
	 * site of the catalogue keeps byte-identical behaviour -- the
	 * new helper is purely additive. Opens the rest-axis CENTRE
	 * accessor next to the audible-axis CENTRE
	 * (`meanNoteDurationMs`); the natural follow-up is the
	 * rest-axis CEILING / FLOOR pair (`peakRestDurationMs` /
	 * `troughRestDurationMs`) that mirrors the audible-axis
	 * `peakNoteDurationMs` / `troughNoteDurationMs` cluster.
	 */
	static uint16_t meanRestDurationMs(uint8_t id);
};

#endif // MAKERPHONE_PHONESYSTEMTONES_H

