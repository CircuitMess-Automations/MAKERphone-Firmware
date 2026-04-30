#ifndef MAKERPHONE_PHONEACTIVECALL_H
#define MAKERPHONE_PHONEACTIVECALL_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhonePixelAvatar;

/**
 * PhoneActiveCall
 *
 * The MAKERphone 2.0 "you are on a call right now" screen (S25). Second
 * Phase-D screen, the natural successor to PhoneIncomingCall (S24) once
 * the user has hit ANSWER, and the eventual destination of the dialer's
 * "CALL" softkey too. Composes the same Phase-A widgets the rest of the
 * call-flow uses (PhoneSynthwaveBg, PhoneStatusBar, PhoneSoftKeyBar,
 * PhonePixelAvatar) so the visual language stays uniform across the
 * incoming -> active -> ended sequence:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |               IN CALL                  | <- pixelbasic7 caption
 *   |             +----------+               |
 *   |             | (avatar) |               | <- PhonePixelAvatar 32x32
 *   |             +----------+               |
 *   |              ALEX KIM                  | <- pixelbasic7 caller name
 *   |               00:42                    | <- pixelbasic16 mm:ss timer
 *   |                                        |
 *   |               * MUTED *                | <- pixelbasic7, only when muted
 *   |  MUTE                              END | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * S25 ships the screen *class* itself; the host (S26 will wire a
 * PhoneCallEnded overlay, S28 the LoRa-backed peer side) is responsible
 * for constructing it after ANSWER fires and pushing it on top of
 * whatever screen was active. The MUTE softkey toggles the mute state
 * (label flips MUTE <-> UNMUTE, and the on-screen indicator appears /
 * disappears) and notifies a user-supplied callback so the audio path
 * can react. The END softkey fires the user-supplied end callback (or
 * pop()s the screen if no callback is wired) so the class is fully
 * driveable today without committing to a call-end design.
 *
 * Implementation notes:
 *  - Code-only - no SPIFFS assets. Reuses PhonePixelAvatar (S11) verbatim
 *    plus the standard top/bottom bars. Data partition cost stays zero.
 *  - 160x128 budget: 10 px status bar at top, 10 px softkey bar at
 *    bottom. "IN CALL" caption sits just under the status bar in
 *    pixelbasic7 cyan (cyan rather than the incoming screen's sunset
 *    orange, to read as "calm / connected" instead of "alert"). The
 *    32x32 avatar centred horizontally below it, the caller name
 *    underneath in pixelbasic7 warm cream (smaller than the incoming
 *    screen's pixelbasic16 because the focal element here is the
 *    timer, not the name). The timer itself sits below the name in
 *    pixelbasic16 warm cream - the only large glyph on the screen,
 *    so the eye locks onto it. A "* MUTED *" indicator appears below
 *    the timer in pixelbasic7 sunset orange when the user toggles
 *    mute, otherwise it is hidden so the layout reads cleanly.
 *  - The call timer ticks via a 250 ms lv_timer (faster than 1 Hz so
 *    a freshly-pushed screen updates its first second visibly within
 *    a quarter-second of construction). The label is only redrawn
 *    when the integer second changes, so the work is bounded and the
 *    LVGL refresh budget is not strained.
 *  - MUTE / END are wired identically to the dialer's CALL / BACK
 *    soft-keys, using the same setButtonHoldTime + flash-on-press
 *    pattern, so the muscle memory transfers between Phase-D screens.
 *    BTN_BACK is also wired to END so the standard hardware "get me
 *    out" gesture hangs the call up just like the softkey does, and
 *    BTN_ENTER (the centre A button) toggles mute as a friendly
 *    second confirm path - matches the way PhoneIncomingCall accepts
 *    A as a second ANSWER button.
 *  - Caller info is copied into a fixed-size internal buffer (same
 *    MaxNameLen / MaxNumberLen caps PhoneIncomingCall uses) so a host
 *    that allocates these on the stack does not need to keep them
 *    alive for the screen lifetime.
 *  - The screen does NOT take ownership of any audio path. setOnMute
 *    is purely a notification callback - what mute *means* (mic gain,
 *    LoRa packet flag, whatever) is the host's call.
 */
class PhoneActiveCall : public LVScreen, private InputListener {
public:
	using ActionHandler = void (*)(PhoneActiveCall* self);
	using MuteHandler   = void (*)(PhoneActiveCall* self, bool muted);

