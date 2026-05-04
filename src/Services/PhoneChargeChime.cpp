#include "PhoneChargeChime.h"

#include "PhoneRingtoneEngine.h"

#include <Arduino.h>
#include <Chatter.h>
#include <Loop/LoopManager.h>
#include <Notes.h>

// =====================================================================
// S150 — PhoneChargeChime
//
// Implementation overview:
//
//   1. Sampling: every TrendSampleMs we push the current Battery
//      voltage into an 8-slot ring buffer. The head wraps so the
//      oldest sample sits at trendHead and the newest sits at
//      (trendHead + TrendSamples - 1) % TrendSamples.
//
//   2. Decisions: trendDeltaMv() returns (newest - oldest). Positive
//      means the cell is taking charge; near-zero means the CV-stage
//      tail or a finished top-up; strongly negative means the device
//      is back on battery (charger unplugged).
//
//   3. State transitions:
//        Idle      -> Charging   when delta >= ChargingRiseMv
//        Charging  -> Complete   when window full,
//                                delta < CompleteFlatMv,
//                                AND battery looks topped out
//                                ( percentage >= CompletePercent OR
//                                  voltage    >= CompleteVoltMv )
//                                Fires the chime exactly once.
//        Complete  -> Idle       when delta <= -UnplugDropMv
//                                (charger pulled, voltage sags).
//
//   4. Manual overrides (`setCharging(bool)`, `notifyChargeComplete()`)
//      flip the state machine and start a ManualGuardMs window during
//      which the heuristic cannot revise the call. The chime is only
//      ever played by `notifyChargeComplete()` or the natural
//      Charging -> Complete edge — `setCharging(false)` from the host
//      is treated as "the device went back on battery", same as the
//      voltage-drop path, and stays silent.
//
//   5. The chime melody is a short major-third ascending chord
//      finishing on a held note — bright and short enough to be heard
//      over a desk full of clutter without ever feeling like a
//      ringtone. It is fired one-shot through the global Ringtone
//      engine, so Settings.sound mute is honoured automatically.
//
// =====================================================================

PhoneChargeChime ChargeChime;

namespace {

// Three-step ascending chord ending on a held G6: a clean "completed!"
// signature distinct from the boot G-major arpeggio (S148, ascending
// over a wider span) and the power-off arpeggio (S149, descending).
// Total ~520 ms including the inter-note gaps — quick enough that the
// user can pick the device up and not hear it tail off, but distinctive
// enough to be unmistakable on a desk.
const PhoneRingtoneEngine::Note kChargeCompleteNotes[] = {
		{ NOTE_C6, 110 },
		{ NOTE_E6, 110 },
		{ NOTE_G6, 280 },
};

const PhoneRingtoneEngine::Melody kChargeCompleteMelody = {
		kChargeCompleteNotes,
		sizeof(kChargeCompleteNotes) / sizeof(kChargeCompleteNotes[0]),
		30,        // gapMs — same family as S148/S149 chimes
		false,     // one-shot
		"ChrgDone",
};

} // namespace

// ---------- lifecycle ------------------------------------------------

void PhoneChargeChime::begin() {
	// Reset all bookkeeping. begin() is idempotent: a second call
	// resets the trend buffer (LoopManager dedupes the listener
	// pointer) so a second wake-up does not stale-fire on samples
	// taken from before the previous sleep.
	state           = State::Idle;
	firedThisCycle  = false;
	trendHead       = 0;
	trendCount      = 0;
	for(uint8_t i = 0; i < TrendSamples; i++) {
		trendBuf[i] = 0;
	}
	lastSampleMs   = millis();
	manualGuardEnd = 0;
	postChimeUntil = 0;

	LoopManager::addListener(this);
}

// ---------- public hooks --------------------------------------------

void PhoneChargeChime::setCharging(bool on) {
	manualGuardEnd = millis() + ManualGuardMs;

	if(on) {
		// Enter Charging from any state. Reset the per-cycle fire
		// flag so the next natural Charging -> Complete edge can
		// chime again (it would otherwise stay suppressed for the
		// rest of the device's life).
		state          = State::Charging;
		firedThisCycle = false;
	} else {
		// Caller declared "no longer charging" — treat it as the
		// charger having been unplugged. No chime, just back to
		// Idle. A pending firedThisCycle is also cleared so the
		// next charge cycle is observable.
		state          = State::Idle;
		firedThisCycle = false;
	}
}

