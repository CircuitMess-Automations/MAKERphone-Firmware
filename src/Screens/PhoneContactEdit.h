#ifndef MAKERPHONE_PHONECONTACTEDIT_H
#define MAKERPHONE_PHONECONTACTEDIT_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Types.hpp"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhonePixelAvatar;
class PhoneT9Input;

/**
 * PhoneContactEdit
 *
 * MAKERphone 2.0 contact-edit screen (S38). Third Phase-F screen, the
 * companion of PhoneContactsScreen (S36) + PhoneContactDetail (S37).
 * Lets the user customise a phone-book entry's name + avatar in a
 * Sony-Ericsson-style two-section editor:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |  +----------------------------------+ |
 *   |  | Alex|                           | | <- T9 name field (S32)
 *   |  | abc                          Abc| |
 *   |  +----------------------------------+ |
 *   |          AVATAR  (focus dot)           | <- section header
 *   |  [P] [P] [P] [P]                       | <- 4x2 PhonePixelAvatar grid
 *   |  [P] [P] [P] [P]                       |   (selected slot gets the
 *   |                                        |    sunset orange border)
 *   |  SAVE                          BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The screen is dual-focus:
 *  - NAME focus (default): every numeric press feeds the T9 input,
 *    BTN_L = backspace, BTN_R = case toggle - the same chord every
 *    other MAKERphone T9 host wires (`ConvoScreen`, dialer's '*' / '#').
 *  - AVATAR focus: BTN_L moves the selection to the previous of the
 *    eight grid slots, BTN_R moves it to the next one (with wrap).
 *    The currently selected avatar is rendered with `setSelected(true)`
 *    so it gets the standard sunset-orange highlight border.
 *
 * BTN_ENTER toggles between the two focus zones, and the active zone
 * paints a cyan accent on its section header so the user always sees
 * what their next BTN_L / BTN_R will do. The BTN_LEFT softkey is bound
 * to SAVE (it persists the edits via the PhoneContacts helpers and
 * fires the registered onSave callback), and BTN_RIGHT is the BACK
 * softkey - short-press pops, long-press bails to the homescreen, the
 * convention every other Phase-D / Phase-F screen uses.
 *
 * Avatar picker
 *  - The picker exposes eight `PhonePixelAvatar` slots seeded from a
 *    fixed table (`kAvatarSeeds`) chosen so the resulting designs are
 *    visually distinct. If the contact's existing seed isn't already
 *    in the table, slot 0's seed is replaced by it - so opening the
 *    editor never silently loses the user's previously chosen avatar.
 *  - The avatars are stamped out as 32x32 widgets in a 4x2 grid that
 *    fits inside the 160 px width with 2 px gutters. No SPIFFS assets,
 *    purely code-only - same family as every other Phone* widget.
 *
 * Persistence
 *  - When the SAVE softkey fires, the screen calls into
 *    `PhoneContacts::setDisplayName` / `setAvatarSeed` for the bound
 *    UID (skipped when uid == 0 - placeholder rows from the S36
 *    fallback list have no real persistence target). After the writes
 *    land, the registered onSave callback runs so a host can pop / do
 *    a follow-up nav. The default behaviour with no callback wired is
 *    a flash + pop().
 *
 * Implementation notes
 *  - 100 % code-only. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar / PhonePixelAvatar / PhoneT9Input so the screen
 *    reads as part of the same MAKERphone family.
 *  - The screen owns its T9 input and the eight avatar widgets; their
 *    backing lv_obj's are children of `obj`, so LVScreen's destructor
 *    walks the tree and frees them automatically.
 *  - Long-press on BTN_BACK (>= 600 ms) short-circuits to the home -
 *    same convention every Phase-D / Phase-F screen uses, so muscle
 *    memory transfers cleanly.
 */
class PhoneContactEdit : public LVScreen, private InputListener {
public:
	using ActionHandler = void (*)(PhoneContactEdit* self);
	using SaveHandler   = void (*)(PhoneContactEdit* self,
									const char* newName,
									uint8_t newAvatarSeed);

	/**
	 * Build an editor for the given uid. The name + avatar seed are
	 * read out of the live PhoneContacts helpers so a host that does
	 * not own a row payload can still push the screen.
	 */
	explicit PhoneContactEdit(UID_t uid);

	/**
	 * Build an editor with explicit fields. Caller owns `name`; the
	 * screen copies it internally so the original may be freed
	 * afterwards. `name == nullptr` initialises the field empty.
	 */
	PhoneContactEdit(UID_t uid,
					 const char* name,
					 uint8_t avatarSeed);

	virtual ~PhoneContactEdit() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Bind the SAVE softkey. Default behaviour with no callback wired
	 * is to persist the edits via PhoneContacts (when uid != 0) and
	 * pop the screen.
	 */
	void setOnSave(SaveHandler cb);

	/** Bind the BACK softkey. Default: pop() without persisting. */
	void setOnBack(ActionHandler cb);

	/** Replace the visible label of the left softkey (default "SAVE"). */
	void setLeftLabel(const char* label);
	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);
	/** Replace the caption above the form (default "EDIT CONTACT"). */
	void setCaption(const char* text);

	/** Read-only accessors so a save handler can route per-contact. */
	UID_t       getUid()         const { return uid; }
	uint8_t     getCurrentSeed() const;
	/**
	 * Snapshot the current T9 buffer into the supplied output buffer.
	 * `outLen` must be > 0; the result is always nul-terminated. Safer
	 * than a `const char*` accessor because PhoneT9Input::getText()
	 * returns the buffer by value, so a c_str() on the returned
	 * temporary would dangle the moment the expression ends.
	 */
	void copyCurrentName(char* out, size_t outLen) const;

	void flashLeftSoftKey();
	void flashRightSoftKey();

	static constexpr uint8_t AvatarCount = 8;
	static constexpr uint8_t MaxNameLen  = 23; // matches PhoneContact::displayName

private:
	enum class Focus : uint8_t {
		Name   = 0,
		Avatar = 1,
	};

	PhoneSynthwaveBg* wallpaper     = nullptr;
	PhoneStatusBar*   statusBar     = nullptr;
	PhoneSoftKeyBar*  softKeys      = nullptr;
	PhoneT9Input*     nameInput     = nullptr;

	lv_obj_t* captionLabel     = nullptr;
	lv_obj_t* avatarSectionHdr = nullptr;

	PhonePixelAvatar* avatars[AvatarCount] = {nullptr};
	uint8_t           seeds[AvatarCount]   = {0};

	UID_t   uid            = 0;
	uint8_t selectedAvatar = 0;

	Focus focus = Focus::Name;

	SaveHandler   saveCb = nullptr;
	ActionHandler backCb = nullptr;

	// Suppress short-press BTN_BACK once a long-press has already
	// fired pop-to-home on the same hold cycle.
	bool backHoldFired = false;

	void initSeedsFrom(uint8_t initialSeed);

	void buildLayout();
	void buildHeaders();
	void buildNameInput(const char* initialName);
	void buildAvatarGrid();

	void refreshFocus();
	void refreshAvatarSelection();

	void cycleAvatar(int8_t direction);
	void invokeSave();

	void buttonPressed(uint i) override;
	void buttonHeld(uint i) override;
	void buttonReleased(uint i) override;

	// Eight curated PhonePixelAvatar seeds chosen so the resulting
	// designs read as visually distinct on a 160x128 display. Slot 0
	// is replaced by the contact's existing seed at construction
	// time when the existing seed is not already in this list.
	static const uint8_t kAvatarSeeds[AvatarCount];
};

#endif // MAKERPHONE_PHONECONTACTEDIT_H
