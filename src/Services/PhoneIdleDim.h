#ifndef CHATTER_FIRMWARE_PHONE_IDLE_DIM_H
#define CHATTER_FIRMWARE_PHONE_IDLE_DIM_H

#include <Arduino.h>
#include <Loop/LoopListener.h>
#include <Input/InputListener.h>

/**
 * S69 / S198 - PhoneIdleDim
 *
 * Tiny power-saving service that auto-dims the backlight after a short
 * window of inactivity, and restores the user's full brightness on the
 * next button press.
 *
 * Sits between "user is actively using the phone" and "SleepService
 * pulls the plug" - i.e. it is the soft, reversible step before the
 * existing SleepService kicks in (`SleepService` owns the hard sleep
 * behaviour: full backlight fade-out + light-sleep + LoRa-aware
 * wake - see `src/Services/SleepService.cpp`). Both services share
 * the same any-key-resets-the-clock contract; nothing here changes
 * SleepService's timing or its configurable `Settings.sleepTime` index.
 *
 * Two-stage dim (S198 battery-life pass):
 *
 *   - Stage 0 (Bright): mirrors `Settings.screenBrightness` exactly.
 *   - Stage 1 (Dim): after `IDLE_DIM_MS` (30 s) of no input, falls to
 *     `DIM_FACTOR` (30 %) of the user's brightness. Same reversible
 *     "any key restores full brightness" semantics as before.
 *   - Stage 2 (Deep dim): after `DEEP_DIM_MS` (90 s) of no input, falls
 *     further to `DEEP_DIM_FACTOR` (12 %). This is still well above the
 *     "panel electrically off" floor SleepService owns; the screen stays
 *     readable in a dim room but the backlight current draw is ~3x
 *     lower than Stage 1. This is the new battery-life knob added in
 *     S198 - on a typical 3.7 V/700 mAh Chatter cell, the backlight is
 *     the dominant idle draw, and dropping to 12 % during the long
 *     idle window before SleepService takes over buys ~10-15 % of
 *     standby battery life back.
 *
 * Behaviour:
 *   - Boots in the bright state, mirroring `Settings.screenBrightness`.
 *   - After `IDLE_DIM_MS` ms with no button activity, calls
 *     `Chatter.setBrightness()` with a dimmer level (a fraction of the
 *     user's setting, floored so the screen stays readable in a dim
 *     room).
 *   - After `DEEP_DIM_MS` ms of *continuous* idleness, drops further to
 *     `DEEP_DIM_FACTOR` of the user's brightness. SleepService still
 *     owns the deeper "panel off + light-sleep" step beyond that.
 *   - Any button press (via `InputListener::anyKeyPressed()`) instantly
 *     restores the user's full brightness and resets the idle clock,
 *     regardless of which stage we were in.
 *   - Skips silently while the backlight is electrically off (i.e.
 *     `Chatter.backlightPowered() == false`), which is the case during
 *     `SleepService::enterSleep()`'s fade-out and the light-sleep
 *     window. After SleepService's `fadeIn()` brings the backlight
 *     back, the next any-key event re-syncs us automatically.
 *
 * The service is intentionally separate from SleepService so the dim
 * step can be tweaked, disabled, or re-timed without touching the
 * carefully-balanced sleep / shutdown / LoRa-wake state machine.
 */
class PhoneIdleDim : public LoopListener, private InputListener {
public:
	PhoneIdleDim();
	void begin();

	void loop(uint micros) override;

	// Externally pokeable - handy for screens that drive activity
	// without going through Input (e.g. an incoming-call ringer waking
	// the user). Cheap no-op if we're already in the bright state.
	void resetActivity();

	enum class Stage : uint8_t {
		Bright   = 0,
		Dim      = 1,
		DeepDim  = 2,
	};

	Stage stage() const { return currentStage; }

	// Backwards-compatible accessor: anything that previously asked
	// "is the screen dim right now?" wants either dim stage to count.
	bool isDimmed() const { return currentStage != Stage::Bright; }
	bool isDeepDimmed() const { return currentStage == Stage::DeepDim; }

	/** Milliseconds since the last activity (button press / external poke).
	 *  Useful for diag screens that want to surface the idle clock. */
	uint32_t msSinceActivity() const { return (uint32_t)(millis() - activityTime); }

	/** Last brightness we wrote to the panel (0..255). 0 if we have not
	 *  written one yet (e.g. boot, or while the panel is electrically off). */
	uint8_t lastAppliedBrightness() const { return lastApplied; }

	// Idle window before we slip into the dim state, in ms. 30 s
	// matches the original S69 roadmap entry "auto-dim after 30s".
	static constexpr uint32_t IDLE_DIM_MS = 30000;

	// Idle window before we drop further into the deep-dim state. The
	// gap from IDLE_DIM_MS to DEEP_DIM_MS is the "shallow dim" window
	// during which a quick glance is still legible at DIM_FACTOR
	// brightness; once we cross DEEP_DIM_MS the user has clearly
	// stopped paying attention, so it is safe to drop further.
	static constexpr uint32_t DEEP_DIM_MS = 90000;

	// Dimmed brightness as a fraction of the user-configured
	// `Settings.screenBrightness` (0..255). 30 % is dim enough to feel
	// like a real phone's idle dim, while still being clearly visible
	// in a normally-lit room.
	static constexpr float DIM_FACTOR = 0.30f;

	// Deep-dim brightness as a fraction of the user's setting. 12 %
	// keeps the LovyanGFX backlight visibly on (the panel does not
	// look electrically dead, which would be confusing) while
	// dropping the dominant idle current draw substantially.
	static constexpr float DEEP_DIM_FACTOR = 0.12f;

	// Hard floor so even with screenBrightness=0 the screen stays
	// faintly lit while idle (mostly defensive - the brightness slider
	// already clamps above zero, but if anything does write 0 we don't
	// want to sneak a fully-black backlight in here).
	static constexpr uint8_t DIM_FLOOR = 30;

	// Hard floor for deep-dim. A touch below DIM_FLOOR but still
	// above zero so the panel is clearly "alive but resting", never
	// "powered off".
	static constexpr uint8_t DEEP_DIM_FLOOR = 18;

private:
	void anyKeyPressed() override;

	// Recompute and apply the brightness for the requested stage based
	// on the current `Settings.screenBrightness`. Cheap and idempotent.
	void applyBrightnessFor(Stage s);

	uint32_t activityTime = 0;
	Stage currentStage = Stage::Bright;
	uint8_t lastApplied = 0; // last brightness we wrote, to skip redundant ledc updates
};

extern PhoneIdleDim IdleDim;

#endif // CHATTER_FIRMWARE_PHONE_IDLE_DIM_H
