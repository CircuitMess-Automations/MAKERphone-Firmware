#ifndef CHATTER_FIRMWARE_PHONE_ALARM_SERVICE_H
#define CHATTER_FIRMWARE_PHONE_ALARM_SERVICE_H

#include <Arduino.h>
#include <Loop/LoopListener.h>

/**
 * S124 — PhoneAlarmService
 *
 * Background multi-alarm engine that backs the PhoneAlarmClock screen.
 * Lives as a global LoopListener so alarms continue to monitor wall
 * time even when the user is on a different screen — the screen is a
 * pure viewer/editor over this service's state, never the source of
 * truth.
 *
 * Design at a glance:
 *
 *   - Up to MaxAlarms (4) independent alarm slots. Four is plenty for
 *     a feature-phone (wake / nap / school-run / weekend-different
 *     covers most realistic patterns) and keeps the list-view fitting
 *     cleanly inside the 160x128 display without paging.
 *
 *   - Each slot is a tiny POD (hour, minute, enabled). No day-of-week
 *     pattern in v1 — once an alarm fires it remains armed for the
 *     next match (so a daily 07:00 keeps firing every day until the
 *     user disables it, matching the muscle memory of every basic
 *     phone alarm clock since the 90s).
 *
 *   - Snooze is a one-shot deferred trigger. After Snooze() the engine
 *     stops the active ringtone, then re-fires when the wall-clock
 *     reaches snoozeUntilEpoch. Snooze applies to the slot that was
 *     ringing; a fresh dismiss clears it.
 *
 *   - Persistence lives in NVS under the "mpalarm" namespace as a
 *     single 16-byte blob (4 alarm slots, 4 bytes each, plus a small
 *     header). Mirrors the PhoneComposerStorage pattern (single blob,
 *     single magic prefix, idempotent lazy open) so power-loss can
 *     never produce a half-written record.
 *
 *   - The ringing trigger is the global `Ringtone` engine (S39); a
 *     muted device gets the visual cue from PhoneAlarmClock but no
 *     piezo output, exactly like PhoneTimer's alarm.
 *
 * The service is idle-cheap: loop() runs on every micro-tick the
 * LoopManager dispatches, but the body short-circuits in O(1) unless
 * we have crossed a minute boundary or a snooze deadline. There is
 * no allocation in the hot path.
 *
 * Header-only public surface; the screen never touches NVS, the
 * Ringtone engine, or PhoneClock directly — every state transition
 * routes through one of the methods below.
 */
class PhoneAlarmService : public LoopListener {
public:
	/** Maximum number of independent alarm slots exposed in the UI. */
	static constexpr uint8_t MaxAlarms       = 4;

	/** Default snooze interval (minutes). Matches the muscle-memory
	 *  default on every Nokia / Sony Ericsson alarm clock. */
	static constexpr uint8_t DefaultSnoozeMin = 5;

	/** Cooldown (seconds) between two consecutive natural fires of the
	 *  same slot. Once the wall-clock crosses the alarm minute we set
	 *  this guard so a second loop tick within the same minute does
	 *  not re-trigger. */
	static constexpr uint32_t MinuteRefireGuardSec = 65;

	/** Tiny POD describing a single alarm. Two bytes of state; that is
	 *  intentional — it costs nothing to keep the layout dense, and
	 *  the NVS blob ends up well below the partition's per-key budget. */
	struct Alarm {
		uint8_t hour    = 0;   // 0..23
		uint8_t minute  = 0;   // 0..59
		bool    enabled = false;
	};

	/** Lifecycle. begin() is idempotent; safe to call from setup() or
	 *  the splash callback. */
	void begin();

	/** Read a slot. Out-of-range slots return a default-initialised
	 *  alarm (00:00, disabled) so callers can render unconditionally. */
	Alarm getAlarm(uint8_t slot) const;

	/** Replace a slot. Out-of-range slots are silently ignored.
	 *  Hour is clamped to 0..23 and minute to 0..59. The full alarm
	 *  table is then persisted to NVS so a subsequent reboot lands on
	 *  the same configuration. Snooze state is cleared if the user
	 *  edits the currently-snoozing slot to avoid surprise re-fires. */
	void setAlarm(uint8_t slot, uint8_t hour, uint8_t minute, bool enabled);

	/** True while the ringtone is actively alerting the user. Cleared
	 *  on dismiss() / snooze(). */
	bool isFiring() const { return firing; }

	/** Index (0..MaxAlarms-1) of the slot whose alarm is currently
	 *  active, or -1 if none. */
	int8_t firingSlot() const { return firing ? firingIdx : -1; }

	/** Stop the ringtone, mark not-firing, clear any snooze state.
	 *  No-op if no alarm is currently ringing. */
	void dismiss();

	/** Stop the ringtone, defer the next fire by `minutes`. The
	 *  service will re-fire the same alarm slot when the wall clock
	 *  reaches now + minutes. No-op if no alarm is currently ringing. */
	void snooze(uint8_t minutes = DefaultSnoozeMin);

	/** True iff a snooze is queued and pending. */
	bool isSnoozing() const { return snoozeActive; }

	/** Slot the queued snooze will re-fire. -1 if none. */
	int8_t snoozingSlot() const { return snoozeActive ? snoozeIdx : -1; }

	/** Background tick driven by LoopManager. */
	void loop(uint micros) override;

private:
	/** Stash for the slot configuration. Index by slot number. */
	Alarm alarms[MaxAlarms];

	bool   firing       = false;   // ringtone active
	int8_t firingIdx    = -1;      // slot driving the active ringtone

	bool     snoozeActive = false;
	int8_t   snoozeIdx    = -1;
	uint32_t snoozeUntilEpoch = 0;

	/** Last epoch-minute we evaluated. We only fire on a minute
	 *  rollover so the same alarm cannot trigger more than once per
	 *  minute even if loop() is called thousands of times. */
	uint32_t lastEvalMinute = 0;

	/** Last epoch-minute each slot fired naturally. Suppresses a
	 *  second fire inside the same minute window if the user dismisses
	 *  fast and stays inside the same wall-clock minute. */
	uint32_t lastFiredMinute[MaxAlarms] = { 0, 0, 0, 0 };

	bool nvsLoaded = false;

	/** Internal helpers. */
	void   loadFromNvs();
	void   saveToNvs() const;
	void   triggerFire(uint8_t slot, uint32_t nowMinute);
	static uint32_t epochToMinute(uint32_t epoch);
};

extern PhoneAlarmService Alarms;

#endif // CHATTER_FIRMWARE_PHONE_ALARM_SERVICE_H
