#include "PhoneSystemTones.h"

#include <Notes.h>
#include <Settings.h>

// =====================================================================
// S192 — PhoneSystemTonesLib
//
// Eighteen short system chimes, every one a 1..6 note Melody pulled
// from `Notes.h`. The note arrays live in this translation unit's
// anonymous namespace so the linker keeps them as a single read-only
// blob — same pattern S148/S149/S150/S157 use for their chime
// melodies. Sizes stay small (largest is six notes) so the total cost
// of the catalogue is well under a kilobyte of flash.
//
// Design rules used to pick pitches and lengths:
//
//   * "Positive" cues (Success, Save, Unlock, NetworkOk, LevelUp,
//      TimerDone, AlarmDismiss) ascend.
//   * "Negative" cues (Error, NetworkFail, LowBattery, DeleteItem,
//      CallEnd) descend.
//   * Equal-pitch pip pairs (Notify, SmsReceived) cue
//      "something arrived" without picking a direction.
//   * Single-note pulses (MenuOpen, MenuClose, Alert) cue UI events
//      where a melody would be overkill.
//   * MenuOpen rises by a major third, MenuClose falls by a major
//      third, so the two are mirror images.
//   * No system chime borrows the silhouette of any S148/S149/S150
//      chime — this catalogue is meant to coexist with them, not
//      collide. The boot/power/charge family stays the property of
//      its dedicated services.
//
// Total chime length stays under 200 ms for every entry so the audio
// always lands inside the same perceptual frame as the UI animation
// the chime is reinforcing (toast, screen-pop, save flash, etc.).
// =====================================================================

namespace {

using Note   = PhoneRingtoneEngine::Note;
using Melody = PhoneRingtoneEngine::Melody;

// ---- Notify - generic notification ping (two equal mid-high pips) ----
const Note kNotifyNotes[] = {
	{ NOTE_E6, 35 },
	{ NOTE_E6, 35 },
};

// ---- Success - rising minor third (cheerful, quick) ----
const Note kSuccessNotes[] = {
	{ NOTE_C6, 50 },
	{ NOTE_E6, 70 },
};

// ---- Error - descending major second (a soft "uh-oh", not a buzzer) ----
const Note kErrorNotes[] = {
	{ NOTE_F5, 60 },
	{ NOTE_D5, 90 },
};

// ---- Alert - single sharp pulse, top of the piezo's clean range ----
const Note kAlertNotes[] = {
	{ NOTE_A6, 90 },
};

// ---- Unlock - quick rising perfect fifth ("opened up") ----
const Note kUnlockNotes[] = {
	{ NOTE_C5, 35 },
	{ NOTE_G5, 55 },
};

// ---- Lock - falling perfect fifth ("closed down") ----
const Note kLockNotes[] = {
	{ NOTE_G5, 35 },
	{ NOTE_C5, 55 },
};

// ---- SmsReceived - two pips at a higher pitch than Notify so the
//      two cues are clearly different even from the next room ----
const Note kSmsReceivedNotes[] = {
	{ NOTE_G6, 30 },
	{ NOTE_G6, 30 },
};

// ---- CallEnd - descending three-note "click-clack-clunk" ----
const Note kCallEndNotes[] = {
	{ NOTE_E6, 40 },
	{ NOTE_C6, 50 },
	{ NOTE_A5, 70 },
};

// ---- MenuOpen - one note, rising compared to MenuClose ----
const Note kMenuOpenNotes[] = {
	{ NOTE_E6, 30 },
};

// ---- MenuClose - one note, lower than MenuOpen ----
const Note kMenuCloseNotes[] = {
	{ NOTE_C6, 30 },
};

// ---- Save - rising "ka-ching" three-note arpeggio ----
const Note kSaveNotes[] = {
	{ NOTE_C6, 35 },
	{ NOTE_E6, 35 },
	{ NOTE_G6, 60 },
};

// ---- DeleteItem - falling "ka-thunk" two notes ----
const Note kDeleteItemNotes[] = {
	{ NOTE_E6, 40 },
	{ NOTE_A5, 70 },
};

// ---- TimerDone - rising-then-flat triplet, classic "ding ding ding" ----
const Note kTimerDoneNotes[] = {
	{ NOTE_C6, 50 },
	{ NOTE_C6, 50 },
	{ NOTE_E6, 80 },
};

// ---- AlarmDismiss - calm rising minor third ("ok, off") ----
const Note kAlarmDismissNotes[] = {
	{ NOTE_E5, 50 },
	{ NOTE_G5, 80 },
};

// ---- LevelUp - chunky four-note major-chord arpeggio ----
const Note kLevelUpNotes[] = {
	{ NOTE_C5, 40 },
	{ NOTE_E5, 40 },
	{ NOTE_G5, 40 },
	{ NOTE_C6, 70 },
};

// ---- NetworkOk - rising perfect fourth ("connected") ----
const Note kNetworkOkNotes[] = {
	{ NOTE_C6, 40 },
	{ NOTE_F6, 80 },
};

// ---- NetworkFail - falling perfect fourth ("disconnected") ----
const Note kNetworkFailNotes[] = {
	{ NOTE_F6, 40 },
	{ NOTE_C6, 80 },
};

// ---- LowBattery - three falling pips (warning silhouette) ----
const Note kLowBatteryNotes[] = {
	{ NOTE_E5, 60 },
	{ NOTE_D5, 60 },
	{ NOTE_C5, 90 },
};

// Per-melody wrappers. We name each one so PhoneRingtoneEngine's
// `currentName()` accessor reports a useful string for debug + for
// any future "now playing" overlay.
const Melody kMelodies[PhoneSystemTones::Count] = {
	{ kNotifyNotes,       sizeof(kNotifyNotes)       / sizeof(Note), 35, false, "Notify"      },
	{ kSuccessNotes,      sizeof(kSuccessNotes)      / sizeof(Note), 25, false, "Success"     },
	{ kErrorNotes,        sizeof(kErrorNotes)        / sizeof(Note), 30, false, "Error"       },
	{ kAlertNotes,        sizeof(kAlertNotes)        / sizeof(Note),  0, false, "Alert"       },
	{ kUnlockNotes,       sizeof(kUnlockNotes)       / sizeof(Note), 20, false, "Unlock"      },
	{ kLockNotes,         sizeof(kLockNotes)         / sizeof(Note), 20, false, "Lock"        },
	{ kSmsReceivedNotes,  sizeof(kSmsReceivedNotes)  / sizeof(Note), 50, false, "SMS"         },
	{ kCallEndNotes,      sizeof(kCallEndNotes)      / sizeof(Note), 25, false, "CallEnd"     },
	{ kMenuOpenNotes,     sizeof(kMenuOpenNotes)     / sizeof(Note),  0, false, "MenuOpen"    },
	{ kMenuCloseNotes,    sizeof(kMenuCloseNotes)    / sizeof(Note),  0, false, "MenuClose"   },
	{ kSaveNotes,         sizeof(kSaveNotes)         / sizeof(Note), 20, false, "Save"        },
	{ kDeleteItemNotes,   sizeof(kDeleteItemNotes)   / sizeof(Note), 30, false, "Delete"      },
	{ kTimerDoneNotes,    sizeof(kTimerDoneNotes)    / sizeof(Note), 60, false, "TimerDone"   },
	{ kAlarmDismissNotes, sizeof(kAlarmDismissNotes) / sizeof(Note), 30, false, "AlarmDismiss"},
	{ kLevelUpNotes,      sizeof(kLevelUpNotes)      / sizeof(Note), 25, false, "LevelUp"     },
	{ kNetworkOkNotes,    sizeof(kNetworkOkNotes)    / sizeof(Note), 30, false, "NetworkOk"   },
	{ kNetworkFailNotes,  sizeof(kNetworkFailNotes)  / sizeof(Note), 30, false, "NetworkFail" },
	{ kLowBatteryNotes,   sizeof(kLowBatteryNotes)   / sizeof(Note), 70, false, "LowBattery"  },
};

// Display labels track Id by index. Same length as kMelodies; kept
// separate so the picker UI can show shorter / friendlier names than
// the engine's internal melody name field.
const char* const kNames[PhoneSystemTones::Count] = {
	"NOTIFY",       "SUCCESS",     "ERROR",      "ALERT",
	"UNLOCK",       "LOCK",        "SMS",        "CALL END",
	"MENU OPEN",    "MENU CLOSE",  "SAVE",       "DELETE",
	"TIMER DONE",   "ALARM OFF",   "LEVEL UP",   "NETWORK OK",
	"NETWORK FAIL", "LOW BATTERY",
};

} // namespace

uint8_t PhoneSystemTones::count(){
	return (uint8_t) Count;
}

bool PhoneSystemTones::valid(uint8_t id){
	return id < (uint8_t) Count;
}

const char* PhoneSystemTones::name(uint8_t id){
	if(!valid(id)) return "?";
	return kNames[id];
}

const PhoneRingtoneEngine::Melody* PhoneSystemTones::melody(uint8_t id){
	if(!valid(id)) return nullptr;
	return &kMelodies[id];
}

void PhoneSystemTones::play(uint8_t id){
	// S231 - the gate / valid-id checks (and the engine handoff) live
	// inside `tryPlay(id)` so this fire-and-forget entry point and
	// the new bool-returning `tryPlay` cannot drift out of sync. The
	// pre-S231 body inlined `valid` + `isSilenced` + `Ringtone.play`
	// here; calling tryPlay(id) is byte-identical at the engine call
	// boundary (same valid() early-return, same isSilenced() early-
	// return, same Ringtone.play(kMelodies[id]) invocation, no
	// persisted state) so every existing `PhoneSystemTones::play()`
	// call site in the firmware (S110 PhoneCallEnded, S134 PhoneSimon,
	// S141 PhoneAlarmClock, etc.) keeps the same observable behaviour
	// without any per-site change.
	(void) tryPlay(id);
}

bool PhoneSystemTones::tryPlay(uint8_t id){
	if(!valid(id)) return false;
	// S230 - SILENT / MEETING profile gate. PhoneRingtoneEngine
	// internally short-circuits the piezo when Settings.sound is
	// false (see emitTone()), BUT the engine self-mutes per-loop, so
	// the micro-window between `Ringtone.play()` and the engine's
	// first mute pass is enough for some Chatter units to emit an
	// audible blip before falling silent. Closing the window here
	// brings the eighteen-cue catalogue into the same convention the
	// S205 / S219-S229 sweep used for every other non-alarm
	// `Ringtone.play()` call site - skip the engine call entirely
	// under a silenced profile so the LoopManager listener is never
	// registered. The catalogue itself stays loud: `melody(id)` still
	// returns the same const Melody* regardless of profile state, so
	// any PRE-LOAD call site that grabs the structure (notably
	// PhoneIncomingCall) is untouched - only the central
	// `play(uint8_t id)` / `tryPlay(uint8_t id)` entry points gate.
	if(isSilenced()) return false;
	// Routing through the engine means a Settings.sound mute is
	// ALSO honoured by the engine itself as a defence-in-depth (see
	// PhoneRingtoneEngine::emitTone), and any active melody (call
	// ringer, music player) replaces this one-shot so latched
	// higher-priority audio always wins.
	Ringtone.play(kMelodies[id]);
	// S231 - report back to the caller that the engine call fired.
	// Foreshadowed by the S192 / S230 commit bodies' "future Settings
	// -> Sounds -> System chimes picker" design notes: a preview row
	// in that picker wants to fall back to a "(silenced)" caption
	// when the active profile gates the cue out, and a future diag
	// screen that walks every chime in turn wants the same answer
	// for the same reason. Distinct from `play()` so the existing
	// fire-and-forget entry point keeps its void signature -- every
	// pre-S231 PhoneSystemTones::play() call site in the firmware
	// stays compile-clean without per-site adaptation.
	return true;
}

// S230 - SILENT / MEETING profile gate. PhoneProfileScreen (S159)
// writes `Settings.get().sound = false` for both Silent and Meeting
// profiles and `true` for General / Outdoor / Headset, so reading
// the legacy bool is the cheapest one-read cover for "should the
// system-tones catalogue drive the piezo right now" without
// dragging the five-state ProfileService::Profile enum into this
// library. Same pattern S205 / S219-S229 use for their per-screen /
// per-modal / per-service helpers; PhoneSystemTones was the last
// remaining non-alarm `Ringtone.play()` call site that S229's
// commit body had explicitly deferred to its own slower-rollout
// session because the central `play(uint8_t id)` entry point
// touches every UI cue in the firmware at once. The
// PhoneAlarmService alarm engine intentionally stays un-gated by
// design: alarm audio MUST fire even on a Silent or Meeting
// profile so the user can still wake up. Likewise the S148 boot
// chime and S149 power-off arpeggio stay un-gated for their usual
// reasons (boot-chime can fire before Settings has been hydrated
// from NVS, power-off arpeggio is a deliberate user-confirmation
// cue for a destructive action).
bool PhoneSystemTones::isSilenced(){
	return !Settings.get().sound;
}