	/**
	 * Build an active-call screen for a known peer. The call timer
	 * starts counting from onStart() so a host that constructs the
	 * screen ahead of time still gets an honest mm:ss readout from
	 * the moment the user actually sees the screen.
	 *
	 * @param callerName   Display name shown above the timer. The
	 *                     screen copies this internally; the original
	 *                     may be freed afterwards. May be nullptr
	 *                     (renders "UNKNOWN").
	 * @param callerNumber Reserved for future use (S35 contacts wiring
	 *                     will surface the dialled number alongside
	 *                     the contact name). Stored on the screen so
	 *                     a host can read it back via getCallerNumber()
	 *                     but not currently rendered to keep the
	 *                     active-call layout calm.
	 * @param avatarSeed   Seed forwarded to PhonePixelAvatar so the
	 *                     avatar matches what the contacts list /
	 *                     incoming-call screen showed for this peer.
	 */
	PhoneActiveCall(const char* callerName,
					const char* callerNumber = nullptr,
					uint8_t avatarSeed = 0);
	virtual ~PhoneActiveCall() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Bind a callback for the MUTE softkey (BTN_LEFT / BTN_ENTER). The
	 * callback receives the *new* mute state so a host can react with
	 * a single branch. With no callback wired the screen still toggles
	 * its own mute state and updates the visual indicator, so the
	 * widget is fully driveable for visual smoke testing.
	 */
	void setOnMute(MuteHandler cb);

	/**
	 * Bind a callback for the END softkey (BTN_RIGHT / BTN_BACK). The
	 * default (when nullptr) is to pop() the screen so a host that
	 * just wanted the visual still gets sensible behaviour.
	 */
	void setOnEnd(ActionHandler cb);

	/** Replace the visible label of the left softkey (default "MUTE"/"UNMUTE"). */
	void setLeftLabel(const char* label);

	/** Replace the visible label of the right softkey (default "END"). */
	void setRightLabel(const char* label);

	/** Replace the caption above the avatar (default "IN CALL"). */
	void setCaption(const char* text);

	/** Replace the caller display name in place. nullptr -> "UNKNOWN". */
	void setCallerName(const char* name);

	/** Replace the caller number in place. nullptr or "" clears it. */
	void setCallerNumber(const char* number);

	/** Switch the avatar to a different seed without rebuilding. */
	void setAvatarSeed(uint8_t seed);

	/** Programmatically set the mute state. Updates the label + indicator. */
	void setMuted(bool muted);

	bool isMuted() const { return muted; }

	/**
	 * Number of seconds the call has been active. Driven by millis()
	 * captured at onStart(); useful for the future S26 PhoneCallEnded
	 * overlay which wants to display the call duration after we
	 * pop().
	 */
	uint32_t getDurationSeconds() const;

	const char* getCallerName()   const { return callerName; }
	const char* getCallerNumber() const { return callerNumber; }
	uint8_t     getAvatarSeed()   const { return avatarSeed; }

	/** Caps so a runaway string cannot blow the per-screen allocation. */
	static constexpr uint8_t MaxNameLen   = 24;
	static constexpr uint8_t MaxNumberLen = 24;

	/** Refresh cadence for the timer label, ms. */
	static constexpr uint32_t TimerTickMs = 250;

private:
	PhoneSynthwaveBg*  wallpaper;
	PhoneStatusBar*    statusBar;
	PhoneSoftKeyBar*   softKeys;
	PhonePixelAvatar*  avatar;

	lv_obj_t* captionLabel;     // "IN CALL", pixelbasic7
	lv_obj_t* nameLabel;        // caller name, pixelbasic7
	lv_obj_t* timerLabel;       // mm:ss timer, pixelbasic16
	lv_obj_t* mutedLabel;       // "* MUTED *", pixelbasic7 (hidden by default)

	char callerName[MaxNameLen + 1];
	char callerNumber[MaxNumberLen + 1];
	uint8_t avatarSeed;

	uint32_t startMs    = 0;
	uint32_t lastSecond = 0xFFFFFFFFu;  // forces a redraw on first tick
	lv_timer_t* tickTimer = nullptr;

	bool muted = false;

	ActionHandler endCb  = nullptr;
	MuteHandler   muteCb = nullptr;

	// Long-press-suppression flags - same pattern PhoneIncomingCall and
	// PhoneDialerScreen use so the short/long-press paths do not
	// double-fire on key release.
	bool muteLongFired = false;
	bool endLongFired  = false;

	void buildCaption();
	void buildAvatarBlock();
	void buildLabels();
	void startTimerTick();
	void stopTimerTick();
	void refreshTimerLabel();
	void refreshMuteVisuals();

	void copyName(const char* src);
	void copyNumber(const char* src);
	void refreshNameLabel();

	void fireMuteToggle();
	void fireEnd();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;

	static void onTickTimer(lv_timer_t* timer);
};

#endif // MAKERPHONE_PHONEACTIVECALL_H
