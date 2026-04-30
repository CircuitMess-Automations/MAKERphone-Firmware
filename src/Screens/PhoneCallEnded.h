#ifndef MAKERPHONE_PHONECALLENDED_H
#define MAKERPHONE_PHONECALLENDED_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhonePixelAvatar;

/**
 * PhoneCallEnded
 *
 * The MAKERphone 2.0 "call just ended" overlay (S26). Third Phase-D
 * screen, sandwiched between PhoneActiveCall (S25) and the eventual
 * PhoneCallHistory (S27). The host pushes one of these on top of
 * PhoneActiveCall the moment the user (or the remote peer) hangs up;
 * the screen then displays a brief "CALL ENDED — Xm Ys" summary, holds
 * for ~1.5 s, and auto-dismisses back to whoever pushed it (typically
 * PhoneHomeScreen via the active-call screen's end callback). Composes
 * the same Phase-A widgets the rest of the call-flow uses (PhoneSynthwaveBg,
 * PhoneStatusBar, PhoneSoftKeyBar, PhonePixelAvatar) so the visual
 * language stays uniform across the incoming -> active -> ended
 * sequence:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |             CALL ENDED                 | <- pixelbasic7 caption (orange)
 *   |             +----------+               |
 *   |             | (avatar) |               | <- PhonePixelAvatar 32x32
 *   |             +----------+               |
 *   |              ALEX KIM                  | <- pixelbasic7 caller name
 *   |              0m 42s                    | <- pixelbasic16 duration
 *   |                                        |
 *   |          press any key                 | <- pixelbasic7 hint (dim)
 *   |                                  HOME  | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Implementation notes:
 *  - Code-only - no SPIFFS assets. Reuses PhonePixelAvatar (S11), the
 *    standard top/bottom bars, and a synthwave wallpaper. Data
 *    partition cost stays zero.
 *  - 160x128 budget: the layout intentionally mirrors PhoneActiveCall
 *    (caption, avatar, name, big-glyph readout) so the user feels the
 *    call-flow visually settle rather than jump-cut. The caption uses
 *    sunset orange (rather than the active-call cyan) to read as
 *    "alert / state change", and the duration replaces the live timer
 *    in pixelbasic16 warm cream as the focal element.
 *  - Auto-dismiss after AutoDismissMs ms (default 1500). Driven by a
 *    one-shot lv_timer started in onStart() so a host that constructs
 *    the screen ahead of time still gets the full 1.5 s window from
 *    the moment the user actually sees it. The timer fires fireDismiss()
 *    which pops back to the parent (the screen that pushed us) so the
 *    classic "call ended -> homescreen" loop closes itself with no
 *    extra wiring required at the call site.
 *  - Any hardware key (BTN_BACK, BTN_ENTER, BTN_LEFT, BTN_RIGHT, any
 *    keypad digit) short-circuits the auto-dismiss timer and pops
 *    immediately, so the user never feels held hostage by the overlay.
 *    Same "press any key to skip" pattern the boot splash will use in
 *    S56.
 *  - The duration is rendered as "Xm Ys" (e.g. "0m 42s", "12m 03s")
 *    rather than mm:ss because the call has *ended* - we want the
 *    label to read as a noun ("the call lasted 0m 42s") rather than
 *    a clock. This also visually distinguishes it from the live
 *    PhoneActiveCall timer at a glance.
 *  - The screen does NOT take ownership of the call lifecycle. The
 *    expected wiring is: PhoneActiveCall::endCb hangs up audio,
 *    captures getDurationSeconds(), pops itself, and the parent
 *    immediately pushes a PhoneCallEnded with that duration. After
 *    auto-dismiss the parent (homescreen) is restored.
 *  - Caller info is copied into a fixed-size internal buffer (same
 *    MaxNameLen cap PhoneIncomingCall / PhoneActiveCall use) so a
 *    host that allocates the name on the stack does not need to keep
 *    it alive for the screen lifetime.
 */
class PhoneCallEnded : public LVScreen, private InputListener {
public:
	using ActionHandler = void (*)(PhoneCallEnded* self);