// S232 - total catalogued playback duration of chime `id` in ms.
// Sum of per-Note `durationMs` entries plus the inter-note `gapMs`
// waits that PhoneRingtoneEngine::loop() interleaves between
// consecutive notes. Reading the engine source (loop() in
// PhoneRingtoneEngine.cpp): when `gapMs > 0` the inGap branch
// consumes one gap AFTER every note (including the last one,
// before step++ pushes step >= count and stop() runs), so the
// catalogue answer is `sum + count * gapMs` for non-zero gaps and
// just `sum` for zero gaps. UINT32 accumulator with a final
// UINT16_MAX clamp for forward-compatibility (the largest current
// catalogue entry, LowBattery, is ~420 ms -- well under the clamp
// limit). Profile-state INDEPENDENT: the answer is the same on
// SILENT / MEETING profiles as on GENERAL / OUTDOOR / HEADSET so
// the foreshadowed picker can debounce row presses on a stable
// catalogued duration regardless of what `tryPlay(id)` reports.
uint16_t PhoneSystemTones::durationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint32_t total = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		total += (uint32_t) m.notes[i].durationMs;
	}
	if(m.gapMs > 0){
		total += (uint32_t) m.gapMs * (uint32_t) m.count;
	}
	if(total > 0xFFFFU) return 0xFFFFU;
	return (uint16_t) total;
}

// S233 - structural note-count accessor for chime `id`. Returns the
// number of catalogued PhoneRingtoneEngine::Note entries in the
// underlying Melody (kMelodies[id].count). Returns 0 for an
// out-of-range id and for the (currently impossible) empty-melody
// case. Foreshadowed by the S192 / S230 / S231 / S232 commit
// bodies' "future Settings -> Sounds -> System chimes picker" and
// "future PhoneDiagScreen Sound test entry that walks every chime"
// design notes -- a row caption that wants to render
// "(N notes, M ms)" can pair `noteCount(id)` with `durationMs(id)`
// without touching the const Melody* pointer at the call site.
// Profile-state INDEPENDENT: the catalogued shape is the same on
// SILENT / MEETING profiles as on GENERAL / OUTDOOR / HEADSET, so
// a picker can lay out row labels at construction time and leave
// them unchanged when the user toggles profiles. Cheap O(1) struct
// field read; no engine interaction, no persisted state, no per-
// call allocation; mirrors the existing `count`/`valid`/`name`/
// `melody`/`play`/`tryPlay`/`isSilenced`/`durationMs` cluster.
uint16_t PhoneSystemTones::noteCount(uint8_t id){
	if(!valid(id)) return 0;
	return kMelodies[id].count;
}

// S234 - structural first-note pitch accessor for chime `id`. Returns
// the catalogued frequency in Hz of the FIRST
// PhoneRingtoneEngine::Note entry in the underlying Melody
// (kMelodies[id].notes[0].freq). Returns 0 for an out-of-range id, for
// the (currently impossible) empty-melody case, and -- transparently
// -- for the (currently impossible) leading-rest case (a Note with
// freq == 0 is the catalogue's encoding for a silent step; no v1
// chime opens with a rest, so the answer collapses to "the catalogued
// first audible note's pitch" for every entry that ships today, while
// staying unambiguous if a future chime ever opens with a rest --
// 0 is the same value the engine itself uses to mean "no tone is
// being driven right now"). Foreshadowed by the S233 commit body's
// "future firstFreqHz(id) accessor for the pitch indicator" design
// note -- the foreshadowed picker's preview row uses noteCount(id)
// for the dotted-timeline dot row and this accessor for the leading-
// pitch indicator (the only catalogued differentiator between equal-
// shape rows like Notify vs. SmsReceived, both two-pip pairs with the
// same noteCount/durationMs but at NOTE_E6 / NOTE_G6 respectively).
// Profile-state INDEPENDENT: the catalogued first-note frequency is
// the same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
// HEADSET, so a picker can render its pitch indicator at construction
// time and leave it unchanged when the user toggles profiles. Cheap
// O(1) struct field read; no engine interaction, no persisted state,
// no per-call allocation; mirrors the existing count / valid / name /
// melody / play / tryPlay / isSilenced / durationMs / noteCount
// cluster. Distinct from PhoneRingtoneEngine::currentFreq() (the S191
// live-piezo accessor) -- that helper reports the LIVE frequency the
// engine is driving right now, the catalogue answer reports the FIRST
// catalogued note regardless of whether the engine is playing. Both
// are useful and live at different layers -- neither subsumes the
// other.
uint16_t PhoneSystemTones::firstFreqHz(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	return m.notes[0].freq;
}

// S235 - structural last-note pitch accessor for chime `id`. Returns
// the catalogued frequency in Hz of the LAST PhoneRingtoneEngine::Note
// entry in the underlying Melody (kMelodies[id].notes[count - 1].freq).
// Returns 0 for an out-of-range id, for the (currently impossible)
// empty-melody case, and -- transparently -- for the (currently
// impossible) trailing-rest case (a Note with freq == 0 is the
// catalogue's encoding for a silent step; no v1 chime closes on a
// rest, so the answer collapses to "the catalogued last audible
// note's pitch" for every entry that ships today, while staying
// unambiguous if a future chime ever closes on a rest -- 0 is the
// same value the engine itself uses to mean "no tone is being driven
// right now"). Foreshadowed by the S234 commit body's "rising /
// falling silhouette" framing -- where firstFreqHz(id) reports the
// leading pitch (the only catalogued differentiator between equal-
// shape pip pairs like Notify [NOTE_E6, NOTE_E6] vs SmsReceived
// [NOTE_G6, NOTE_G6]), lastFreqHz(id) reports the trailing pitch so
// the foreshadowed picker can render an up / down / level direction
// arrow by comparing the two answers without walking the catalogued
// Note array at the call site: first<last for ascending cues
// (Success, Unlock, Save, NetworkOk, LevelUp, AlarmDismiss),
// first>last for descending cues (Error, Lock, CallEnd, DeleteItem,
// NetworkFail, LowBattery), first==last for level cues (Notify,
// Alert, SmsReceived, MenuOpen, MenuClose, TimerDone). That trio
// matches the silhouette grouping the cpp comment block at the top
// of this file documents -- "positive cues ascend / negative cues
// descend / equal-pitch pip pairs cue something arrived without
// picking a direction". Profile-state INDEPENDENT: the catalogued
// last-note frequency is the same on SILENT / MEETING profiles as
// on GENERAL / OUTDOOR / HEADSET, so a picker can render its
// direction arrow at construction time and leave it unchanged when
// the user toggles profiles. Cheap O(1) array-tail field read; no
// engine interaction, no persisted state, no per-call allocation;
// mirrors the existing count / valid / name / melody / play /
// tryPlay / isSilenced / durationMs / noteCount / firstFreqHz
// cluster. Distinct from PhoneRingtoneEngine::currentFreq() (the
// S191 live-piezo accessor) -- that helper reports the LIVE
// frequency the engine is driving right now, the catalogue answer
// reports the LAST catalogued note regardless of whether the engine
// is playing. Both are useful and live at different layers --
// neither subsumes the other.
uint16_t PhoneSystemTones::lastFreqHz(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	return m.notes[m.count - 1].freq;
}

// S236 - structural inter-note gap accessor for chime `id`. Returns
// the catalogued `gapMs` field of the underlying
// PhoneRingtoneEngine::Melody (kMelodies[id].gapMs), the wait the
// engine's loop() interleaves between consecutive notes when stepping
// through the melody. Returns 0 for an out-of-range id (which happens
// to be the same value the engine itself uses to mean "no inter-note
// silence" -- the Alert / MenuOpen / MenuClose entries already ship
// with gapMs = 0 because they are single-note pulses, so a 0 answer
// for an invalid id is indistinguishable from the catalogued single-
// pulse entries and the caller's code path collapses cleanly).
// Foreshadowed by the S232 / S233 / S234 / S235 commit bodies' "future
// Settings -> Sounds -> System chimes picker" and "future
// PhoneDiagScreen Sound test entry that walks every chime" design
// notes -- the picker's preview row already pairs noteCount(id) (S233)
// with durationMs(id) (S232) for the "(N notes, M ms)" caption and
// firstFreqHz(id) (S234) with lastFreqHz(id) (S235) for the rising /
// falling / level direction arrow; gapMs(id) closes the structural
// field set by exposing the third and final catalogued field on the
// Melody struct that had not yet surfaced at the accessor layer
// (notes is exposed via firstFreqHz / lastFreqHz, count is exposed
// via noteCount, the loop flag is moot for the v1 catalogue, name is
// exposed via name, and gapMs was the remaining invisible field). The
// picker can use gapMs(id) to render a per-row "tempo" indicator -- a
// dotted timeline whose dots are spaced by the catalogued gap value
// rather than evenly -- giving cues like Notify (35 ms), SmsReceived
// (50 ms) and TimerDone (60 ms) a third axis of visual differentiation
// beyond name + duration + note count + pitch silhouette. Distinct
// from durationMs(id) (S232): that helper reports the TOTAL playback
// duration including the catalogued gaps (sum of per-Note durationMs
// plus count * gapMs when gapMs > 0), while gapMs(id) reports the
// per-step gap as a structural field. Profile-state INDEPENDENT: the
// catalogued gap is the same on SILENT / MEETING profiles as on
// GENERAL / OUTDOOR / HEADSET, so a picker can lay out its tempo
// indicator at construction time and leave it unchanged when the user
// toggles profiles. Cheap O(1) struct field read; no engine
// interaction, no persisted state, no per-call allocation; mirrors
// the existing count / valid / name / melody / play / tryPlay /
// isSilenced / durationMs / noteCount / firstFreqHz / lastFreqHz
// cluster.
uint16_t PhoneSystemTones::gapMs(uint8_t id){
	if(!valid(id)) return 0;
	return kMelodies[id].gapMs;
}

// S237 - structural loop-flag accessor for chime `id`. Returns the
// catalogued `loop` field of the underlying
// PhoneRingtoneEngine::Melody (kMelodies[id].loop), the boolean the
// engine consults at the end of one playthrough to decide whether to
// restart from step 0 (loop the cue indefinitely) or stop() and
// release the LoopManager listener (one-shot cue). Returns false for
// an out-of-range id, which is the same answer a non-existent chime
// would naturally give -- a no-op cannot loop -- and matches the
// catalogued answer for every v1 entry today (no system chime loops,
// by design: looping is reserved for the call-ringer family in
// PhoneRingtones.cpp). Foreshadowed by the S236 commit body's note
// that the loop flag had been left without an accessor of its own
// because no v1 chime opted into looping; S237 promotes that deferred
// field to first-class accessor parity with the rest of the
// structural surface so the foreshadowed picker has the FULL Melody-
// struct field set behind dedicated accessors (noteCount for count,
// firstFreqHz/lastFreqHz for the leading and trailing notes entries,
// gapMs for gapMs, name for name, loops for loop), and so the
// foreshadowed PhoneDiagScreen "Sound test" entry can fall back to a
// fixed preview window for any future looping entry rather than
// hanging on durationMs(id) alone (a looping entry never completes
// on its own, so a diag walk that uses durationMs as a row-press
// debounce would deadlock without the loops(id) escape hatch).
// Distinct from a hypothetical PhoneRingtoneEngine::isLooping() live
// accessor: even if such a helper existed it would report whether
// the engine is CURRENTLY looping, while the catalogue answer reports
// whether the Melody opted into looping at construction time
// regardless of engine state. Profile-state INDEPENDENT: the
// catalogued loop flag is the same on SILENT / MEETING profiles as
// on GENERAL / OUTDOOR / HEADSET, so the foreshadowed picker can
// render its loops indicator at construction time and leave it
// unchanged when the user toggles profiles. Cheap O(1) struct field
// read; no engine interaction, no persisted state, no per-call
// allocation; mirrors the existing count / valid / name / melody /
// play / tryPlay / isSilenced / durationMs / noteCount /
// firstFreqHz / lastFreqHz / gapMs cluster.
bool PhoneSystemTones::loops(uint8_t id){
	if(!valid(id)) return false;
	return kMelodies[id].loop;
}

