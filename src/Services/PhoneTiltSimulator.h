#ifndef CHATTER_FIRMWARE_PHONE_TILT_SIMULATOR_H
#define CHATTER_FIRMWARE_PHONE_TILT_SIMULATOR_H

#include <Arduino.h>
#include <Loop/LoopListener.h>
#include <Input/InputListener.h>

/**
 * S168 - PhoneTiltSimulator (service)
 *
 * The MAKERphone hardware has no IMU, so we fake "shake the phone"
 * with a two-handed keypad chord: hold BTN_L and BTN_R together for
 * a short, deliberate window (HoldMs = 300 ms). When the chord
 * latches, the service calls `LVScreen::getCurrent()->onShake()` on
 * whichever screen is currently visible. Screens that have something
 * meaningful to randomize (PhoneDiceRoller -> roll, PhoneMagic8Ball
 * -> shake, PhoneFortuneCookie -> crack a fresh fortune, and so on)
 * override the LVScreen::onShake() hook; everyone else inherits the
 * default no-op so the gesture is silently ignored.
 *
 * Why a chord and not a single button? L and R are the only two
 * keys that almost no screen uses as its "primary action" (BTN_5,
 * BTN_ENTER and the soft-keys cover that role across the firmware).
 * Holding both at once is also ergonomically reminiscent of
 * grabbing the phone with two hands ready to shake it - the
 * skeuomorph the roadmap entry asks for ("Tilt simulator (hold L+R
 * together) -> shake-to-randomize current screen").
 *
 * Coexistence with screens that DO use L+R together:
 *
 *   - PhonePinball uses BTN_L/BTN_R as flipper triggers and
 *     frequently has both held at once during normal play.
 *     PhonePinball does NOT override onShake() (it inherits the
 *     LVScreen default), so even if we fire the gesture mid-flip
 *     the screen ignores it - flipper play is unaffected.
 *
 *   - The same is true of PhoneDiceRoller's idle screen which uses
 *     LEFT/RIGHT to cycle face counts: BTN_L is a separate hardware
 *     key from BTN_LEFT (per Pins.hpp), so cycling the dice mode
 *     doesn't accidentally arm the gesture.
 *
 * Detection state machine:
 *
 *   Idle (neither held)
 *     -> on press of L or R, mark that side held.
 *   One held
 *     -> on press of the other side, transition to BothHeld and
 *        record bothHeldStartMs = now.
 *   BothHeld
 *     -> on each loop tick, if (now - bothHeldStartMs) >= HoldMs
 *        AND we have not yet fired this hold, call onShake() on
 *        the current LVScreen and set firedThisHold = true. The
 *        flag prevents repeated fires while the chord is still
 *        held.
 *     -> on release of either side, transition back to one-held
 *        (or Idle), clear firedThisHold so the next chord can
 *        fire freshly.
 *
 * Idle-cheap: the loop() callback is a single millis() compare and
 * an early return when the chord is not active. No allocation, no
 * UI work - the gesture just calls onShake() and the screen does
 * the actual randomize work in its own override.
 *
 * Resets:
 *
 *   - Releasing either side mid-hold (before HoldMs elapses)
 *     cancels the pending fire. The user has to re-press both
 *     sides to start a fresh chord.
 *
 *   - Releasing either side after a fire clears firedThisHold so
 *     the next chord can fire onShake() again. There is no further
 *     debounce on top of that - if the user presses L+R, gets a
 *     shake, releases, and presses L+R again, they get a second
 *     shake immediately. This matches the way physically shaking
 *     a Magic 8-Ball / dice cup works (you can shake again as soon
 *     as you've reset your grip).
 */
class PhoneTiltSimulator : public LoopListener, private InputListener {
public:
	void begin();

	/** Clear the gesture state. Useful for tests, also called from
	 *  begin() so the service starts in a known state regardless of
	 *  the order of boot-time button events. */
	void reset();

	/** Read-only view of the gesture state for tests and diagnostics. */
	bool isLHeld()    const { return lHeld; }
	bool isRHeld()    const { return rHeld; }
	bool isArmed()    const { return lHeld && rHeld; }
	bool hasFired()   const { return firedThisHold; }

	/** Hold threshold (ms). The chord must remain pressed for at
	 *  least this long before the gesture latches. 300 ms is long
	 *  enough that an accidental simultaneous press (e.g. an
	 *  ergonomic finger fumble) does not trigger a shake, and short
	 *  enough that a deliberate hold still feels snappy. */
	static constexpr uint32_t HoldMs = 300;

private:
	void buttonPressed(uint i)  override;
	void buttonReleased(uint i) override;
	void loop(uint micros)      override;

	/** Apply the gesture: dispatch to the current LVScreen's
	 *  onShake() hook. Centralised here so loop() stays readable. */
	void fireShake();

	bool     lHeld          = false;
	bool     rHeld          = false;
	bool     firedThisHold  = false;
	uint32_t bothHeldStartMs = 0;
};

extern PhoneTiltSimulator Tilt;

#endif // CHATTER_FIRMWARE_PHONE_TILT_SIMULATOR_H
