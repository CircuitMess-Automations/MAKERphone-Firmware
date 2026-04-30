#ifndef MAKERPHONE_PHONEINCOMINGCALL_H
#define MAKERPHONE_PHONEINCOMINGCALL_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhonePixelAvatar;

/**
 * PhoneIncomingCall
 *
 * The MAKERphone 2.0 "someone is calling you" screen (S24). First Phase-D
 * call screen and the visual partner of the dialer that S23 just shipped.
 * Composes the existing Phase-A widgets (PhoneSynthwaveBg, PhoneStatusBar,
 * PhoneSoftKeyBar, PhonePixelAvatar) into the unmistakable Sony-Ericsson
 * incoming-call silhouette:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |                                        |
 *   |            INCOMING CALL               | <- pixelbasic7 caption
 *   |                                        |
 *   |             +----------+               |
 *   |             | (avatar) |               | <- PhonePixelAvatar 32x32
 *   |             +----------+               |    + pulsing ring
 *   |                                        |
 *   |              ALEX KIM                  | <- pixelbasic16 caller name
 *   |             +1 555 0123                | <- pixelbasic7 number
 *   |                                        |
 *   |  ANSWER                          REJECT| <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * S24 ships the screen *class* itself; the host (S28 will wire it into
 * LoRaService) is responsible for constructing it with a caller name,
 * number and avatar seed and pushing it on top of whatever screen was
 * active. ANSWER (BTN_LEFT or BTN_ENTER) and REJECT (BTN_RIGHT or
 * BTN_BACK) both fire user-supplied callbacks; with no callback wired
 * the screen just pops itself, so the class is fully driveable today
 * without committing to a call-flow design (PhoneActiveCall is S25).
 *
 * Implementation notes:
 *  - Code-only - no SPIFFS assets. Reuses PhonePixelAvatar (S11) verbatim
 *    plus the standard top/bottom bars. Data partition cost stays zero.
 *  - 160x128 budget: 10 px status bar at top, 10 px softkey bar at the
 *    bottom, "INCOMING CALL" caption sitting just under the status bar,
 *    a 32x32 avatar with a 4 px pulsing ring centred horizontally, the
 *    caller name in pixelbasic16 directly below, and the number in
 *    pixelbasic7 underneath. All vertical positions are pinned with
 *    LV_ALIGN_TOP_MID + explicit Y offsets so a long caller name does
 *    not push the layout around (the label uses LABEL_LONG_DOT, which
 *    truncates with an ellipsis at the 140 px cap).
 *  - The pulsing ring is a single rounded lv_obj rectangle (square,
 *    radius LV_RADIUS_CIRCLE) with a transparent fill and an animated
 *    border opacity + scale ping-pong. Same animation pattern that
 *    PhoneIconTile uses for its halo pulse; the period is set to a
 *    longer 900 ms here so the ring reads as a slow phone-ringing
 *    breath rather than the snappier menu-tile selection halo.
 *  - ANSWER / REJECT are wired identically to the dialer's CALL / BACK
 *    soft-keys, using the same setButtonHoldTime + flash-on-press
 *    pattern, so the muscle memory transfers between Phase-D screens
 *    without surprise.
 *  - The screen is constructed with copies of caller name / number so
 *    a host that allocates these on the stack (or in a temporary
 *    buffer) does not need to keep them alive for the screen lifetime.
 *    Both fields are capped at MaxNameLen / MaxNumberLen characters
 *    to bound the per-screen allocation.
 */
class PhoneIncomingCall : public LVScreen, private InputListener {
public:
	using ActionHandler = void (*)(PhoneIncomingCall* self);

	/**
	 * Build an incoming-call screen for a known peer.
	 *
	 * @param callerName  Display name shown in the centre. Caller owns
	 *                    the string; the screen copies it internally,
	 *                    so the original may be freed afterwards. May
	 *                    be nullptr (renders "UNKNOWN").
	 * @param callerNumber Optional secondary line shown under the name
	 *                    (the LoRa peer id, dialled number, etc.). May
	 *                    be nullptr - the line is hidden in that case.
	 * @param avatarSeed  Seed forwarded to PhonePixelAvatar so the
	 *                    avatar visually matches whatever the contacts
	 *                    list / messaging shows for this peer.
	 */
	PhoneIncomingCall(const char* callerName,
					  const char* callerNumber = nullptr,
					  uint8_t avatarSeed = 0);
	virtual ~PhoneIncomingCall() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Bind a callback for the ANSWER softkey (BTN_LEFT / BTN_ENTER). The
	 * default behaviour with no callback wired is to pop() the screen,
	 * so a host that just wants the visual without a real call simply
	 * leaves this nullptr. A future S25 will wire this through to push
	 * a PhoneActiveCall screen with the caller info.
	 */
	void setOnAnswer(ActionHandler cb);

	/**
	 * Bind a callback for the REJECT softkey (BTN_RIGHT / BTN_BACK).
	 * Default behaviour with no callback wired is to pop() the screen
	 * so the user falls back to whatever was on screen before the call.
	 */
	void setOnReject(ActionHandler cb);

	/** Replace the visible label of the left softkey (default "ANSWER"). */
	void setLeftLabel(const char* label);

	/** Replace the visible label of the right softkey (default "REJECT"). */
	void setRightLabel(const char* label);

	/** Replace the caption above the avatar (default "INCOMING CALL"). */
	void setCaption(const char* text);

	/** Replace the caller display name in place. nullptr -> "UNKNOWN". */
	void setCallerName(const char* name);

	/** Replace the caller number in place. nullptr or "" hides the line. */
	void setCallerNumber(const char* number);

	/** Switch the avatar to a different seed without rebuilding. */
	void setAvatarSeed(uint8_t seed);

	const char* getCallerName()   const { return callerName; }
	const char* getCallerNumber() const { return callerNumber; }
	uint8_t     getAvatarSeed()   const { return avatarSeed; }

	/** Caps so a runaway string cannot blow the per-screen allocation. */
	static constexpr uint8_t MaxNameLen   = 24;
	static constexpr uint8_t MaxNumberLen = 24;

private:
	PhoneSynthwaveBg*  wallpaper;
	PhoneStatusBar*    statusBar;
	PhoneSoftKeyBar*   softKeys;
	PhonePixelAvatar*  avatar;

	lv_obj_t* captionLabel;   // "INCOMING CALL", pixelbasic7
	lv_obj_t* nameLabel;      // caller name, pixelbasic16
	lv_obj_t* numberLabel;    // caller number, pixelbasic7
	lv_obj_t* ring;           // pulsing ring around the avatar (LVGL owns the anim)

	char callerName[MaxNameLen + 1];
	char callerNumber[MaxNumberLen + 1];
	uint8_t avatarSeed;

	ActionHandler answerCb = nullptr;
	ActionHandler rejectCb = nullptr;

	// Long-press-suppression flags: matches the pattern used by
	// PhoneHomeScreen / PhoneMainMenu / PhoneDialerScreen so the
	// short/long-press paths do not double-fire on key release.
	bool answerLongFired = false;
	bool rejectLongFired = false;

	void buildCaption();
	void buildAvatarBlock();
	void buildLabels();
	void startRingAnimation();

	void copyName(const char* src);
	void copyNumber(const char* src);
	void refreshNameLabel();
	void refreshNumberLabel();

	void fireAnswer();
	void fireReject();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEINCOMINGCALL_H