// S238 - derived ascending / level / descending pitch-direction
// accessor for chime `id`. Returns +1 if firstFreqHz(id) < lastFreqHz(id)
// (ascending silhouette: Success, Unlock, Save, NetworkOk, LevelUp,
// AlarmDismiss, TimerDone today), -1 if firstFreqHz(id) >
// lastFreqHz(id) (descending silhouette: Error, Lock, CallEnd,
// DeleteItem, NetworkFail, LowBattery today), 0 otherwise (level
// silhouette: Notify, Alert, SmsReceived, MenuOpen, MenuClose today;
// also the answer for an out-of-range id, which collapses cleanly to
// the level case so a non-existent chime is indistinguishable from a
// level entry at the call site and a picker rendering the direction
// arrow does not need to special-case the invalid id path).
// Foreshadowed by the S234 / S235 commit bodies' "rising / falling
// silhouette" framing -- where firstFreqHz(id) reports the leading
// pitch and lastFreqHz(id) reports the trailing pitch, the
// foreshadowed picker / diag walk uses the comparison to render a
// direction arrow / colour-code by silhouette. S238 promotes that
// caller-side arithmetic step to a dedicated derived accessor that
// returns the catalogued direction in one call, mirroring the S232
// durationMs(id) precedent of exposing a derived answer rather than
// a raw struct field where the derived form is the one the caller
// actually wants. The trio of categorisations matches the silhouette
// grouping the cpp comment block at the top of this file documents
// ("Positive cues ascend / Negative cues descend / Equal-pitch pip
// pairs cue something arrived without picking a direction"), so the
// accessor reproduces that grouping at the API layer. Distinct from
// firstFreqHz(id) / lastFreqHz(id) themselves: those helpers stay so
// a caller that wants the absolute pitch values can still read them
// directly. Profile-state INDEPENDENT: the catalogued silhouette is
// the same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
// HEADSET. Cheap O(1) two-field comparison via firstFreqHz(id) /
// lastFreqHz(id) so the rest-aware semantics those accessors already
// implement (a leading or trailing rest is encoded as freq == 0 in
// the catalogue) feed straight into the silhouette answer without
// re-deriving them here -- if a future entry ever opens or closes on
// a rest the silhouette collapses transparently to whichever side
// the catalogued audible note dominates.
int8_t PhoneSystemTones::silhouette(uint8_t id){
	if(!valid(id)) return 0;
	const uint16_t first = firstFreqHz(id);
	const uint16_t last  = lastFreqHz(id);
	if(first < last) return 1;
	if(first > last) return -1;
	return 0;
}

// S239 - derived pitch-span accessor for chime `id`. Returns the
// absolute frequency interval, in Hz, between the catalogued first
// note and the catalogued last note of the underlying
// PhoneRingtoneEngine::Melody (|firstFreqHz(id) - lastFreqHz(id)|).
// Returns 0 for an out-of-range id, for the (currently impossible)
// empty-melody case, and for every level silhouette in the catalogue
// (Notify, Alert, SmsReceived, MenuOpen, MenuClose -- where
// firstFreqHz(id) == lastFreqHz(id) by construction). Returns the
// unsigned magnitude of the catalogued interval for every ascending
// or descending silhouette regardless of direction (Unlock and Lock
// both report the same ~131 Hz magnitude -- a perfect fifth between
// NOTE_C5 and NOTE_G5; NetworkOk and NetworkFail both report the
// same ~350 Hz magnitude -- a perfect fourth between NOTE_C6 and
// NOTE_F6). Foreshadowed by the S238 commit body's "direction is
// one half of the silhouette, magnitude is the other" framing -- where
// S238 silhouette(id) returns the SIGN of the catalogued first /
// last comparison (+1 ascending / 0 level / -1 descending), S239
// pitchSpanHz(id) returns the MAGNITUDE of the same comparison. The
// two derived accessors together describe the catalogued silhouette
// completely without either subsuming the other: a caller that wants
// only the direction stays on silhouette(id), a caller that wants
// only the magnitude stays on pitchSpanHz(id), and a caller that
// wants both reads them separately rather than re-deriving one from
// the other. Mirrors the S232 durationMs(id) precedent of exposing a
// derived answer where the derived form is the one the caller
// actually wants. The foreshadowed "Settings -> Sounds -> System
// chimes" picker can pair the bar's TILT (silhouette) with its
// HEIGHT (pitchSpanHz) to give the user a glanceable visual
// abstraction of the catalogued cue without registering a
// LoopManager listener of its own. Distinct from firstFreqHz(id) /
// lastFreqHz(id) themselves: those helpers stay so a caller that
// wants the absolute pitch values can still read them directly.
// Distinct from silhouette(id) (S238): that helper reports the
// direction of the catalogued span, this helper reports the
// magnitude. Profile-state INDEPENDENT: the catalogued pitch span is
// the same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
// HEADSET. Cheap O(1) two-field absolute difference via
// firstFreqHz(id) / lastFreqHz(id) so the rest-aware semantics those
// accessors already implement (a leading or trailing rest is encoded
// as freq == 0 in the catalogue) feed straight into the magnitude
// answer without re-deriving them here -- if a future entry ever
// opens or closes on a rest the magnitude collapses transparently to
// the catalogued audible note's pitch.
uint16_t PhoneSystemTones::pitchSpanHz(uint8_t id){
	if(!valid(id)) return 0;
	const uint16_t first = firstFreqHz(id);
	const uint16_t last  = lastFreqHz(id);
	return (first > last) ? (uint16_t)(first - last) : (uint16_t)(last - first);
}

// S240 - derived catalogue-wide maximum-pitch accessor for chime
// `id`. Returns the highest catalogued frequency, in Hz, across
// every PhoneRingtoneEngine::Note entry in the underlying Melody
// (max(kMelodies[id].notes[i].freq) for i in [0..count-1],
// skipping rest-encoded freq == 0 entries so a future leading /
// trailing / interior rest does not collapse the answer to zero by
// accident). Returns 0 for an out-of-range id, for the (currently
// impossible) empty-melody case, and for the (currently impossible)
// all-rests-melody case. Foreshadowed by the S234 / S235 / S238 /
// S239 commit bodies' progressive build-up of the catalogue pitch
// surface: where firstFreqHz(id) (S234) and lastFreqHz(id) (S235)
// expose the catalogued endpoints and silhouette(id) (S238) /
// pitchSpanHz(id) (S239) expose the relationship between those
// endpoints, S240 exposes the GLOBAL maximum the cue reaches at any
// step. For every monotonic melody in the v1 catalogue the answer
// agrees with whichever endpoint silhouette(id) points at; for
// non-monotonic entries the answer reports the catalogued ceiling
// regardless of which step it lands on -- TimerDone [NOTE_C6,
// NOTE_C6, NOTE_E6] is level by silhouette (first==last==NOTE_C6)
// but its peak NOTE_E6 sits above either endpoint, so the catalogued
// ceiling is the only catalogued differentiator between TimerDone
// and the genuinely-level pip pairs (Notify, SmsReceived) at the
// picker layer. The foreshadowed "Settings -> Sounds -> System
// chimes" picker can pair the bar's TILT (silhouette), HEIGHT
// (pitchSpanHz) and CEILING (peakFreqHz) to render a per-row pitch
// bar whose top traces the catalogued maximum. Distinct from
// firstFreqHz(id) / lastFreqHz(id) (catalogued endpoints), distinct
// from pitchSpanHz(id) (absolute difference between endpoints), and
// distinct from PhoneRingtoneEngine::currentFreq() (live-piezo
// accessor S191). Profile-state INDEPENDENT: the catalogued peak is
// the same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
// HEADSET. Cheap O(notes) linear scan with a uint16_t accumulator;
// no engine interaction, no persisted state, no per-call allocation;
// mirrors the existing count / valid / name / melody / play /
// tryPlay / isSilenced / durationMs / noteCount / firstFreqHz /
// lastFreqHz / gapMs / loops / silhouette / pitchSpanHz cluster.
uint16_t PhoneSystemTones::peakFreqHz(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint16_t peak = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		const uint16_t f = m.notes[i].freq;
		if(f != 0 && f > peak) peak = f;
	}
	return peak;
}

// S241 - derived catalogue-wide minimum-pitch accessor for chime
// `id`. Returns the lowest catalogued audible frequency, in Hz,
// across every PhoneRingtoneEngine::Note entry in the underlying
// Melody (min(kMelodies[id].notes[i].freq) for i in [0..count-1],
// ignoring rest-encoded freq == 0 entries so a future leading /
// trailing / interior rest does not collapse the answer to zero by
// accident -- exactly mirroring the rest-skipping rule S240's
// peakFreqHz already uses for the catalogued CEILING). Returns 0
// for an out-of-range id, for the (currently impossible) empty-
// melody case, and for the (currently impossible) all-rests-melody
// case. Foreshadowed by the S240 commit body's "S240 exposes the
// GLOBAL maximum the cue reaches at any step" framing: where
// peakFreqHz(id) (S240) reports the catalogued CEILING across every
// step, S241 reports the catalogued FLOOR across every step. The
// two derived accessors together describe the catalogued pitch
// envelope completely without either subsuming the other -- a
// caller that wants only the ceiling stays on peakFreqHz(id), a
// caller that wants only the floor stays on troughFreqHz(id), and
// a caller that wants both reads them separately rather than
// re-deriving one from the other. Mirrors the S238 / S239
// (silhouette / pitchSpanHz) precedent of shipping the two halves
// of a catalogued shape as separate accessors. For every monotonic
// melody in the v1 catalogue the trough agrees with whichever
// endpoint sits below the other (the opposite endpoint of S240's
// peak); for non-monotonic future entries the answer reports the
// catalogued floor regardless of which step it lands on, which is
// the visually-meaningful one for the picker / diag walk. The
// pitch-bar abstraction the foreshadowed "Settings -> Sounds ->
// System chimes" picker renders -- TILT (silhouette), HEIGHT
// (pitchSpanHz), CEILING (peakFreqHz), FLOOR (troughFreqHz) -- now
// has all four catalogued anchor points exposed at the API layer,
// so the picker can lay out the bar's bottom edge at construction
// time without re-deriving the catalogued floor from the const
// Melody* pointer at the call site. Distinct from firstFreqHz /
// lastFreqHz (catalogued endpoints), distinct from pitchSpanHz
// (absolute difference between endpoints), distinct from peakFreqHz
// (catalogued ceiling), and distinct from
// PhoneRingtoneEngine::currentFreq() (live-piezo accessor S191).
// Profile-state INDEPENDENT: the catalogued trough is the same on
// SILENT / MEETING profiles as on GENERAL / OUTDOOR / HEADSET. The
// implementation mirrors S240 exactly with `<` substituted for `>`
// and a `found` sentinel because there is no natural starting value
// for a min-search across uint16_t -- starting at UINT16_MAX would
// work but the explicit sentinel makes the all-rests fallback
// (return 0) match S240's empty-melody fallback by construction.
// No engine interaction, no persisted state, no per-call allocation;
// next to the existing count / valid / name / melody / play /
// tryPlay / isSilenced / durationMs / noteCount / firstFreqHz /
// lastFreqHz / gapMs / loops / silhouette / pitchSpanHz / peakFreqHz
// cluster.
uint16_t PhoneSystemTones::troughFreqHz(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint16_t trough = 0;
	bool found = false;
	for(uint16_t i = 0; i < m.count; ++i){
		const uint16_t f = m.notes[i].freq;
		if(f == 0) continue;
		if(!found || f < trough){ trough = f; found = true; }
	}
	return found ? trough : 0;
}

