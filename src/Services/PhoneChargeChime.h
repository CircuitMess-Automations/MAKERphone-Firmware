#ifndef CHATTER_FIRMWARE_PHONE_CHARGE_CHIME_H
#define CHATTER_FIRMWARE_PHONE_CHARGE_CHIME_H

#include <Arduino.h>
#include <Loop/LoopListener.h>

/**
 * S150 — PhoneChargeChime (service)
 *
 * Background "battery filled up" cue. A single one-shot melody fires
 * the moment the device transitions from "charging" to "charge
 * complete" — exactly the way late-2000s feature phones thanked the
 * user with a tiny chime once the cell topped out and the charger
 * dropped to a trickle.
 *
 * The Chatter battery service exposes voltage / percentage but no
 * `isCharging()` flag, so the trigger is reconstructed from a rolling
 * voltage trend, the same heuristic PhoneChargingOverlay (S59) uses
 * for its bolt chip — but reused here as a system-wide service so the
 * chime fires regardless of which screen happens to be foregrounded.
 *
 * State machine (drained -> filled -> unplugged):
 *
 *      Idle  --rising voltage--->  Charging
 *      Charging --plateau & ~full--> Complete   ----> chime fires once
 *      Complete --voltage drop-->  Idle
 *
 * Rising / plateau detection uses an 8-sample ring buffer with the
 * same parameters as the overlay so the two stay coherent: a >= 30 mV
 * rise across the window declares "charging", and a sustained < 10 mV
 * rise across the same window while the percentage label is at >= 95 %
 * (or voltage at >= 4 150 mV) declares "complete". A voltage drop of
 * >= 50 mV during Complete returns the state to Idle so subsequent
 * charge cycles still fire.
 *
 * The chime is routed through the global PhoneRingtoneEngine (S39),
 * so the existing Settings.sound mute applies automatically — silent
 * profiles produce a state transition but no audible output.
 *
 * Host hooks:
 *
 *   - `setCharging(true/false)` lets a future explicit USB-detect
 *     signal (or unit-test harness) override the trend heuristic.
 *     The override behaves identically to a heuristic-driven
 *     transition, so a test can drive the full cycle synchronously.
 *
 *   - `notifyChargeComplete()` fires the chime immediately and parks
 *     the state machine in Complete. Useful for QA tests and any
 *     future hardware path that exposes a real "charging done"
 *     interrupt.
 *
 *   - `reset()` returns to Idle without making any noise.
 *
 * Idle-cheap: the loop body only does work once per
 * `TrendSampleMs` (1 s); the rest of the time it short-circuits on a
 * single millis() comparison. There is no allocation in the hot
 * path.
 */
class PhoneChargeChime : public LoopListener {
public:
	enum class State : uint8_t {
		Idle     = 0,
		Charging = 1,
		Complete = 2,
	};

	/** Lifecycle. begin() is idempotent. */
	void begin();

	/** Background tick driven by LoopManager. */
	void loop(uint micros) override;

	/** Force the state machine into Charging / Idle. While an explicit
	 *  override is active the trend heuristic is suppressed for
	 *  ManualGuardMs so the next sample window cannot immediately
	 *  contradict the host's intent. Transition Charging -> Idle via
	 *  this path does NOT fire the chime — only the state machine's
	 *  natural Charging -> Complete edge does. */
	void setCharging(bool on);

	/** Fire the chime immediately and park in Complete. Subsequent
	 *  trend evaluation still re-arms back to Idle when the voltage
	 *  drops, so the next charge cycle is still observable. */
	void notifyChargeComplete();

	/** Force back to Idle without making noise. Clears the trend
	 *  buffer so the next charge cycle starts from a clean window. */
	void reset();

	/** Test-friendly accessors. */
	State currentState() const { return state; }
	bool  hasFired()     const { return firedThisCycle; }

	// ---- tunables ---------------------------------------------------

	/** Voltage trend cadence (ms). Mirrors PhoneChargingOverlay so the
	 *  two heuristics decide things at the same frame. */
	static constexpr uint16_t TrendSampleMs    = 1000;

	/** Number of samples kept in the ring buffer. */
	static constexpr uint8_t  TrendSamples     = 8;

	/** mV rise across the full window required to declare Charging. */
	static constexpr int16_t  ChargingRiseMv   = 30;

	/** mV rise tolerated across the full window while still calling the
	 *  trend "flat" (CV-stage tail end of a top-up). */
	static constexpr int16_t  CompleteFlatMv   = 10;

	/** mV drop across the full window that returns Complete -> Idle
	 *  (charger unplugged, battery in use again). */
	static constexpr int16_t  UnplugDropMv     = 50;

	/** Battery percentage at-or-above which the Charging -> Complete
	 *  transition is allowed. Below this we keep the engine in
	 *  Charging even if the trend goes flat (most likely a stalled
	 *  charge or a flaky cable, not a real top-out). */
	static constexpr uint8_t  CompletePercent  = 95;

	/** Voltage (mV) at-or-above which the percentage gate above is
	 *  bypassed. Some packs read 4 150 - 4 180 mV at 99 % under load. */
	static constexpr uint16_t CompleteVoltMv   = 4150;

	/** How long the host's manual override of charging state is
	 *  protected from being re-overridden by the heuristic, in ms. */
	static constexpr uint16_t ManualGuardMs    = 4000;

	/** Cooldown after the chime fires before the heuristic is allowed
	 *  to flip Charging back on again (debounces a noisy plateau). */
	static constexpr uint16_t PostChimeGuardMs = 4000;

private:
	State    state           = State::Idle;
	bool     firedThisCycle  = false;

	// Sample ring buffer.
	uint16_t trendBuf[TrendSamples] = {};
	uint8_t  trendHead              = 0;
	uint8_t  trendCount             = 0;

	uint32_t lastSampleMs   = 0;
	uint32_t manualGuardEnd = 0;
	uint32_t postChimeUntil = 0;

	// Helpers.
	void     sampleTrend();
	int16_t  trendDeltaMv()  const;
	bool     windowFull()    const { return trendCount >= TrendSamples; }
	bool     batteryFull()   const;
	void     fireChimeOnce();
};

extern PhoneChargeChime ChargeChime;

#endif // CHATTER_FIRMWARE_PHONE_CHARGE_CHIME_H