void PhoneChargeChime::notifyChargeComplete() {
	manualGuardEnd = millis() + ManualGuardMs;
	state          = State::Complete;
	fireChimeOnce();
}

void PhoneChargeChime::reset() {
	state           = State::Idle;
	firedThisCycle  = false;
	trendHead       = 0;
	trendCount      = 0;
	for(uint8_t i = 0; i < TrendSamples; i++) {
		trendBuf[i] = 0;
	}
	manualGuardEnd  = 0;
	postChimeUntil  = 0;
}

// ---------- background tick -----------------------------------------

void PhoneChargeChime::loop(uint /*micros*/) {
	const uint32_t now = millis();

	// Cheap guard: only do work on the trend cadence. The default
	// 1 000 ms tick keeps cost negligible compared to LVGL's
	// per-frame draw work.
	if(now - lastSampleMs < TrendSampleMs) return;
	lastSampleMs = now;

	sampleTrend();

	// While a manual override is active we let the trend buffer
	// continue to fill but we suppress the heuristic's transitions.
	// This guarantees the host's intent survives the next few sample
	// ticks without being immediately re-evaluated.
	if(now < manualGuardEnd) return;

	// A short cooldown after the chime fires keeps a noisy CV
	// plateau from immediately bouncing the engine back into
	// Charging on its own. The state remains in Complete during
	// the guard.
	if(now < postChimeUntil) return;

	if(!windowFull()) return;

	const int16_t delta = trendDeltaMv();

	switch(state) {
		case State::Idle:
			if(delta >= ChargingRiseMv) {
				state          = State::Charging;
				firedThisCycle = false;
			}
			break;

		case State::Charging:
			// Plateau detection: trend has gone flat AND the cell
			// reads "near full". If only the trend is flat but the
			// percent is mid-pack (e.g. a stalled charge with a
			// dodgy cable) we stay in Charging — the chime should
			// only fire when the device really did finish.
			if(delta < CompleteFlatMv && batteryFull()) {
				state = State::Complete;
				fireChimeOnce();
			}
			break;

		case State::Complete:
			// Stay parked until the voltage clearly drops. A
			// charger unplug, or the device starting to consume
			// the cell, both produce a multi-sample drop large
			// enough to clear UnplugDropMv.
			if(delta <= -UnplugDropMv) {
				state          = State::Idle;
				firedThisCycle = false;
			}
			break;
	}
}

// ---------- helpers --------------------------------------------------

void PhoneChargeChime::sampleTrend() {
	const uint16_t v = Battery.getVoltage();
	trendBuf[trendHead] = v;
	trendHead = (uint8_t)((trendHead + 1) % TrendSamples);
	if(trendCount < TrendSamples) trendCount++;
}

int16_t PhoneChargeChime::trendDeltaMv() const {
	if(!windowFull()) return 0;

	const uint8_t newestIdx = (uint8_t)((trendHead + TrendSamples - 1) % TrendSamples);
	const uint8_t oldestIdx = trendHead; // ring buffer wraps to oldest

	const int32_t newest = (int32_t) trendBuf[newestIdx];
	const int32_t oldest = (int32_t) trendBuf[oldestIdx];
	int32_t delta = newest - oldest;

	// Clamp to int16_t to keep the public type narrow; physical
	// transients on the ESP32 ADC never approach +/-32 V anyway, so
	// any saturation here would already mean we are looking at noise.
	if(delta >  INT16_MAX) delta = INT16_MAX;
	if(delta < -INT16_MAX) delta = -INT16_MAX;
	return (int16_t) delta;
}

bool PhoneChargeChime::batteryFull() const {
	const uint8_t  pct = Battery.getPercentage();
	const uint16_t v   = Battery.getVoltage();
	return (pct >= CompletePercent) || (v >= CompleteVoltMv);
}

void PhoneChargeChime::fireChimeOnce() {
	if(firedThisCycle) return;
	firedThisCycle = true;
	postChimeUntil = millis() + PostChimeGuardMs;

	// Ringtone engine internally short-circuits the piezo when
	// Settings.sound is false, so a muted device still observes the
	// state transition but produces no audible chime.
	Ringtone.play(kChargeCompleteMelody);
}