// S242 - derived catalogue-wide arithmetic-mean pitch accessor for
// chime `id`. Returns the integer-rounded average of every audible
// (non-rest) catalogued frequency in the underlying Melody, in Hz
// (sum(notes[i].freq for non-rest i) / count_of_non_rest_notes).
// Rest-encoded freq == 0 entries are skipped so a future leading /
// trailing / interior rest does not pull the mean toward zero by
// accident, exactly mirroring the rest-skipping rule S240 / S241
// already use for the catalogued ceiling / floor scans. Returns 0
// for an out-of-range id, for the (currently impossible) empty-
// melody case, and for the (currently impossible) all-rests-melody
// case -- the same three "no answer" cases S240 / S241 already
// collapse to 0. Where peakFreqHz(id) (S240) reports the catalogued
// CEILING and troughFreqHz(id) (S241) reports the catalogued FLOOR,
// S242 reports the catalogued CENTRE -- the trio (CEILING, FLOOR,
// CENTRE) describes the catalogued pitch envelope's vertical
// anchors completely without any helper subsuming the others. For a
// strictly monotonic melody with `n` evenly-spaced steps the mean
// lands halfway between the catalogued first and last; for non-
// monotonic future entries (a fall-then-rise valley, an up-down-up
// "wave", a peak-and-return cue, etc.) the mean weights every step
// equally and so does NOT in general agree with the midpoint
// between peakFreqHz(id) and troughFreqHz(id). Distinct from
// firstFreqHz / lastFreqHz (catalogued endpoints), distinct from
// pitchSpanHz (absolute difference between endpoints), distinct
// from peakFreqHz (catalogued ceiling), distinct from troughFreqHz
// (catalogued floor), and distinct from
// PhoneRingtoneEngine::currentFreq() (live-piezo accessor S191).
// Profile-state INDEPENDENT: the catalogued mean is the same on
// SILENT / MEETING profiles as on GENERAL / OUTDOOR / HEADSET. Cheap
// O(notes) linear scan with a uint32_t accumulator (so the running
// sum cannot overflow even on a hypothetical 65535 x ~13000 Hz
// worst case) and a uint16_t non-rest counter; the final integer-
// rounded division uses the standard +half-divisor trick to avoid
// pulling in <math.h>. No engine interaction, no persisted state,
// no per-call allocation; mirrors the existing count / valid / name
// / melody / play / tryPlay / isSilenced / durationMs / noteCount /
// firstFreqHz / lastFreqHz / gapMs / loops / silhouette /
// pitchSpanHz / peakFreqHz / troughFreqHz cluster.
uint16_t PhoneSystemTones::meanFreqHz(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint32_t sum = 0;
	uint16_t audible = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		const uint16_t f = m.notes[i].freq;
		if(f == 0) continue;
		sum += f;
		++audible;
	}
	if(audible == 0) return 0;
	const uint32_t rounded = (sum + (audible / 2)) / audible;
	if(rounded > 0xFFFFu) return 0xFFFFu;
	return (uint16_t)rounded;
}

// S243 - derived structural audible-step accessor for chime `id`.
// Returns the number of catalogued PhoneRingtoneEngine::Note entries
// in the underlying Melody whose freq != 0 -- i.e. the count of
// NON-rest steps that the engine will actually drive the piezo for.
// Rest-encoded freq == 0 entries are skipped so a future leading /
// trailing / interior rest does not inflate the answer past the
// audible step count, exactly mirroring the rest-skipping rule S240
// (peakFreqHz), S241 (troughFreqHz) and S242 (meanFreqHz) already use
// for the catalogued ceiling, floor and centre scans. Returns 0 for
// an out-of-range id and for the (currently impossible) empty-melody
// case. Where noteCount(id) (S233) reports the catalogued TOTAL step
// count (audible notes + any future rests), audibleNoteCount(id)
// reports the catalogued AUDIBLE step count -- the number of
// catalogued steps that will actually drive the piezo when the engine
// plays back the cue. For every v1 catalogue entry today the two
// accessors agree (no v1 chime currently uses rests), so the picker /
// diag walk gets byte-identical behaviour today; they diverge only
// when a future entry interleaves a rest, at which point the picker
// can render captions like "(3 notes, 1 rest, 240 ms)" using the
// difference noteCount(id) - audibleNoteCount(id) without re-walking
// the const Melody* pointer at the call site. Foreshadowed by the
// S242 commit body's "uint16_t non-rest counter" framing: the divisor
// S242 already computes internally for meanFreqHz is exactly
// audibleNoteCount(id). Distinct from noteCount(id) (catalogued TOTAL
// step count), distinct from firstFreqHz / lastFreqHz (catalogued
// endpoints), distinct from peakFreqHz / troughFreqHz / meanFreqHz
// (catalogued ceiling / floor / centre), and distinct from
// PhoneRingtoneEngine::currentFreq() (live-piezo accessor S191).
// Profile-state INDEPENDENT: the catalogued audible-step count is the
// same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
// HEADSET. Cheap O(notes) linear scan with a uint16_t counter; no
// arithmetic, no rounding, no per-call allocation; mirrors the
// existing count / valid / name / melody / play / tryPlay /
// isSilenced / durationMs / noteCount / firstFreqHz / lastFreqHz /
// gapMs / loops / silhouette / pitchSpanHz / peakFreqHz /
// troughFreqHz / meanFreqHz cluster.
uint16_t PhoneSystemTones::audibleNoteCount(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint16_t audible = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq == 0) continue;
		++audible;
	}
	return audible;
}

// S244 - derived structural rest-step accessor for chime id. Returns
// the number of catalogued PhoneRingtoneEngine::Note entries in the
// underlying Melody whose freq == 0 (the count of REST steps that the
// engine encounters but does NOT drive the piezo for). Exact
// complement of audibleNoteCount(id) (S243): for every catalogued
// chime restNoteCount(id) + audibleNoteCount(id) == noteCount(id).
// Returns 0 for an out-of-range id, for the (currently impossible)
// empty-melody case, and for every v1 catalogue entry today (no v1
// chime uses rests) -- the accessor only diverges from a constant
// zero when a future v2+ entry interleaves a rest, at which point a
// picker row caption like "(3 notes, 1 rest, 240 ms)" reads
// audibleNoteCount(id) and restNoteCount(id) directly instead of
// computing the difference noteCount(id) - audibleNoteCount(id) at
// the call site. Where noteCount(id) (S233) reports the catalogued
// TOTAL step count and audibleNoteCount(id) (S243) reports the
// catalogued AUDIBLE step count, restNoteCount(id) reports the
// catalogued REST step count -- the third leg of the same partition,
// rounding out the structural-count cluster (TOTAL / AUDIBLE / REST)
// the same way S239-S242 rounded out the structural-pitch cluster
// (SPAN / PEAK / TROUGH / MEAN) on top of the S234 / S235 endpoint
// pair. Profile-state INDEPENDENT: the catalogued rest-step count is
// the same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
// HEADSET. Cheap O(notes) linear scan with a uint16_t counter; no
// arithmetic, no rounding, no per-call allocation; mirrors the
// existing count / valid / name / melody / play / tryPlay /
// isSilenced / durationMs / noteCount / firstFreqHz / lastFreqHz /
// gapMs / loops / silhouette / pitchSpanHz / peakFreqHz /
// troughFreqHz / meanFreqHz / audibleNoteCount cluster.
uint16_t PhoneSystemTones::restNoteCount(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint16_t rests = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq == 0) ++rests;
	}
	return rests;
}

// S246 - derived structural audible-drive-time accessor for chime id.
// Returns the sum of durationMs across every catalogued
// PhoneRingtoneEngine::Note entry in the underlying Melody whose
// freq != 0 -- i.e. the wall-clock time, in ms, that the engine
// actually drives the piezo for one playback of the chime, NOT
// counting rest-step durations and NOT counting the per-step
// inter-loop gapMs filler that durationMs(id) (S232) folds into
// its TOTAL answer. Duration-side complement of audibleNoteCount(id)
// (S243): where S243 reports "how MANY of the catalogued steps drive
// the piezo," S246 reports "for HOW LONG the piezo is driven across
// those steps." Together with restNoteCount(id) (S244 -- count of
// REST steps) the catalogue exposes both halves of the audible /
// rest split on the COUNT axis today, and the next session in this
// cluster will promote a restDurationMs(id) sibling to round out
// the same split on the DURATION axis. Returns 0 for an out-of-range
// id, for the (currently impossible) empty-melody case, and for the
// (also currently impossible) all-rests catalogue entry. For every
// v1 catalogue entry today (no v1 chime uses rests) the audible-
// step branch matches "sum of durationMs across every step" so the
// answer collapses to durationMs(id) - (gapMs(id) * noteCount(id))
// for the v1 catalogue, and only diverges from that subtraction
// when a future v2+ entry interleaves a rest. Saturates at 0xFFFF
// ms (the same uint16_t ceiling durationMs(id) already uses) so a
// picker row caption can render the value as a four-digit integer
// without an int cast at the call site. Profile-state INDEPENDENT:
// the catalogued audible-drive duration is the same on SILENT /
// MEETING profiles as on GENERAL / OUTDOOR / HEADSET (the S231
// tryPlay(id) gate already reports the silenced answer separately
// for any caller that wants to fade the row caption into a
// "(silenced)" form). Cheap O(notes) linear scan with a uint32_t
// accumulator clamped to a uint16_t return; no rounding, no per-
// call allocation; mirrors the existing count / valid / name /
// melody / play / tryPlay / isSilenced / durationMs / noteCount /
// firstFreqHz / lastFreqHz / gapMs / loops / silhouette /
// pitchSpanHz / peakFreqHz / troughFreqHz / meanFreqHz /
// audibleNoteCount / restNoteCount cluster.
uint16_t PhoneSystemTones::audibleDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint32_t total = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq == 0) continue;
		total += (uint32_t) m.notes[i].durationMs;
	}
	if(total > 0xFFFFU) return 0xFFFFU;
	return (uint16_t) total;
}

// S247 - derived structural rest-step-duration accessor for chime id.
// Returns the sum of durationMs across every catalogued
// PhoneRingtoneEngine::Note entry in the underlying Melody whose
// freq == 0 -- i.e. the wall-clock time, in ms, that the engine
// spends sitting on REST steps without driving the piezo for one
// playback of the chime, NOT counting the audible-step durations
// that audibleDurationMs(id) (S246) reports and NOT counting the
// per-step inter-loop gapMs filler that durationMs(id) (S232)
// folds into its TOTAL answer. The silence-side complement of
// audibleDurationMs(id) (S246) and the duration-side complement of
// restNoteCount(id) (S244): where S246 reports "for HOW LONG the
// piezo is driven across the audible steps" and S244 reports "how
// MANY of the catalogued steps are RESTS," restDurationMs(id)
// reports "for HOW LONG the catalogued REST steps hold the piezo
// silent." The four leaves of the catalogue's audible / rest split
// now exist as dedicated accessors on both axes:
// audibleNoteCount(id) (S243) + restNoteCount(id) (S244) on the
// COUNT axis, audibleDurationMs(id) (S246) + restDurationMs(id)
// (S247) on the DURATION axis. Together with gapMs(id) (S236) and
// noteCount(id) (S233) the catalogue exposes the full structural-
// duration partition that durationMs(id) (S232) folds together:
// for every catalogued chime
//     audibleDurationMs(id) + restDurationMs(id)
//       + gapMs(id) * noteCount(id) == durationMs(id)
// (modulo the uint16_t saturation that all four duration accessors
// share). Returns 0 for an out-of-range id, for the (currently
// impossible) empty-melody case, for any catalogue entry with no
// rest steps, and -- byte-identically -- for every v1 catalogue
// entry today (no v1 chime uses rests). Saturates at 0xFFFF ms
// (the same uint16_t ceiling durationMs(id) and audibleDurationMs(id)
// already use). Profile-state INDEPENDENT: the catalogued rest-step
// duration is the same on SILENT / MEETING profiles as on GENERAL /
// OUTDOOR / HEADSET (the S231 tryPlay(id) gate already reports the
// silenced answer separately for any caller that wants to fade the
// row caption into a "(silenced)" form). Cheap O(notes) linear scan
// with a uint32_t accumulator clamped to a uint16_t return; no
// rounding, no per-call allocation; mirrors the existing count /
// valid / name / melody / play / tryPlay / isSilenced / durationMs
// / noteCount / firstFreqHz / lastFreqHz / gapMs / loops /
// silhouette / pitchSpanHz / peakFreqHz / troughFreqHz / meanFreqHz
// / audibleNoteCount / restNoteCount / audibleDurationMs cluster.
uint16_t PhoneSystemTones::restDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint32_t total = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq != 0) continue;
		total += (uint32_t) m.notes[i].durationMs;
	}
	if(total > 0xFFFFU) return 0xFFFFU;
	return (uint16_t) total;
}


