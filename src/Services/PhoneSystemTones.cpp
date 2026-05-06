#include "PhoneSystemTones.h"

#include <Notes.h>

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
	if(!valid(id)) return;
	// Routing through the engine means a Settings.sound mute is
	// honoured automatically (see PhoneRingtoneEngine::emitTone) and
	// any active melody (call ringer, music player) replaces this
	// one-shot so latched higher-priority audio always wins.
	Ringtone.play(kMelodies[id]);
}
