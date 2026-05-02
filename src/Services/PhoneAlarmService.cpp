#include "PhoneAlarmService.h"

#include "PhoneClock.h"
#include "PhoneRingtoneEngine.h"

#include <Arduino.h>
#include <Loop/LoopManager.h>
#include <Notes.h>

#include <nvs.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

// =====================================================================
// S124 — PhoneAlarmService
//
// Implementation overview:
//
//   1. Persistence is one NVS blob containing a tiny header + four
//      packed alarm records. We read on first begin(), write on every
//      setAlarm() call. Read-on-boot is implicit through begin().
//
//   2. Tick logic is a minute-quantised state machine:
//        - Each loop() converts PhoneClock::nowEpoch() to "epoch
//          minutes" (epoch / 60).
//        - If we have not crossed into a new minute since the last
//          eval, return immediately. This keeps the cost per
//          micro-tick down to a single division and a comparison.
//        - If we did cross a minute, walk the four alarms; any slot
//          whose (hour, minute) matches the current minute and whose
//          lastFiredMinute differs from the current value triggers
//          the ringtone.
//        - Snooze is checked first: a queued snooze re-fires on the
//          first tick at-or-after snoozeUntilEpoch.
//
//   3. The fire-trigger is `Ringtone.play(AlarmMelody)` with a
//      looping melody, identical pattern to PhoneTimer's alarm so the
//      audio cue is consistent across the device.
// =====================================================================

PhoneAlarmService Alarms;