// S248 - derived structural inter-step gap-total accessor for chime
// id. Returns the catalogued per-step gapMs filler the engine
// inserts BETWEEN consecutive PhoneRingtoneEngine::Note steps for
// one playback of the chime, summed across every step (i.e.
// gapMs(id) * noteCount(id), saturated at the uint16_t ceiling the
// rest of the duration cluster shares). Where gapMs(id) (S236)
// reports the per-step filler component in isolation,
// gapTotalMs(id) reports the same component IN AGGREGATE across
// the whole playback -- the third leg of the structural-duration
// partition that durationMs(id) (S232) folds together. The
// aggregate-side complement of gapMs(id) and the gap-axis sibling
// of audibleDurationMs(id) (S246) and restDurationMs(id) (S247):
// every catalogued step's wall-clock cost partitions cleanly into
// one of three buckets -- the audible-step durations the piezo is
// driven for (S246), the rest-step durations the piezo holds
// silent for (S247), and the inter-step gap filler the engine
// waits between consecutive steps (S248). Together with
// noteCount(id) (S233) the catalogue exposes the full structural-
// duration partition that durationMs(id) folds together: for every
// catalogued chime
//     audibleDurationMs(id) + restDurationMs(id) + gapTotalMs(id)
//       == durationMs(id)
// (modulo the uint16_t saturation that all four duration accessors
// share). Returns 0 for an out-of-range id, for the (currently
// impossible) empty-melody case, and -- byte-identically -- for
// every v1 catalogue entry whose kMelodies[id].gapMs == 0 (i.e.
// the single-pulse chimes that don't space their steps with a
// gap). Saturates at 0xFFFF ms (the same uint16_t ceiling
// durationMs(id), audibleDurationMs(id), and restDurationMs(id)
// already use). Profile-state INDEPENDENT: the catalogued
// inter-step gap total is the same on SILENT / MEETING profiles as
// on GENERAL / OUTDOOR / HEADSET (the S231 tryPlay(id) gate
// already reports the silenced answer separately for any caller
// that wants to fade the row caption into a "(silenced)" form).
// Cheap O(1) -- two field reads, one uint32_t multiply, one
// saturate-to-uint16_t -- no per-call allocation, no scan of the
// underlying Note* array; mirrors the existing count / valid /
// name / melody / play / tryPlay / isSilenced / durationMs /
// noteCount / firstFreqHz / lastFreqHz / gapMs / loops /
// silhouette / pitchSpanHz / peakFreqHz / troughFreqHz /
// meanFreqHz / audibleNoteCount / restNoteCount / audibleDurationMs
// / restDurationMs cluster.
uint16_t PhoneSystemTones::gapTotalMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	if(m.gapMs == 0) return 0;
	uint32_t total = (uint32_t) m.gapMs * (uint32_t) m.count;
	if(total > 0xFFFFU) return 0xFFFFU;
	return (uint16_t) total;
}

// S249 - derived per-audible-step mean-duration accessor for chime
// id. Returns the catalogued audible-step durations divided by the
// count of those audible steps, i.e. the mean wall-clock duration
// per AUDIBLE catalogued PhoneRingtoneEngine::Note step, in ms,
// rounded toward zero by integer division. Where
// audibleDurationMs(id) (S246) reports the SUM of the catalogued
// audible-step durations and audibleNoteCount(id) (S243) reports
// the COUNT of those steps, meanNoteDurationMs(id) collapses the
// pair into a per-step CENTRE -- the duration-axis sibling of
// meanFreqHz(id) (S242) on the pitch axis. Returns 0 for an
// out-of-range id, for the (currently impossible) empty-melody
// case, and for the all-rests-melody case (where the audible-step
// counter would be zero and the divide would be undefined) -- the
// same three "no answer" cases the pitch-axis trio peakFreqHz
// (S240) / troughFreqHz (S241) / meanFreqHz (S242) already
// collapse to 0. Saturates at 0xFFFF ms (the same uint16_t ceiling
// durationMs / audibleDurationMs / restDurationMs / gapTotalMs
// already share). Cheap O(notes) linear scan with two uint32_t
// accumulators (audible-step duration sum + audible-step counter)
// fused into one pass, one final divide and saturate; no
// per-call allocation, no recursion into audibleDurationMs /
// audibleNoteCount. Mirrors the existing count / valid / name /
// melody / play / tryPlay / isSilenced / durationMs / noteCount /
// firstFreqHz / lastFreqHz / gapMs / loops / silhouette /
// pitchSpanHz / peakFreqHz / troughFreqHz / meanFreqHz /
// audibleNoteCount / restNoteCount / audibleDurationMs /
// restDurationMs / gapTotalMs cluster.
uint16_t PhoneSystemTones::meanNoteDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint32_t total = 0;
	uint16_t audible = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq == 0) continue;
		total += (uint32_t) m.notes[i].durationMs;
		++audible;
	}
	if(audible == 0) return 0;
	uint32_t mean = total / (uint32_t) audible;
	if(mean > 0xFFFFU) return 0xFFFFU;
	return (uint16_t) mean;
}

// S250 - derived per-audible-step maximum-duration accessor for
// chime id. Returns the longest catalogued audible-step durationMs
// value the engine holds the piezo at a tone for, in ms. Where
// audibleDurationMs(id) (S246) reports the SUM of the catalogued
// audible-step durations, audibleNoteCount(id) (S243) reports the
// COUNT of those steps, and meanNoteDurationMs(id) (S249) collapses
// the SUM/COUNT pair into a per-step CENTRE,
// peakNoteDurationMs(id) reports the per-step CEILING -- the
// duration-axis sibling of peakFreqHz(id) (S240) on the pitch axis.
// Returns 0 for an out-of-range id, for the (currently impossible)
// empty-melody case, and for the all-rests-melody case (where no
// audible step exists and the ceiling is undefined) -- the same
// three "no answer" cases the pitch-axis trio peakFreqHz (S240) /
// troughFreqHz (S241) / meanFreqHz (S242) and the duration-axis
// centre accessor meanNoteDurationMs (S249) already collapse to 0.
// Saturates at 0xFFFF ms (the same uint16_t ceiling the duration
// cluster shares); since the catalogued per-step duration is itself
// a uint16_t the cap is in practice unreachable, but the
// saturate-on-overflow guard keeps the return type honest. Cheap
// O(notes) linear scan with a single uint16_t running max that
// skips rest steps (freq == 0); no per-call allocation, no
// recursion into the existing audibleDurationMs / audibleNoteCount
// / meanNoteDurationMs accessors. Mirrors the existing count /
// valid / name / melody / play / tryPlay / isSilenced / durationMs
// / noteCount / firstFreqHz / lastFreqHz / gapMs / loops /
// silhouette / pitchSpanHz / peakFreqHz / troughFreqHz /
// meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs cluster.
uint16_t PhoneSystemTones::peakNoteDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint16_t peak = 0;
	bool any = false;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq == 0) continue;
		any = true;
		if(m.notes[i].durationMs > peak) peak = m.notes[i].durationMs;
	}
	if(!any) return 0;
	return peak;
}

// S251 - derived per-audible-step minimum-duration accessor for
// chime `id`. Returns the shortest catalogued audible-step
// durationMs value the engine holds the piezo at a tone for, in ms.
// Where audibleDurationMs(id) (S246) reports the SUM of the
// catalogued audible-step durations, audibleNoteCount(id) (S243)
// reports the COUNT of those steps, meanNoteDurationMs(id) (S249)
// collapses the SUM/COUNT pair into a per-step CENTRE, and
// peakNoteDurationMs(id) (S250) reports the per-step CEILING,
// troughNoteDurationMs(id) reports the per-step FLOOR -- the
// duration-axis sibling of troughFreqHz(id) (S241) on the pitch
// axis, completing the duration-axis (CEILING, FLOOR, CENTRE) trio
// that mirrors the pitch-axis (CEILING, FLOOR, CENTRE) trio of
// S240 / S241 / S242. Returns 0 for an out-of-range id, for the
// (currently impossible) empty-melody case, and for the all-rests-
// melody case (where audibleNoteCount(id) == 0, no audible step
// exists, and the floor is undefined) -- the same three "no
// answer" cases the pitch-axis trio peakFreqHz / troughFreqHz /
// meanFreqHz and the duration-axis centre/ceiling pair
// meanNoteDurationMs / peakNoteDurationMs already collapse to 0.
// Saturates at 0xFFFF ms (the same uint16_t ceiling the duration
// cluster durationMs / audibleDurationMs / restDurationMs /
// gapTotalMs / meanNoteDurationMs / peakNoteDurationMs already
// share); since the catalogued per-step duration is itself a
// uint16_t the cap is in practice unreachable, but the saturate-
// on-overflow guard keeps the return type honest. Cheap O(notes)
// linear scan with a single uint16_t running min guarded by a
// `found` sentinel -- mirroring troughFreqHz(id) (S241) exactly
// with `<` substituted for `>` and a `found` flag because there
// is no natural starting value for a min-search across uint16_t.
// Skips rest steps (freq == 0) so the floor reports the shortest
// AUDIBLE step; no per-call allocation, no recursion into the
// existing audibleDurationMs / audibleNoteCount /
// meanNoteDurationMs / peakNoteDurationMs accessors. Mirrors the
// existing count / valid / name / melody / play / tryPlay /
// isSilenced / durationMs / noteCount / firstFreqHz / lastFreqHz
// / gapMs / loops / silhouette / pitchSpanHz / peakFreqHz /
// troughFreqHz / meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs cluster.
uint16_t PhoneSystemTones::troughNoteDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint16_t trough = 0;
	bool found = false;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq == 0) continue;
		const uint16_t d = m.notes[i].durationMs;
		if(!found || d < trough){ trough = d; found = true; }
	}
	return found ? trough : 0;
}

// S252 - structural first-note duration accessor for chime `id`.
// Returns the catalogued duration in ms of the FIRST
// PhoneRingtoneEngine::Note entry in the underlying Melody
// (kMelodies[id].notes[0].durationMs). The duration-axis sibling
// of firstFreqHz(id) (S234) on the pitch axis -- where firstFreqHz
// reports the catalogued leading pitch the engine drives the
// piezo at on the first audible step, firstNoteDurationMs reports
// the catalogued leading step-duration the engine holds that
// pitch for. Returns 0 for an out-of-range id and for the
// (currently impossible) empty-melody case -- the same two "no
// answer" cases the structural-pair firstFreqHz / lastFreqHz
// already collapse to 0. Like firstFreqHz / lastFreqHz this is a
// STRUCTURAL accessor (reads the array endpoint regardless of
// whether the first step is an audible note or a rest); in the v1
// catalogue no chime opens with a rest so the answer collapses
// transparently to "the leading audible step's duration" for
// every entry that ships today. Distinct from peakNoteDurationMs
// (audible-step CEILING), troughNoteDurationMs (audible-step
// FLOOR), meanNoteDurationMs (audible-step CENTRE),
// audibleDurationMs (audible-step SUM), durationMs (TOTAL incl.
// audible + rests + gaps), restDurationMs / gapTotalMs / gapMs
// (per-component accessors) -- none of which can be inverted to
// recover the catalogued leading-endpoint step-duration without
// walking the catalogued Note* pointer at the call site. Cheap
// O(1) struct field read; no engine interaction, no persisted
// state, no per-call allocation; mirrors firstFreqHz(id) (S234)
// exactly with durationMs substituted for freq. Header surface
// grows by exactly one public symbol; the cpp adds a single
// function next to the existing count / valid / name / melody /
// play / tryPlay / isSilenced / durationMs / noteCount /
// firstFreqHz / lastFreqHz / gapMs / loops / silhouette /
// pitchSpanHz / peakFreqHz / troughFreqHz / meanFreqHz /
// audibleNoteCount / restNoteCount / audibleDurationMs /
// restDurationMs / gapTotalMs / meanNoteDurationMs /
// peakNoteDurationMs / troughNoteDurationMs cluster.
uint16_t PhoneSystemTones::firstNoteDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	return m.notes[0].durationMs;
}

