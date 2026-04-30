#ifndef MAKERPHONE_PHONECONTACTDETAIL_H
#define MAKERPHONE_PHONECONTACTDETAIL_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Types.hpp"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhonePixelAvatar;

/**
 * PhoneContactDetail
 *
 * MAKERphone 2.0 contact detail screen (S37). Second Phase-F screen,
 * pushed by PhoneContactsScreen (S36) when the user taps OPEN on a
 * row. The screen renders the focused contact in the standard retro
 * feature-phone "vCard" silhouette - 32x32 pixel avatar at the top,
 * display name in pixelbasic16 underneath, the LoRa peer id (the
 * MAKERphone "phone number") below that, and two action buttons at
 * the bottom for CALL / MESSAGE:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             CONTACT                    | <- pixelbasic7 cyan caption
 *   |             +----------+               |
 *   |             | (avatar) |               | <- PhonePixelAvatar 32x32
 *   |             +----------+               |
 *   |              ALEX KIM           *      | <- name + favorite mark
 *   |        +1A2B 3C4D 5E6F 7890            | <- LoRa peer id (hex)
 *   |        ----------------------          |
 *   |    [ CALL ]            [ MESSAGE ]     | <- action buttons
 *   |  CALL                          BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * S37 ships the screen *class* + wires it as the destination of the
 * S36 `setOnOpen` callback in IntroScreen.cpp. The class accepts the
 * S36 row payload directly (via its `Entry` struct) so a host that
 * already has the row data does not need to re-hit Storage. Constructor
 * convenience overloads also accept a raw uid (re-reading the live
 * PhoneContacts helpers) or an explicit name + uid + seed, so a future
 * call site (e.g. a contacts-search result, or PhoneCallHistory's
 * "show contact" affordance) can push the same screen without owning
 * a PhoneContactsScreen::Entry.
 *
 * Behaviour:
 *  - BTN_LEFT (CALL softkey) fires the CALL handler set via setOnCall.
 *    The default behaviour with no callback wired is a no-op flash so
 *    the screen is fully driveable in unit / visual tests. The
 *    IntroScreen.cpp wiring binds the default real-call dispatch.
 *  - BTN_RIGHT (BACK softkey) fires the BACK handler set via setOnBack,
 *    defaulting to pop().
 *  - BTN_2 / BTN_LEFT_ARROW move focus to the CALL action button;
 *    BTN_8 / BTN_RIGHT_ARROW move focus to the MESSAGE action button.
 *    BTN_ENTER fires whichever action button is focused. With no
 *    arrow-pressed yet the focus defaults to CALL so a single
 *    BTN_ENTER tap rings the contact - the muscle memory transfers
 *    cleanly from the softkey path.
 *  - The screen also listens for a long-press on BTN_BACK (>= 600 ms)
 *    to short-circuit out to the homescreen (pop chain), matching the
 *    convention every other Phase-D / Phase-F screen uses.
 *
 * Implementation notes:
 *  - Code-only - reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar / PhonePixelAvatar so the screen reads as part
 *    of the same MAKERphone family. Zero SPIFFS asset cost.
 *  - The peer id ("number") is rendered in 4-digit hex groups for
 *    legibility - "+1A2B 3C4D 5E6F 7890" rather than a 16-char hex
 *    blob. UIDs that happen to be 0 (sample / placeholder rows from
 *    S36's seed list) render as "(no number)" in dim purple so the
 *    user gets a visual cue rather than a bogus all-zero string.
 *  - The two action buttons are plain rounded lv_obj rectangles with a
 *    pixelbasic7 caption inside. Focused button gets the sunset orange
 *    border + cream caption; unfocused gets the dim purple border + dim
 *    purple caption. One label colour swap per focus change keeps the
 *    redraw cost minimal.
 */
class PhoneContactDetail : public LVScreen, private InputListener {
public:
	using ActionHandler = void (*)(PhoneContactDetail* self);

	/**
	 * Build a contact detail screen from explicit fields. Caller owns
	 * `name`; the screen copies it internally so the original may be
	 * freed afterwards. `name == nullptr` renders "UNKNOWN".
	 *
	 * @param uid         LoRa peer id (the MAKERphone "phone number").
	 * @param name        Display name shown under the avatar.
	 * @param avatarSeed  Seed for PhonePixelAvatar. Defaults to 0.
	 * @param favorite    Whether to render the "*" favorite mark.
	 */
	PhoneContactDetail(UID_t uid,
					   const char* name,
					   uint8_t avatarSeed = 0,
					   bool favorite = false);

	/**
	 * Build a contact detail screen from a known uid. Display name,
	 * avatar seed and favorite flag are read out of the live
	 * PhoneContacts helpers so a host that does not own a row payload
	 * can still push the screen without reaching into Storage itself.
	 */
	explicit PhoneContactDetail(UID_t uid);

	virtual ~PhoneContactDetail() override;

	void onStart() override;
	void onStop() override;

	/** Bind the CALL softkey / CALL action button. */
	void setOnCall(ActionHandler cb);

	/** Bind the MESSAGE action button. */
	void setOnMessage(ActionHandler cb);

	/** Bind the BACK softkey. Default: pop(). */
	void setOnBack(ActionHandler cb);

	/** Replace the visible label of the left softkey (default "CALL"). */
	void setLeftLabel(const char* label);
	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);
	/** Replace the caption above the avatar (default "CONTACT"). */
	void setCaption(const char* text);

	/** Read-only accessors so an action handler can route per-contact. */
	UID_t       getUid()         const { return uid; }
	const char* getName()        const { return name; }
	uint8_t     getAvatarSeed()  const { return avatarSeed; }
	bool        isFavorite()     const { return favorite; }

	void flashLeftSoftKey();
	void flashRightSoftKey();

	static constexpr uint8_t MaxNameLen = 24;

private:
	enum class Focus : uint8_t { Call = 0, Message = 1 };

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;
	PhonePixelAvatar* avatar;

	lv_obj_t* captionLabel;
	lv_obj_t* nameLabel;
	lv_obj_t* favLabel;
	lv_obj_t* numberLabel;
	lv_obj_t* divider;

	lv_obj_t* btnCall;
	lv_obj_t* btnMessage;
	lv_obj_t* btnCallLabel;
	lv_obj_t* btnMessageLabel;

	UID_t   uid          = 0;
	char    name[MaxNameLen + 1] = {0};
	uint8_t avatarSeed   = 0;
	bool    favorite     = false;

	Focus focus = Focus::Call;

	ActionHandler callCb    = nullptr;
	ActionHandler messageCb = nullptr;
	ActionHandler backCb    = nullptr;

	// Suppress the BTN_BACK short-press handling once a long-press has
	// already fired pop-to-home on the same hold cycle.
	bool backHoldFired = false;

	void buildLayout();
	void buildAvatarBlock();
	void buildLabels();
	void buildActionButtons();

	void copyName(const char* src);
	void formatPeerId(char* out, size_t outLen) const;

	void refreshFocus();
	void invokeFocusedAction();

	void buttonPressed(uint i) override;
	void buttonHeld(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONECONTACTDETAIL_H