namespace {

constexpr const char* kNamespace = "mpalarm";
constexpr const char* kBlobKey   = "tbl";

// Single-byte magic + version so a partially-rewritten record either
// matches or is rejected outright. 12 bytes total: 4 header + 4 * 2
// data + 0 padding currently — but we leave the header at 4 bytes so
// the blob remains future-proof if we add weekday masks later.
constexpr uint8_t kMagic0  = 'M';
constexpr uint8_t kMagic1  = 'A';
constexpr uint8_t kVersion = 1;

// Per-slot encoding: byte 0 = hour | (enabled << 7), byte 1 = minute.
// 4 slots * 2 bytes = 8 bytes of payload.
constexpr size_t kHeaderSize  = 4;
constexpr size_t kPayloadSize = PhoneAlarmService::MaxAlarms * 2;
constexpr size_t kBlobSize    = kHeaderSize + kPayloadSize;

// Alarm melody: rising 4-note arpeggio that loops while the alarm is
// firing. Uses the same NOTE_* constants as PhoneTimer's alarm so the
// two sound family-related; the 250 ms cadence keeps it urgent without
// straying into shrillness. Static const so it lives in flash.
const PhoneRingtoneEngine::Note kAlarmNotes[] = {
		{ NOTE_E5,  240 },
		{ NOTE_A5,  240 },
		{ NOTE_C6,  240 },
		{ NOTE_A5,  240 },
};

const PhoneRingtoneEngine::Melody kAlarmMelody = {
		kAlarmNotes,
		sizeof(kAlarmNotes) / sizeof(kAlarmNotes[0]),
		70,        // gapMs — same family as PhoneTimer's TimerAlm
		true,      // loop until dismissed
		"AlarmClk",
};

// Single shared NVS handle. Mirrors PhoneComposerStorage's lazy-open
// pattern so we never spam nvs_open() retries on the keypad-event hot
// path: tried-once-and-failed stays failed for the device's lifetime.
nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("AlarmService",
		         "nvs_open(%s) failed: %d -- alarms run without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

} // namespace

// ---------- helpers --------------------------------------------------

uint32_t PhoneAlarmService::epochToMinute(uint32_t epoch) {
	// 60-second buckets give us a stable "this minute / next minute"
	// boundary the firing logic compares against. Wraparound is only
	// a concern past 2106-02 which is comfortably outside the device's
	// life; a uint32_t/60 has identical wrap behaviour either way.
	return epoch / 60UL;
}

// ---------- lifecycle ------------------------------------------------

void PhoneAlarmService::begin() {
	// Lazy-load on first begin(). Idempotent: a second begin() does
	// not re-attach the loop listener (LoopManager dedupes by pointer
	// in any case) and does not re-read NVS unless the first read
	// failed.
	if(!nvsLoaded) {
		loadFromNvs();
		nvsLoaded = true;
	}
	LoopManager::addListener(this);
}

void PhoneAlarmService::loadFromNvs() {
	if(!ensureOpen()) return;

	uint8_t blob[kBlobSize] = {};
	size_t  size = sizeof(blob);
	auto err = nvs_get_blob(s_handle, kBlobKey, blob, &size);
	if(err != ESP_OK)               return;
	if(size < kBlobSize)            return;
	if(blob[0] != kMagic0)          return;
	if(blob[1] != kMagic1)          return;
	if(blob[2] != kVersion)         return;

	for(uint8_t i = 0; i < MaxAlarms; ++i) {
		const uint8_t b0 = blob[kHeaderSize + i * 2 + 0];
		const uint8_t b1 = blob[kHeaderSize + i * 2 + 1];
		const uint8_t hour = b0 & 0x1F;        // 0..31, clamped below
		const bool    en   = (b0 & 0x80) != 0;
		const uint8_t min  = b1;

		alarms[i].hour    = (hour <= 23) ? hour : 0;
		alarms[i].minute  = (min  <= 59) ? min  : 0;
		alarms[i].enabled = en;
	}
}

void PhoneAlarmService::saveToNvs() const {
	if(!ensureOpen()) return;

	uint8_t blob[kBlobSize] = {};
	blob[0] = kMagic0;
	blob[1] = kMagic1;
	blob[2] = kVersion;
	blob[3] = 0;
	for(uint8_t i = 0; i < MaxAlarms; ++i) {
		uint8_t b0 = (uint8_t)(alarms[i].hour & 0x1F);
		if(alarms[i].enabled) b0 |= 0x80;
		blob[kHeaderSize + i * 2 + 0] = b0;
		blob[kHeaderSize + i * 2 + 1] = alarms[i].minute;
	}

	auto err = nvs_set_blob(s_handle, kBlobKey, blob, sizeof(blob));
	if(err != ESP_OK) {
		ESP_LOGW("AlarmService", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("AlarmService", "nvs_commit failed: %d", (int)err);
	}
}

// ---------- public state -------------------------------------------

PhoneAlarmService::Alarm PhoneAlarmService::getAlarm(uint8_t slot) const {
	if(slot >= MaxAlarms) return Alarm{};
	return alarms[slot];
}

void PhoneAlarmService::setAlarm(uint8_t slot, uint8_t hour, uint8_t minute, bool enabled) {
	if(slot >= MaxAlarms) return;

	if(hour   > 23) hour   = 23;
	if(minute > 59) minute = 59;

	alarms[slot].hour    = hour;
	alarms[slot].minute  = minute;
	alarms[slot].enabled = enabled;

	// Editing a slot that has a queued snooze cancels the snooze for
	// that slot — otherwise a user who edits "07:00 -> 07:15" while
	// snoozing would still get the original 07:05 ring back. Mirrors
	// the principle of least-surprise on every alarm clock.
	if(snoozeActive && snoozeIdx == (int8_t)slot) {
		snoozeActive     = false;
		snoozeIdx        = -1;
		snoozeUntilEpoch = 0;
	}

	saveToNvs();
}

void PhoneAlarmService::dismiss() {
	if(firing) {
		Ringtone.stop();
	}
	firing       = false;
	firingIdx    = -1;
	snoozeActive = false;
	snoozeIdx    = -1;
	snoozeUntilEpoch = 0;
}

void PhoneAlarmService::snooze(uint8_t minutes) {
	if(!firing) return;
	if(minutes == 0) minutes = DefaultSnoozeMin;

	const int8_t slot = firingIdx;

	Ringtone.stop();
	firing    = false;
	firingIdx = -1;

	// Snooze deadline is computed against the user's wall clock so a
	// device that has had the time set forward / back since the alarm
	// rang still snoozes by `minutes` of real time, not minutes of the
	// previous epoch.
	const uint32_t now = PhoneClock::nowEpoch();
	snoozeActive     = true;
	snoozeIdx        = slot;
	snoozeUntilEpoch = now + (uint32_t)minutes * 60UL;
}

// ---------- background tick ----------------------------------------

void PhoneAlarmService::triggerFire(uint8_t slot, uint32_t nowMinute) {
	firing                    = true;
	firingIdx                 = (int8_t)slot;
	lastFiredMinute[slot]     = nowMinute;
	// Snooze is consumed by a fire (whether natural or the snooze
	// delivering); a fresh ring needs a fresh snooze choice.
	snoozeActive     = false;
	snoozeIdx        = -1;
	snoozeUntilEpoch = 0;

	Ringtone.play(kAlarmMelody);
}

void PhoneAlarmService::loop(uint /*micros*/) {
	// Don't do anything while a fire is already active — the user has
	// not dismissed/snoozed yet, so we have nothing to evaluate. The
	// melody itself is already being driven by the global Ringtone
	// LoopListener.
	if(firing) return;

	const uint32_t epoch    = PhoneClock::nowEpoch();
	const uint32_t minNow   = epochToMinute(epoch);

	// Snooze check: a queued deferred re-fire takes precedence over
	// whatever the natural slot list says, because the user has
	// already told us what they wanted.
	if(snoozeActive) {
		if(epoch >= snoozeUntilEpoch) {
			const int8_t slot = snoozeIdx;
			snoozeActive     = false;
			snoozeIdx        = -1;
			snoozeUntilEpoch = 0;
			if(slot >= 0 && (uint8_t)slot < MaxAlarms) {
				triggerFire((uint8_t)slot, minNow);
				return;
			}
		}
	}

	// O(1) early-out for the common case where we are still inside
	// the same minute we last evaluated. Costs one comparison per
	// micro-tick when no minute boundary has been crossed.
	if(minNow == lastEvalMinute) return;
	lastEvalMinute = minNow;

	// Decompose the current epoch into HH:MM. We re-use PhoneClock's
	// civil-time decoder so leap-year and month-arithmetic logic stays
	// in a single place.
	uint16_t y; uint8_t mo, d, h, mi, s, dow;
	PhoneClock::now(y, mo, d, h, mi, s, dow);

	for(uint8_t i = 0; i < MaxAlarms; ++i) {
		const Alarm& a = alarms[i];
		if(!a.enabled)            continue;
		if(a.hour   != h)         continue;
		if(a.minute != mi)        continue;

		// MinuteRefireGuardSec stops a "we just dismissed in the same
		// minute" replay. Compute the guard in minutes — if the slot
		// fired within this same minute, skip.
		if(lastFiredMinute[i] == minNow) continue;

		triggerFire(i, minNow);
		return; // only one fire per loop tick
	}
}