// S253 - structural last-note duration accessor for chime `id`.
// Returns the catalogued duration in ms of the LAST
// PhoneRingtoneEngine::Note entry in the underlying Melody
// (kMelodies[id].notes[m.count - 1].durationMs). The trailing-
// endpoint sibling of firstNoteDurationMs(id) (S252) on the
// duration axis, and the duration-axis sibling of lastFreqHz(id)
// (S235) on the pitch axis -- where lastFreqHz reports the
// catalogued trailing pitch the engine drives the piezo at on the
// final audible step, lastNoteDurationMs reports the catalogued
// trailing step-duration the engine holds that pitch for. With
// S252 it closes the duration-axis structural pair (LEADING,
// TRAILING) so the duration axis now matches the pitch axis
// exactly: firstFreqHz / lastFreqHz on the pitch axis <->
// firstNoteDurationMs / lastNoteDurationMs on the duration axis.
// Returns 0 for an out-of-range id and for the (currently
// impossible) empty-melody case -- the same two "no answer" cases
// the structural-pair firstFreqHz / lastFreqHz / firstNoteDurationMs
// already collapse to 0. Like firstFreqHz / lastFreqHz /
// firstNoteDurationMs this is a STRUCTURAL accessor (reads the
// array endpoint regardless of whether the last step is an
// audible note or a rest); in the v1 catalogue no chime closes
// with a rest so the answer collapses transparently to "the
// trailing audible step's duration" for every entry that ships
// today. Distinct from firstNoteDurationMs (LEADING endpoint),
// peakNoteDurationMs (audible-step CEILING), troughNoteDurationMs
// (audible-step FLOOR), meanNoteDurationMs (audible-step CENTRE),
// audibleDurationMs (audible-step SUM), durationMs (TOTAL incl.
// audible + rests + gaps), restDurationMs / gapTotalMs / gapMs
// (per-component accessors) -- none of which can be inverted to
// recover the catalogued trailing-endpoint step-duration without
// walking the catalogued Note* pointer at the call site. Cheap
// O(1) struct field read; no engine interaction, no persisted
// state, no per-call allocation; mirrors lastFreqHz(id) (S235)
// exactly with durationMs substituted for freq. Header surface
// grows by exactly one public symbol; the cpp adds a single
// function next to the existing count / valid / name / melody /
// play / tryPlay / isSilenced / durationMs / noteCount /
// firstFreqHz / lastFreqHz / gapMs / loops / silhouette /
// pitchSpanHz / peakFreqHz / troughFreqHz / meanFreqHz /
// audibleNoteCount / restNoteCount / audibleDurationMs /
// restDurationMs / gapTotalMs / meanNoteDurationMs /
// peakNoteDurationMs / troughNoteDurationMs / firstNoteDurationMs
// cluster.
uint16_t PhoneSystemTones::lastNoteDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	return m.notes[m.count - 1].durationMs;
}

// S254 - derived catalogue-wide endpoint-magnitude accessor on the
// DURATION axis for chime `id`. Mirrors pitchSpanHz(id) (S239)
// exactly with durationMs substituted for freq and the S252 / S253
// duration-endpoint pair (firstNoteDurationMs / lastNoteDurationMs)
// substituted for the S234 / S235 pitch-endpoint pair (firstFreqHz
// / lastFreqHz). Returns the unsigned absolute difference, in ms,
// between the catalogued LEADING step-duration and the catalogued
// TRAILING step-duration of the underlying Melody. Returns 0 for
// an out-of-range id (firstNoteDurationMs / lastNoteDurationMs
// already collapse to 0 there) and -- naturally -- for any chime
// whose leading and trailing step-durations are equal. Closes the
// derived endpoint-magnitude axis on the duration side at full
// symmetry with the pitch side. Cheap O(1): two struct field reads
// and one subtraction, no engine interaction, no allocation.
// Distinct from peakNoteDurationMs / troughNoteDurationMs /
// meanNoteDurationMs (audible-step CEILING / FLOOR / CENTRE across
// every step, not the structural endpoint magnitude), distinct
// from audibleDurationMs / restDurationMs / gapTotalMs (per-
// component sums), distinct from durationMs (TOTAL), distinct
// from firstNoteDurationMs / lastNoteDurationMs (the structural
// endpoints themselves), distinct from pitchSpanHz (pitch axis,
// not duration axis). Lives next to the existing count / valid /
// name / melody / play / tryPlay / isSilenced / durationMs /
// noteCount / firstFreqHz / lastFreqHz / gapMs / loops /
// silhouette / pitchSpanHz / peakFreqHz / troughFreqHz /
// meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs / troughNoteDurationMs
// / firstNoteDurationMs / lastNoteDurationMs cluster.
uint16_t PhoneSystemTones::durationSpanMs(uint8_t id){
	if(!valid(id)) return 0;
	const uint16_t first = firstNoteDurationMs(id);
	const uint16_t last  = lastNoteDurationMs(id);
	return (first > last) ? (uint16_t)(first - last) : (uint16_t)(last - first);
}

// S255 - derived audible-CEILING-FLOOR magnitude accessor on the
// PITCH axis for chime `id`. Returns the unsigned absolute
// difference, in Hz, between the catalogued audible CEILING and
// the catalogued audible FLOOR of the underlying Melody --
// peakFreqHz(id) - troughFreqHz(id) (always non-negative by
// construction, since peakFreqHz is a max-search and
// troughFreqHz is a min-search across the same audible-step
// subset, so peak >= trough is invariant). The audible-axis
// pitch sibling of pitchSpanHz(id) (S239) on the structural-
// endpoint axis: where pitchSpanHz reports the magnitude of the
// (LEADING, TRAILING) STRUCTURAL endpoint pair, S255 reports the
// magnitude of the (CEILING, FLOOR) AUDIBLE envelope, so a future
// PhoneDiagScreen "Sound test" walk that wants to render the
// audible-pitch range of a cue (full vertical extent of the
// pitch-bar between top tick and bottom tick) or a future
// "Settings -> Sounds -> System chimes" picker row caption like
// "(spans 880 Hz)" can read a dedicated derived accessor for the
// audible-axis envelope magnitude instead of computing
// peakFreqHz(id) - troughFreqHz(id) at the call site. Returns 0
// for an out-of-range id (collapsing transparently because both
// peakFreqHz(id) and troughFreqHz(id) already collapse to 0
// there), for the (currently impossible) empty-melody case, for
// the (currently impossible) all-rests-melody case (both sides
// collapse to 0), and -- naturally -- for any flat-pitch chime
// whose audible CEILING and FLOOR coincide (every audible step
// lands on the same frequency). Distinct from pitchSpanHz
// (structural endpoint magnitude, not audible CEILING / FLOOR
// magnitude), distinct from peakFreqHz / troughFreqHz (catalogued
// CEILING / FLOOR themselves, not the magnitude between them),
// distinct from meanFreqHz (audible-step CENTRE), distinct from
// firstFreqHz / lastFreqHz (structural endpoints, not audible
// bounds), distinct from durationSpanMs (duration axis, not
// pitch axis), distinct from silhouette (signed tilt sign).
// Profile-state INDEPENDENT: catalogued audible envelope
// magnitude is the same on SILENT / MEETING profiles as on
// GENERAL / OUTDOOR / HEADSET. Cheap O(notes): the two helpers
// each do a single linear scan with a uint16_t accumulator;
// total work is two passes plus one subtraction, no engine
// interaction, no persisted state, no per-call allocation;
// mirrors pitchSpanHz(id) (S239) exactly with peakFreqHz /
// troughFreqHz substituted for firstFreqHz / lastFreqHz. Lives
// next to the existing count / valid / name / melody / play /
// tryPlay / isSilenced / durationMs / noteCount / firstFreqHz /
// lastFreqHz / gapMs / loops / silhouette / pitchSpanHz /
// peakFreqHz / troughFreqHz / meanFreqHz / audibleNoteCount /
// restNoteCount / audibleDurationMs / restDurationMs /
// gapTotalMs / meanNoteDurationMs / peakNoteDurationMs /
// troughNoteDurationMs / firstNoteDurationMs /
// lastNoteDurationMs / durationSpanMs cluster. Opens the derived
// audible-envelope-magnitude axis on the pitch side at full
// symmetry with the structural-endpoint magnitude axis already
// closed by pitchSpanHz; the duration-axis sibling
// (audibleDurationSpanMs) is the natural follow-up.
uint16_t PhoneSystemTones::audiblePitchSpanHz(uint8_t id){
	if(!valid(id)) return 0;
	const uint16_t peak = peakFreqHz(id);
	const uint16_t trough = troughFreqHz(id);
	return (peak >= trough) ? (uint16_t)(peak - trough) : (uint16_t)(trough - peak);
}

// S256 -- derived audible-CEILING-FLOOR magnitude accessor on the
// DURATION axis for chime `id`. Returns the unsigned absolute
// difference, in milliseconds, between the catalogued audible
// CEILING note duration and the catalogued audible FLOOR note
// duration of the underlying Melody --
// peakNoteDurationMs(id) - troughNoteDurationMs(id) (always
// non-negative by construction, since peakNoteDurationMs is a
// max-search and troughNoteDurationMs is a min-search across the
// same audible-step subset, so peak >= trough is invariant). The
// audible-axis duration sibling of durationSpanMs(id) (S254) on
// the structural-endpoint axis: where durationSpanMs reports the
// magnitude of the (LEADING, TRAILING) STRUCTURAL endpoint pair,
// S256 reports the magnitude of the (CEILING, FLOOR) AUDIBLE
// envelope, so a future PhoneDiagScreen "Sound test" walk that
// wants to render the audible-duration range of a cue (full
// horizontal extent of a duration-bar between longest-note tick
// and shortest-note tick) or a future "Settings -> Sounds ->
// System chimes" picker row caption like "(spans 320 ms)" can
// read a dedicated derived accessor for the audible-axis
// envelope magnitude instead of computing
// peakNoteDurationMs(id) - troughNoteDurationMs(id) at the call
// site. Returns 0 for an out-of-range id (collapsing
// transparently because both peakNoteDurationMs(id) and
// troughNoteDurationMs(id) already collapse to 0 there), for the
// (currently impossible) empty-melody case, for the (currently
// impossible) all-rests-melody case (both sides collapse to 0),
// and -- naturally -- for any flat-duration chime whose audible
// CEILING and FLOOR note durations coincide (every audible step
// lasts the same number of milliseconds). Distinct from
// durationSpanMs (structural endpoint magnitude, not audible
// CEILING / FLOOR magnitude), distinct from peakNoteDurationMs /
// troughNoteDurationMs (catalogued CEILING / FLOOR themselves),
// distinct from meanNoteDurationMs (audible-step CENTRE),
// distinct from firstNoteDurationMs / lastNoteDurationMs
// (structural endpoints, not audible bounds), distinct from
// audiblePitchSpanHz (pitch axis, not duration axis), distinct
// from audibleDurationMs (audible-step duration TOTAL), distinct
// from silhouette (signed tilt sign). Profile-state INDEPENDENT:
// catalogued audible envelope magnitude is the same on SILENT /
// MEETING profiles as on GENERAL / OUTDOOR / HEADSET. Cheap
// O(notes): the two helpers each do a single linear scan with a
// uint16_t accumulator; total work is two passes plus one
// subtraction, no engine interaction, no persisted state, no
// per-call allocation; mirrors audiblePitchSpanHz(id) (S255)
// exactly with peakNoteDurationMs / troughNoteDurationMs
// substituted for peakFreqHz / troughFreqHz. Lives next to the
// existing count / valid / name / melody / play / tryPlay /
// isSilenced / durationMs / noteCount / firstFreqHz / lastFreqHz
// / gapMs / loops / silhouette / pitchSpanHz / peakFreqHz /
// troughFreqHz / meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs / troughNoteDurationMs
// / firstNoteDurationMs / lastNoteDurationMs / durationSpanMs /
// audiblePitchSpanHz cluster. Closes the derived audible-
// envelope-magnitude axis on the duration side at full symmetry
// with the pitch side already closed by audiblePitchSpanHz,
// completing the four-corner derived-magnitude grid (structural-
// endpoint + audible-envelope on each axis).
uint16_t PhoneSystemTones::audibleDurationSpanMs(uint8_t id){
	if(!valid(id)) return 0;
	const uint16_t peak = peakNoteDurationMs(id);
	const uint16_t trough = troughNoteDurationMs(id);
	return (peak >= trough) ? (uint16_t)(peak - trough) : (uint16_t)(trough - peak);
}

