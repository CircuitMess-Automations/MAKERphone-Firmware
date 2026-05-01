#ifndef CHATTER_FIRMWARE_PHONE_IDLE_DIM_H
#define CHATTER_FIRMWARE_PHONE_IDLE_DIM_H

#include <Arduino.h>
#include <Loop/LoopListener.h>
#include <Input/InputListener.h>

/**
 * S69 — PhoneIdleDim
 *
 * Tiny power-saving service that auto-dims the backlight after a short
 * window of inactivity, and restores the user's full brightness on the
 * next button press.
 *
 * Sits between "user is actively using the phone" and "SleepService
 * pulls the plug" — i.e. it is the soft, reversible step before the
 * existing SleepService kicks in (`SleepService` owns the hard sleep
 * behaviour: full backlight fade-out + light-sleep + LoRa-aware
 * wake — see `src/Services/SleepService.cpp`). Both services share
 * the same any-key-resets-the-clock contract; nothing here changes
 * SleepService's timing or its configurable `Settings.sleepTime` index.
 *
 * Behaviour:
 *   - Boots in the bright state, mirroring `Settings.screenBrightness`.
 *   - After `IDLE_DIM_MS` ms with no button activity, calls
 *     `Chatter.setBrightness()` with a dimmer level (a fraction of the
 *     user's setting, floored so the screen stays readable in a dim
 *     room).
 *   - Any button press (via `InputListener::anyKeyPressed()`) instantly
 *     restores the user's full brightness and resets the idle clock.
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

	// Externally pokeable — handy for screens that drive activity
	// without going through Input (e.g. an incoming-call ringer waking
	// the user). Cheap no-op if we're already in the bright state.
	void resetActivity();

	bool isDimmed() const { return dimmed; }

	// Idle window before we slip into the dim state, in ms. 30 s
	// matches the roadmap entry "auto-dim after 30s".
	static constexpr uint32_t IDLE_DIM_MS = 30000;

	// Dimmed brightness as a fraction of the user-configured
	// `Settings.screenBrightness` (0..255). 30% is dim enough to feel
	// like a real phone's idle dim, while still being clearly visible
	// in a normally-lit room.
	static constexpr float DIM_FACTOR = 0.30f;

	// Hard floor so even with screenBrightness=0 the screen stays
	// faintly lit while idle (mostly defensive — the brightness slider
	// already clamps above zero, but if anything does write 0 we don't
	// want to sneak a fully-black backlight in here).
	static constexpr uint8_t DIM_FLOOR = 30;

private:
	void anyKeyPressed() override;

	// Recompute and apply the bright / dim brightness based on the
	// current `Settings.screenBrightness`. Cheap and idempotent.
	void applyBrightness(bool toDim);

	uint32_t activityTime = 0;
	bool dimmed = false;
	uint8_t lastApplied = 0; // last brightness we wrote, to skip redundant ledc updates
};

extern PhoneIdleDim IdleDim;

#endif // CHATTER_FIRMWARE_PHONE_IDLE_DIM_H