	/**
	 * Build a call-ended overlay summarising a finished call.
	 *
	 * @param callerName       Display name shown above the duration. The
	 *                         screen copies this internally; the original
	 *                         may be freed afterwards. May be nullptr
	 *                         (renders "UNKNOWN").
	 * @param durationSeconds  Length of the call, in whole seconds.
	 *                         Typical caller passes
	 *                         PhoneActiveCall::getDurationSeconds()
	 *                         captured just before pop().
	 * @param avatarSeed       Seed forwarded to PhonePixelAvatar so the
	 *                         avatar matches what the active-call /
	 *                         incoming-call screen showed for this peer.
	 */
	PhoneCallEnded(const char* callerName,
				   uint32_t    durationSeconds,
				   uint8_t     avatarSeed = 0);
	virtual ~PhoneCallEnded() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Bind a callback fired when the overlay dismisses (either via
	 * auto-dismiss or any-key short-circuit). The default behaviour
	 * with no callback wired is to pop() the screen, so the class is
	 * fully driveable for visual smoke testing without a host.
	 */
	void setOnDismiss(ActionHandler cb);

	/** Replace the visible label of the right softkey (default "HOME"). */
	void setRightLabel(const char* label);

	/** Replace the caption above the avatar (default "CALL ENDED"). */
	void setCaption(const char* text);

	/** Replace the caller display name in place. nullptr -> "UNKNOWN". */
	void setCallerName(const char* name);

	/** Switch the avatar to a different seed without rebuilding. */
	void setAvatarSeed(uint8_t seed);

	/**
	 * Override the duration shown in the centre. Useful if the host
	 * computed the value out-of-band (e.g. from a LoRa packet timestamp)
	 * after the screen was already constructed.
	 */
	void setDurationSeconds(uint32_t seconds);

	/**
	 * Override the auto-dismiss delay, in milliseconds. Pass 0 to
	 * disable the auto-dismiss entirely (the screen then sits until a
	 * key press dismisses it). Must be set before onStart() to take
	 * effect for the first run; subsequent calls restart the timer.
	 */
	void setAutoDismissMs(uint32_t ms);

	const char* getCallerName()      const { return callerName; }
	uint32_t    getDurationSeconds() const { return durationSeconds; }
	uint8_t     getAvatarSeed()      const { return avatarSeed; }

	/** Cap so a runaway string cannot blow the per-screen allocation. */
	static constexpr uint8_t MaxNameLen = 24;

	/** Default auto-dismiss delay (1.5 s, per the S26 roadmap entry). */
	static constexpr uint32_t AutoDismissMs = 1500;

private:
	PhoneSynthwaveBg*  wallpaper;
	PhoneStatusBar*    statusBar;
	PhoneSoftKeyBar*   softKeys;
	PhonePixelAvatar*  avatar;

	lv_obj_t* captionLabel;     // "CALL ENDED", pixelbasic7 sunset orange
	lv_obj_t* nameLabel;        // caller name, pixelbasic7 warm cream
	lv_obj_t* durationLabel;    // "Xm Ys", pixelbasic16 warm cream
	lv_obj_t* hintLabel;        // "press any key", pixelbasic7 dim purple

	char callerName[MaxNameLen + 1];
	uint8_t  avatarSeed;
	uint32_t durationSeconds;

	uint32_t    autoDismissMs   = AutoDismissMs;
	lv_timer_t* dismissTimer    = nullptr;
	bool        dismissedAlready = false;  // guards against double-fire

	ActionHandler dismissCb = nullptr;

	void buildCaption();
	void buildAvatarBlock();
	void buildLabels();
	void refreshDurationLabel();
	void refreshNameLabel();

	void copyName(const char* src);

	void startDismissTimer();
	void stopDismissTimer();
	void fireDismiss();

	void buttonPressed(uint i) override;

	static void onDismissTimer(lv_timer_t* timer);
};

#endif // MAKERPHONE_PHONECALLENDED_H