// S257 - derived per-rest-step mean-duration accessor for chime
// id. Returns the catalogued rest-step durations the engine holds
// the piezo SILENT for (the sum restDurationMs(id) (S247) reports)
// divided by the count of those rest steps (the count
// restNoteCount(id) (S244) reports), i.e. the mean wall-clock
// duration per REST catalogued step, in ms, rounded toward zero
// by integer division. The rest-axis sibling of
// meanNoteDurationMs(id) (S249) on the audible-step axis, exactly
// how restDurationMs(id) (S247) is the rest-axis sibling of
// audibleDurationMs(id) (S246) on the audible-step axis. Returns
// 0 for an out-of-range id, for the (currently impossible) empty-
// melody case, and for the no-rests-melody case (where
// restNoteCount(id) == 0 and the divisor would be zero) -- so a
// flat audible-only cue with no rests, or an out-of-range id,
// collapse to the same 0 the catalogued rest-axis pair
// restNoteCount (S244) / restDurationMs (S247) already collapse
// to. Saturates at 0xFFFF ms (the same uint16_t ceiling the
// duration cluster shares) so a picker row caption can render
// the value as a four-digit integer without an int cast at the
// call site. Distinct from restDurationMs (rest-step SUM in
// isolation, not per-step centre), distinct from restNoteCount
// (rest-step COUNT in isolation), distinct from meanNoteDurationMs
// (audible-step CENTRE, not rest-step centre), distinct from
// audibleDurationMs (audible-step SUM), distinct from durationMs
// (TOTAL incl. audible + rests + gaps), distinct from gapTotalMs /
// gapMs (inter-step filler component, not per-rest centre),
// distinct from peakNoteDurationMs / troughNoteDurationMs
// (audible-axis CEILING / FLOOR per-step extents, not rest-axis
// CENTRE), distinct from firstNoteDurationMs / lastNoteDurationMs
// (structural endpoints, not rest-step centre), distinct from
// durationSpanMs / audibleDurationSpanMs (derived endpoint /
// envelope MAGNITUDES on the audible-duration axis). Profile-
// state INDEPENDENT: the catalogued per-rest-step mean is the
// same on SILENT / MEETING profiles as on GENERAL / OUTDOOR /
// HEADSET. Cheap O(notes) linear scan with two uint32_t
// accumulators (rest-step duration sum + rest-step counter) and
// one final divide-and-saturate; no per-call allocation, no
// recursion into the existing restDurationMs / restNoteCount
// accessors -- the loop fuses the two passes so the catalogued
// Note* array is walked exactly once per call. Mirrors
// meanNoteDurationMs(id) (S249) exactly with the rest-step
// predicate (freq == 0) substituted for the audible-step
// predicate (freq != 0). Lives next to the existing count /
// valid / name / melody / play / tryPlay / isSilenced /
// durationMs / noteCount / firstFreqHz / lastFreqHz / gapMs /
// loops / silhouette / pitchSpanHz / peakFreqHz / troughFreqHz /
// meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs / troughNoteDurationMs
// / firstNoteDurationMs / lastNoteDurationMs / durationSpanMs /
// audiblePitchSpanHz / audibleDurationSpanMs cluster.
uint16_t PhoneSystemTones::meanRestDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint32_t total = 0;
	uint16_t rests = 0;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq != 0) continue;
		total += (uint32_t) m.notes[i].durationMs;
		++rests;
	}
	if(rests == 0) return 0;
	uint32_t mean = total / (uint32_t) rests;
	if(mean > 0xFFFFU) return 0xFFFFU;
	return (uint16_t) mean;
}

// S258 - derived per-rest-step maximum-duration accessor for chime
// `id`. Returns the longest catalogued rest-step durationMs value
// the engine holds the piezo SILENT for, in ms. Where
// restDurationMs(id) (S247) reports the SUM of the catalogued
// rest-step durations, restNoteCount(id) (S244) reports the COUNT
// of those steps, and meanRestDurationMs(id) (S257) collapses the
// SUM / COUNT pair into a per-step CENTRE on the rest-duration
// axis, peakRestDurationMs(id) reports the per-step CEILING -- the
// rest-axis sibling of peakNoteDurationMs(id) (S250) on the
// audible-step axis. Returns 0 for an out-of-range id, for the
// (currently impossible) empty-melody case, and for the no-rests-
// melody case (where restNoteCount(id) == 0 and no rest step
// exists, so the CEILING is undefined). Saturates at 0xFFFF ms
// (the same uint16_t ceiling the duration cluster durationMs /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs / troughNoteDurationMs /
// firstNoteDurationMs / lastNoteDurationMs / durationSpanMs /
// audibleDurationSpanMs / meanRestDurationMs already share); since
// the catalogued per-step duration is itself a uint16_t the cap is
// in practice unreachable. Cheap O(notes) linear scan with a
// single uint16_t running max guarded by a `found` sentinel -- no
// natural starting value for a max-search when the rest set may
// be empty -- mirroring peakNoteDurationMs(id) (S250) exactly with
// the rest-step predicate (freq == 0) substituted for the audible-
// step predicate (freq != 0). No per-call allocation, no recursion
// into the existing restDurationMs / restNoteCount /
// meanRestDurationMs accessors. Lives next to the existing count /
// valid / name / melody / play / tryPlay / isSilenced /
// durationMs / noteCount / firstFreqHz / lastFreqHz / gapMs /
// loops / silhouette / pitchSpanHz / peakFreqHz / troughFreqHz /
// meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs / troughNoteDurationMs
// / firstNoteDurationMs / lastNoteDurationMs / durationSpanMs /
// audiblePitchSpanHz / audibleDurationSpanMs / meanRestDurationMs
// cluster.
uint16_t PhoneSystemTones::peakRestDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint16_t peak = 0;
	bool any = false;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq != 0) continue;
		any = true;
		if(m.notes[i].durationMs > peak) peak = m.notes[i].durationMs;
	}
	if(!any) return 0;
	return peak;
}

// S259 - derived per-rest-step minimum-duration accessor for chime
// `id`. Returns the shortest catalogued rest-step durationMs
// value the engine holds the piezo SILENT for, in ms. Where
// restDurationMs(id) (S247) reports the SUM of the catalogued
// rest-step durations, restNoteCount(id) (S244) reports the COUNT
// of those steps, meanRestDurationMs(id) (S257) collapses the SUM
// / COUNT pair into a per-step CENTRE on the rest-duration axis,
// and peakRestDurationMs(id) (S258) reports the per-step CEILING,
// troughRestDurationMs(id) reports the per-step FLOOR -- the
// rest-axis sibling of troughNoteDurationMs(id) (S251) on the
// audible-step axis, closing the rest-axis (CEILING, FLOOR,
// CENTRE) trio in mirror of the audible-axis (CEILING, FLOOR,
// CENTRE) trio. Returns 0 for an out-of-range id, for the
// (currently impossible) empty-melody case, and for the no-rests-
// melody case (where restNoteCount(id) == 0 and the FLOOR is
// undefined) -- the same three "no answer" cases the rest-axis
// pair restNoteCount / restDurationMs, the rest-axis CENTRE
// meanRestDurationMs, and the rest-axis CEILING peakRestDurationMs
// already collapse to 0. Saturates at 0xFFFF ms (the same uint16_t
// ceiling the duration cluster durationMs / audibleDurationMs /
// restDurationMs / gapTotalMs / meanNoteDurationMs /
// peakNoteDurationMs / troughNoteDurationMs / firstNoteDurationMs
// / lastNoteDurationMs / durationSpanMs / audibleDurationSpanMs /
// meanRestDurationMs / peakRestDurationMs already share); since
// the catalogued per-step duration is itself a uint16_t the cap is
// in practice unreachable. Cheap O(notes) linear scan with a
// single uint16_t running min guarded by a `found` sentinel (no
// natural starting value for a min-search when the rest set may be
// empty) -- mirroring troughNoteDurationMs(id) (S251) exactly with
// the rest-step predicate (freq == 0) substituted for the audible-
// step predicate (freq != 0), and mirroring peakRestDurationMs(id)
// (S258) exactly with `<` substituted for `>`. No per-call
// allocation, no recursion into the existing restDurationMs /
// restNoteCount / meanRestDurationMs / peakRestDurationMs
// accessors. Lives next to the existing count / valid / name /
// melody / play / tryPlay / isSilenced / durationMs / noteCount /
// firstFreqHz / lastFreqHz / gapMs / loops / silhouette /
// pitchSpanHz / peakFreqHz / troughFreqHz / meanFreqHz /
// audibleNoteCount / restNoteCount / audibleDurationMs /
// restDurationMs / gapTotalMs / meanNoteDurationMs /
// peakNoteDurationMs / troughNoteDurationMs / firstNoteDurationMs
// / lastNoteDurationMs / durationSpanMs / audiblePitchSpanHz /
// audibleDurationSpanMs / meanRestDurationMs / peakRestDurationMs
// cluster.
uint16_t PhoneSystemTones::troughRestDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	uint16_t trough = 0;
	bool found = false;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq != 0) continue;
		const uint16_t d = m.notes[i].durationMs;
		if(!found || d < trough){ trough = d; found = true; }
	}
	return found ? trough : 0;
}

// S260 - derived rest-CEILING-FLOOR magnitude accessor on the
// DURATION axis. Returns the unsigned absolute difference, in
// milliseconds, between the catalogued per-rest-step CEILING note
// duration and the catalogued per-rest-step FLOOR note duration of
// the underlying Melody --
// peakRestDurationMs(id) - troughRestDurationMs(id) (always non-
// negative by construction, since peakRestDurationMs is a max-
// search and troughRestDurationMs is a min-search across the same
// rest-step subset, so peak >= trough is invariant). The rest-
// axis sibling of audibleDurationSpanMs(id) (S256) on the
// audible-duration envelope axis: where audibleDurationSpanMs(id)
// reports the magnitude of the (CEILING, FLOOR) AUDIBLE envelope,
// restDurationSpanMs(id) reports the magnitude of the
// (CEILING, FLOOR) REST envelope. Returns 0 for an out-of-range
// id (collapsing transparently because both peakRestDurationMs(id)
// and troughRestDurationMs(id) already collapse to 0 there), for
// the (currently impossible) empty-melody case, for the no-rests-
// melody case (both sides collapse to 0 there too), and naturally
// for any flat-rest-duration chime whose rest CEILING and FLOOR
// coincide. Cheap O(notes): mirrors audibleDurationSpanMs(id)
// (S256) exactly with peakRestDurationMs / troughRestDurationMs
// substituted for peakNoteDurationMs / troughNoteDurationMs. Lives
// next to the existing count / valid / name / melody / play /
// tryPlay / isSilenced / durationMs / noteCount / firstFreqHz /
// lastFreqHz / gapMs / loops / silhouette / pitchSpanHz /
// peakFreqHz / troughFreqHz / meanFreqHz / audibleNoteCount /
// restNoteCount / audibleDurationMs / restDurationMs /
// gapTotalMs / meanNoteDurationMs / peakNoteDurationMs /
// troughNoteDurationMs / firstNoteDurationMs /
// lastNoteDurationMs / durationSpanMs / audiblePitchSpanHz /
// audibleDurationSpanMs / meanRestDurationMs /
// peakRestDurationMs / troughRestDurationMs cluster.
uint16_t PhoneSystemTones::restDurationSpanMs(uint8_t id){
	if(!valid(id)) return 0;
	const uint16_t peak = peakRestDurationMs(id);
	const uint16_t trough = troughRestDurationMs(id);
	return (peak >= trough) ? (uint16_t)(peak - trough) : (uint16_t)(trough - peak);
}

// S261 - structural first-rest duration accessor for chime `id`.
// Returns the catalogued duration in ms of the FIRST
// PhoneRingtoneEngine::Note entry whose freq == 0 (i.e. the
// LEADING rest-step) in the underlying Melody. The rest-axis
// sibling of firstNoteDurationMs(id) (S252) on the structural-
// endpoint axis: where firstNoteDurationMs reads the catalogued
// LEADING duration regardless of note-vs-rest character,
// firstRestDurationMs scans forward for the FIRST occurrence of
// a rest-step and reports the catalogued duration the engine
// holds silence for at that LEADING rest position. Returns 0 for
// an out-of-range id, for the (currently impossible) empty-
// melody case, for the no-rests-melody case (a chime that
// contains zero freq == 0 steps), and naturally for any rest
// step the catalogue marks with a zero duration. Distinct from
// firstNoteDurationMs (structural LEADING duration), restDurationMs
// (rest-step SUM), meanRestDurationMs (rest-step CENTRE),
// peakRestDurationMs / troughRestDurationMs (rest CEILING /
// FLOOR), restDurationSpanMs (rest CEILING-FLOOR magnitude),
// restNoteCount (rest-step COUNT). Cheap O(notes): a single
// forward linear scan that returns on the first freq == 0 hit;
// no engine interaction, no persisted state, no per-call
// allocation. Lives next to the existing count / valid / name /
// melody / play / tryPlay / isSilenced / durationMs / noteCount
// / firstFreqHz / lastFreqHz / gapMs / loops / silhouette /
// pitchSpanHz / peakFreqHz / troughFreqHz / meanFreqHz /
// audibleNoteCount / restNoteCount / audibleDurationMs /
// restDurationMs / gapTotalMs / meanNoteDurationMs /
// peakNoteDurationMs / troughNoteDurationMs / firstNoteDurationMs
// / lastNoteDurationMs / durationSpanMs / audiblePitchSpanHz /
// audibleDurationSpanMs / meanRestDurationMs /
// peakRestDurationMs / troughRestDurationMs / restDurationSpanMs
// cluster.
uint16_t PhoneSystemTones::firstRestDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	for(uint16_t i = 0; i < m.count; ++i){
		if(m.notes[i].freq == 0) return m.notes[i].durationMs;
	}
	return 0;
}

