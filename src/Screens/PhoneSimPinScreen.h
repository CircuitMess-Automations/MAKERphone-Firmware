#ifndef MAKERPHONE_PHONESIMPINSCREEN_H
#define MAKERPHONE_PHONESIMPINSCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

/**
 * PhoneSimPinScreen (S162)
 *
 * Sony-Ericsson-style "SIM PIN unlock" boot screen. A purely DECORATIVE
 * four-digit PIN entry surface that flashes between PhoneBootSplash
 * (S56) and IntroScreen during boot, so the MAKERphone feels like a
 * real feature-phone waking up rather than just a Chatter with a
 * splash. Any 4-digit sequence is accepted (it is a costume, not a
 * security gate); BACK on an empty buffer skips the screen entirely so
 * power users do not have to type a PIN every boot.
 *
 *   +----------------------------------------+
 *   |    [SIM]                               |  <- tiny SIM-card icon
 *   |              ENTER PIN                 |  <- pixelbasic16 cream
 *   |               SIM 1                    |  <- pixelbasic7 dim caption
 *   |                                        |
 *   |          .--. .--. .--. .--.           |  <- four pin boxes
 *   |          |* | |* | |  | |  |           |     fill with * as digits
 *   |          '--' '--' '--' '--'           |     are typed
 *   |                                        |
 *   |   (A) ok    (B) clear / skip           |  <- pixelbasic7 dim hint
 *   +----------------------------------------+
 *
 * Behaviour
 *  - BTN_0..BTN_9        push a digit into the 4-slot buffer; the
 *                        matching pin box flashes briefly (LV_OPA flash)
 *                        and then renders as a "*" mask glyph.
 *  - BTN_BACK            with 0 digits typed: dismiss immediately
 *                        (the "no SIM PIN required" escape hatch).
 *                        With >=1 digit typed: clear the most recent
 *                        digit, same as a BACKSPACE.
 *  - BTN_ENTER (BTN_A)   only meaningful with a full 4-digit buffer:
 *                        runs the same "Checking..." -> "PIN OK" check
 *                        flash that auto-fires when the 4th digit lands.
 *  - On 4 digits filled  the 4th digit auto-triggers a brief
 *                        "PIN OK" tick (~700 ms total) and then
 *                        dismisses via the host DismissHandler.
 *
 * The screen is intentionally tolerant of any 4-digit PIN. There is no
 * stored / configurable PIN value - the goal is feature-phone nostalgia,
 * not actual SIM authentication. A future session could promote this to
 * a real lock by reading Settings.simPin and re-using the same screen
 * with a check-then-reject path. The current implementation never
 * rejects.
 *
 * Lifetime / dispatch
 *  - Mirrors PhoneBootSplash exactly: any-key OR auto-advance fires a
 *    single host-supplied DismissHandler. The screen tears itself down
 *    (stop, lv_obj_del) BEFORE invoking the callback so a slow handler
 *    can never re-enter against a half-freed screen, and a `dismissed`
 *    guard collapses the timer / key races to a single dispatch.
 *  - The screen does NOT pop() (no parent at boot - it is the second
 *    screen the device shows); the host callback owns starting the
 *    next screen (typically IntroScreen).
 *
 * Build-time gate
 *  - MAKERPHONE_SHOW_SIM_PIN (1 by default, see MAKERphoneConfig.h)
 *    inserts this screen between the splash and the intro. Setting it
 *    to 0 (or `-DMAKERPHONE_SHOW_SIM_PIN=0`) restores the legacy boot
 *    chain (splash -> intro -> ...) with no behavioural delta.
 */
class PhoneSimPinScreen : public LVScreen, private InputListener {
public:
	using DismissHandler = void (*)();

	/** Number of digits in the decorative PIN. Sized to match every
	 *  Sony-Ericsson / Nokia SIM PIN UX of the late-90s -> mid-00s. */
	static constexpr uint8_t  PinLength = 4;

	/** "PIN OK" tick window after the 4th digit lands (or after
	 *  BTN_ENTER with a full buffer). Kept short enough to feel
	 *  snappy, long enough to register as a real check rather than an
	 *  instant skip. */
	static constexpr uint32_t CheckHoldMs = 700;

	/**
	 * Build the PIN screen. The DismissHandler is invoked exactly once,
	 * after the user enters 4 digits (or skips via BACK on an empty
	 * buffer). The handler is responsible for instantiating + starting
	 * the next screen (typically a freshly-allocated IntroScreen). Pass
	 * nullptr to make the screen a self-tearing-down no-op overlay
	 * (mostly useful from tests).
	 */
	explicit PhoneSimPinScreen(DismissHandler onDismiss = nullptr);

	~PhoneSimPinScreen() override;

	void onStart() override;
	void onStop() override;

	/** From InputListener - drives digit entry / BACK / ENTER. */
	void buttonPressed(uint i) override;

private:
	DismissHandler dismissCb;
	bool           dismissedAlready;

	uint8_t        digitsEntered;
	bool           checking;            // true while the post-4-digit
	                                    // "PIN OK" hold is on screen

	lv_obj_t*      simIcon;             // tiny pixel SIM-card glyph
	lv_obj_t*      simChip;             // gold contact rectangle inside icon
	lv_obj_t*      title;               // "ENTER PIN"
	lv_obj_t*      caption;             // "SIM 1"
	lv_obj_t*      pinBoxes[PinLength]; // four mask boxes
	lv_obj_t*      pinGlyphs[PinLength];// label inside each box ("*")
	lv_obj_t*      hint;                // "(A) ok  (B) clear / skip"
	lv_obj_t*      checkLabel;          // "PIN OK" tick line

	lv_timer_t*    checkTimer;          // delays the dismiss after PinLength
	                                    // digits are filled

	void buildBackground();
	void buildSimIcon();
	void buildTitle();
	void buildCaption();
	void buildPinBoxes();
	void buildHint();
	void buildCheckLabel();

	void renderPinBoxes();              // re-paint each box's filled state
	void updateHint();                  // hint text follows the buffer state

	void onDigit(uint8_t d);            // BTN_0..BTN_9
	void onBackspace();                 // BTN_BACK
	void onConfirm();                   // BTN_ENTER (only at full buffer)

	void startCheckHold();              // brief "PIN OK" flash before dismiss
	void stopCheckHold();
	void fireDismiss();

	static void onCheckTimer(lv_timer_t* timer);
};

#endif // MAKERPHONE_PHONESIMPINSCREEN_H