// S262 - structural last-rest duration accessor for chime `id`.
// Returns the catalogued duration in ms of the LAST
// PhoneRingtoneEngine::Note entry whose freq == 0 (i.e. the
// TRAILING rest-step) in the underlying Melody. The trailing-
// endpoint sibling of firstRestDurationMs(id) (S261) on the
// rest-axis structural-endpoint corner, and the rest-axis
// analogue of lastNoteDurationMs(id) (S253) on the audible-
// axis structural-endpoint corner: where lastNoteDurationMs
// reads the catalogued TRAILING duration regardless of note-
// vs-rest character, lastRestDurationMs scans backward for the
// LAST occurrence of a rest-step and reports the catalogued
// duration the engine holds silence for at that TRAILING rest
// position. With S261 it closes the rest-axis structural
// endpoint pair (LEADING, TRAILING) in the same way
// firstNoteDurationMs / lastNoteDurationMs (S252 / S253)
// closed the audible-axis structural endpoint pair on the
// duration axis. Returns 0 for an out-of-range id, for the
// (currently impossible) empty-melody case, for the no-rests-
// melody case, and naturally for any rest step the catalogue
// marks with a zero duration -- the same "no answer" cases
// firstRestDurationMs(id) (S261) already collapses to 0.
// Distinct from firstRestDurationMs (rest-axis LEADING
// endpoint), lastNoteDurationMs (structural TRAILING duration),
// restDurationMs (rest-step SUM), meanRestDurationMs (rest-
// step CENTRE), peakRestDurationMs / troughRestDurationMs
// (rest CEILING / FLOOR), restDurationSpanMs (rest CEILING-
// FLOOR magnitude), restNoteCount (rest-step COUNT). Cheap
// O(notes): a single reverse linear scan that returns on the
// first freq == 0 hit from the trailing end; no engine
// interaction, no persisted state, no per-call allocation.
// Lives next to the existing count / valid / name / melody /
// play / tryPlay / isSilenced / durationMs / noteCount /
// firstFreqHz / lastFreqHz / gapMs / loops / silhouette /
// pitchSpanHz / peakFreqHz / troughFreqHz / meanFreqHz /
// audibleNoteCount / restNoteCount / audibleDurationMs /
// restDurationMs / gapTotalMs / meanNoteDurationMs /
// peakNoteDurationMs / troughNoteDurationMs /
// firstNoteDurationMs / lastNoteDurationMs / durationSpanMs /
// audiblePitchSpanHz / audibleDurationSpanMs /
// meanRestDurationMs / peakRestDurationMs /
// troughRestDurationMs / restDurationSpanMs /
// firstRestDurationMs cluster.
uint16_t PhoneSystemTones::lastRestDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const Melody& m = kMelodies[id];
	if(m.notes == nullptr || m.count == 0) return 0;
	for(uint16_t i = m.count; i > 0; --i){
		if(m.notes[i - 1].freq == 0) return m.notes[i - 1].durationMs;
	}
	return 0;
}

// S263 - derived rest-axis structural-endpoint magnitude accessor
// on the DURATION axis for chime `id`. Returns the unsigned
// absolute difference, in ms, between the catalogued LEADING
// rest-step duration and the catalogued TRAILING rest-step
// duration of the underlying Melody --
// |firstRestDurationMs(id) - lastRestDurationMs(id)|. The rest-
// axis structural-endpoint sibling of durationSpanMs(id) (S254)
// on the duration axis: where durationSpanMs reports the
// magnitude of the (LEADING, TRAILING) audible-or-rest
// structural endpoint pair, S263 reports the magnitude of the
// (LEADING, TRAILING) REST-only structural endpoint pair. Returns
// 0 for an out-of-range id, for the (currently impossible)
// empty-melody case, for the no-rests-melody case (both endpoint
// accessors collapse to 0), and naturally for any chime whose
// LEADING and TRAILING rest-step share an identical catalogued
// duration (including the structural-degenerate single-rest
// case, where the LEADING rest IS the TRAILING rest and the two
// accessors return the same value). Distinct from durationSpanMs
// (structural endpoint magnitude -- audible-or-rest, not REST-
// only), audibleDurationSpanMs (audible-axis CEILING-FLOOR
// magnitude), restDurationSpanMs (rest-axis CEILING-FLOOR
// magnitude across every rest, not the LEADING-TRAILING endpoint
// pair), firstRestDurationMs / lastRestDurationMs (rest-axis
// endpoint values themselves, not their unsigned difference).
// Cheap O(notes): two linear scans (one forward via
// firstRestDurationMs, one reverse via lastRestDurationMs) plus a
// single unsigned subtraction. Lives next to the existing count /
// valid / name / melody / play / tryPlay / isSilenced /
// durationMs / noteCount / firstFreqHz / lastFreqHz / gapMs /
// loops / silhouette / pitchSpanHz / peakFreqHz / troughFreqHz /
// meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs / troughNoteDurationMs
// / firstNoteDurationMs / lastNoteDurationMs / durationSpanMs /
// audiblePitchSpanHz / audibleDurationSpanMs / meanRestDurationMs
// / peakRestDurationMs / troughRestDurationMs /
// restDurationSpanMs / firstRestDurationMs / lastRestDurationMs
// cluster.
uint16_t PhoneSystemTones::restDurationEndpointSpanMs(uint8_t id){
	if(!valid(id)) return 0;
	const uint16_t first = firstRestDurationMs(id);
	const uint16_t last  = lastRestDurationMs(id);
	return (first > last) ? (uint16_t)(first - last) : (uint16_t)(last - first);
}

// S264 - derived rest-axis structural-endpoint CENTRE accessor on
// the DURATION axis for chime `id`. Returns the arithmetic mean,
// in ms, of the catalogued LEADING rest-step duration
// (firstRestDurationMs(id), S261) and the catalogued TRAILING
// rest-step duration (lastRestDurationMs(id), S262) of the
// underlying Melody -- (firstRestDurationMs(id) +
// lastRestDurationMs(id)) / 2. The rest-axis structural-endpoint
// CENTRE sibling of restDurationEndpointSpanMs(id) (S263) on the
// duration axis: where S263 reports the unsigned MAGNITUDE of the
// (LEADING, TRAILING) REST-only structural endpoint pair, S264
// reports the CENTRE of that same pair. Completes the (MAGNITUDE,
// CENTRE) derived pair on the rest-axis structural-endpoint
// corner of the duration axis. Returns 0 for an out-of-range id,
// for the (currently impossible) empty-melody case, and for the
// no-rests-melody case (both endpoint accessors collapse to 0).
// Returns the shared catalogued duration for any chime whose
// LEADING and TRAILING rest-step share an identical duration
// (including the structural-degenerate single-rest case).
// Distinct from restDurationEndpointSpanMs (rest-axis structural-
// endpoint MAGNITUDE, not CENTRE), meanRestDurationMs (rest-axis
// per-step CENTRE across every rest, not just the LEADING-
// TRAILING pair), meanNoteDurationMs (audible-step per-step
// CENTRE, not rest-step), firstRestDurationMs /
// lastRestDurationMs (rest-axis endpoint values themselves, not
// their arithmetic mean). Cheap O(notes): two linear scans (one
// forward via firstRestDurationMs, one reverse via
// lastRestDurationMs) plus a single unsigned addition and a
// divide-by-two. Uses uint32_t for the intermediate sum so the
// firstRest + lastRest addition never wraps even at the full
// uint16_t ceiling. Lives next to the existing count / valid /
// name / melody / play / tryPlay / isSilenced / durationMs /
// noteCount / firstFreqHz / lastFreqHz / gapMs / loops /
// silhouette / pitchSpanHz / peakFreqHz / troughFreqHz /
// meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs / troughNoteDurationMs
// / firstNoteDurationMs / lastNoteDurationMs / durationSpanMs /
// audiblePitchSpanHz / audibleDurationSpanMs / meanRestDurationMs
// / peakRestDurationMs / troughRestDurationMs /
// restDurationSpanMs / firstRestDurationMs / lastRestDurationMs /
// restDurationEndpointSpanMs cluster.
uint16_t PhoneSystemTones::meanRestEndpointDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const uint32_t first = (uint32_t) firstRestDurationMs(id);
	const uint32_t last  = (uint32_t) lastRestDurationMs(id);
	const uint32_t mean  = (first + last) / 2u;
	if(mean > 0xFFFFU) return 0xFFFFU;
	return (uint16_t) mean;
}

// S265 - derived audible-axis structural-endpoint CENTRE accessor
// on the DURATION axis for chime `id`. Returns the arithmetic
// mean, in ms, of the catalogued LEADING audible-step duration
// (firstNoteDurationMs(id), S252) and the catalogued TRAILING
// audible-step duration (lastNoteDurationMs(id), S253) of the
// underlying Melody -- (firstNoteDurationMs(id) +
// lastNoteDurationMs(id)) / 2. The audible-axis structural-
// endpoint CENTRE sibling of durationSpanMs(id) (S254) on the
// duration axis: where durationSpanMs reports the unsigned
// MAGNITUDE of the (LEADING, TRAILING) AUDIBLE structural
// endpoint pair in ms, S265 reports the CENTRE of that same pair.
// Together with meanRestEndpointDurationMs(id) (S264) it closes
// the (audible-endpoint, rest-endpoint) derived structural-
// CENTRE pair on the duration axis, mirroring how durationSpanMs
// (S254) and restDurationEndpointSpanMs (S263) close the
// MAGNITUDE pair. Returns 0 for an out-of-range id, for the
// (currently impossible) empty-melody case, and for the no-
// audible-notes case (both endpoint accessors collapse to 0).
// Returns the shared catalogued duration for any chime whose
// LEADING and TRAILING audible-step share an identical duration
// (including the structural-degenerate single-audible-step case,
// where the LEADING audible step IS the TRAILING audible step
// and the two accessors return the same value). Distinct from
// durationSpanMs (audible-axis structural-endpoint MAGNITUDE,
// not CENTRE), meanNoteDurationMs (audible-step per-step CENTRE
// across every audible step, not just the LEADING-TRAILING pair),
// meanRestEndpointDurationMs (rest-axis endpoint CENTRE),
// firstNoteDurationMs / lastNoteDurationMs (the endpoint values
// themselves). Cheap O(notes): two linear scans (one forward via
// firstNoteDurationMs, one reverse via lastNoteDurationMs) plus a
// single unsigned addition and a divide-by-two. Uses uint32_t for
// the intermediate sum so the firstNote + lastNote addition never
// wraps even at the full uint16_t ceiling. Lives next to the
// existing count / valid / name / melody / play / tryPlay /
// isSilenced / durationMs / noteCount / firstFreqHz / lastFreqHz
// / gapMs / loops / silhouette / pitchSpanHz / peakFreqHz /
// troughFreqHz / meanFreqHz / audibleNoteCount / restNoteCount /
// audibleDurationMs / restDurationMs / gapTotalMs /
// meanNoteDurationMs / peakNoteDurationMs / troughNoteDurationMs
// / firstNoteDurationMs / lastNoteDurationMs / durationSpanMs /
// audiblePitchSpanHz / audibleDurationSpanMs / meanRestDurationMs
// / peakRestDurationMs / troughRestDurationMs /
// restDurationSpanMs / firstRestDurationMs / lastRestDurationMs /
// restDurationEndpointSpanMs / meanRestEndpointDurationMs
// cluster.
uint16_t PhoneSystemTones::meanNoteEndpointDurationMs(uint8_t id){
	if(!valid(id)) return 0;
	const uint32_t first = (uint32_t) firstNoteDurationMs(id);
	const uint32_t last  = (uint32_t) lastNoteDurationMs(id);
	const uint32_t mean  = (first + last) / 2u;
	if(mean > 0xFFFFU) return 0xFFFFU;
	return (uint16_t) mean;
}
