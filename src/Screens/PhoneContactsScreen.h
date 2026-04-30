#ifndef MAKERPHONE_PHONECONTACTSSCREEN_H
#define MAKERPHONE_PHONECONTACTSSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include <vector>
#include "../Interface/LVScreen.h"
#include "../Types.hpp"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneContactsScreen
 *
 * MAKERphone 2.0 phone-book screen (S36). First Phase-F screen, built on
 * top of the S35 PhoneContacts data model + the existing Friends repo.
 * The user navigates an alphabetised list of paired peers - same data
 * model the legacy FriendsScreen exposes - rendered with a feature-phone
 * row layout and an A-Z scroll-strip on the right for fast letter jumps.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar  (10 px)
 *   |             CONTACTS                   | <- pixelbasic7 cyan caption
 *   |  [A] ALEX KIM                       A  |
 *   |  [A] ALICE                          B  |
 *   |  [B] BRIAN COOPER                  *C  | <- letter scrubber (right)
 *   |  [C] CARL    *                      D  | <- "*" = favorite
 *   |  [D] DAD                            .  |
 *   |  [E] ELLA                           .  |
 *   |                                     Z  |
 *   |  OPEN                          BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - Up/Down (BTN_2 / BTN_LEFT and BTN_8 / BTN_RIGHT) move the cursor
 *    a single row at a time, wrapping at the ends. The visible window
 *    auto-scrolls so the cursor stays on screen, mirroring the
 *    PhoneCallHistory pattern in S27.
 *  - L / R bumpers (BTN_L / BTN_R) jump the cursor to the first contact
 *    of the previous / next letter bucket. With < 1 contact per letter
 *    the jump is identical to a single-row move; with many contacts in
 *    a bucket the jump skips them entirely. This is the affordance the
 *    A-Z scrubber visualises.
 *  - BTN_ENTER fires the optional onOpen callback; the host typically
 *    pushes a PhoneContactDetail screen (S37). With no callback wired
 *    the softkey just flashes so the press is felt.
 *  - BTN_BACK fires the optional onBack callback or pop()s.
 *
 * Population:
 *  - The constructor walks Storage.Friends.all() and seeds one entry per
 *    paired peer (skipping the device's own efuse-mac id, the same
 *    convention FriendsScreen uses). Display names + avatar seeds come
 *    from the S35 PhoneContacts helpers, so a user-edited display name
 *    transparently overrides the broadcast nickname.
 *  - A `clearEntries()` + `addEntry()` API is exposed up-front so a
 *    future caller (S37 returning after edit / sync from a peer pairing)
 *    can repopulate without subclassing. Same pattern PhoneCallHistory
 *    uses.
 *  - When no friends exist yet, the screen falls back to a small set of
 *    representative sample entries (paired with `0` UIDs) so the UI is
 *    visually testable without a paired device. addEntry() makes no
 *    assumption about UID validity; an opener that receives uid==0 is
 *    expected to no-op.
 *  - Sort order: favorites first (PhoneContacts::isFavorite), then a
 *    case-insensitive lexicographic sort on the display name. Stable
 *    enough that the same user state always produces the same row
 *    order across reboots.
 *
 * Implementation notes:
 *  - Code-only - reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen feels visually part of the same
 *    MAKERphone family. Zero SPIFFS asset cost. Each row is built once
 *    and re-bound by overwriting label text on cursor / window scroll,
 *    same pattern PhoneCallHistory introduced.
 *  - Per-row badge is a 10x10 coloured square with the first letter
 *    centered inside it. The square hue is derived from the contact's
 *    avatar seed via a small hue table so two adjacent rows rarely
 *    share a colour. We deliberately do not paint a 32x32 PhonePixelAvatar
 *    in the row - the 12 px row height cannot host it without overflow,
 *    and the letter badge reads cleaner at this density.
 *  - A-Z strip is a single moving glyph + thin tick column on the right
 *    edge. The glyph (pixelbasic7) shows the current cursor's letter
 *    bucket; tick marks above/below trace the alphabet so the user gets
 *    a positional cue without having to fit 26 readable letters in 96
 *    pixels. L / R bumpers jump between buckets.
 */
class PhoneContactsScreen : public LVScreen, private InputListener {
public:
	struct Entry {
		UID_t   uid       = 0;
		// Display name copied from PhoneContacts::displayNameOf at
		// construction; `MaxNameLen + 1` bytes to leave room for the
		// terminator. We never re-fetch the name on every paint - the
		// row strip just looks at this buffer.
		char    name[24 + 1] = {0};
		// Avatar seed forwarded to a future detail screen / hue lookup.
		uint8_t avatarSeed = 0;
		// Favorite flag (drives sort order + the "*" badge in the row).
		uint8_t favorite   = 0;
	};

	using EntryHandler = void (*)(PhoneContactsScreen* self, const Entry& entry);
	using BackHandler  = void (*)(PhoneContactsScreen* self);

	PhoneContactsScreen();
	virtual ~PhoneContactsScreen() override;

	void onStart() override;
	void onStop() override;

	/** Bind the BTN_ENTER (OPEN) handler. Default fires nothing. */
	void setOnOpen(EntryHandler cb);
	/** Bind the BTN_BACK handler. Default pops the screen. */
	void setOnBack(BackHandler cb);

	/** Replace the visible label of the left softkey (default "OPEN"). */
	void setLeftLabel(const char* label);
	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);
	/** Replace the caption above the list (default "CONTACTS"). */
	void setCaption(const char* text);

	/**
	 * Append an entry. Truncates the name to MaxNameLen. Re-paints the
	 * visible window. Caller-supplied strings are copied so the caller
	 * doesn't have to keep them alive.
	 */
	uint8_t addEntry(UID_t uid,
					 const char* name,
					 uint8_t avatarSeed = 0,
					 bool favorite = false);

	/** Wipe every entry from the list and reset the cursor to 0. */
	void clearEntries();

	/**
	 * Re-sort the list (favorites first, then case-insensitive name).
	 * Called automatically after each addEntry. Public so a host that
	 * mutates entries directly via getEntry() / setCursor() can request
	 * a re-sort.
	 */
	void sortEntries();

	uint8_t entryCount() const { return (uint8_t) entries.size(); }
	const Entry& getEntry(uint8_t idx) const { return entries[idx]; }

	uint8_t getCursor() const { return cursor; }
	void setCursor(uint8_t idx);

	void flashLeftSoftKey();
	void flashRightSoftKey();

	static constexpr uint8_t MaxNameLen  = 24;
	static constexpr uint8_t MaxEntries  = 32;
	static constexpr uint8_t VisibleRows = 8;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;
	lv_obj_t* listContainer;     // hosts row strip + highlight
	lv_obj_t* highlight;         // single moving cursor rect
	lv_obj_t* emptyLabel;        // shown when entries.empty()

	// Per-row visuals. VisibleRows-deep ring buffers; on-screen slot `i`
	// always paints entries[windowTop + i] and re-binds via label text.
	lv_obj_t* rowBadgeBg[VisibleRows];
	lv_obj_t* rowBadgeLetter[VisibleRows];
	lv_obj_t* rowName[VisibleRows];
	lv_obj_t* rowFav[VisibleRows];   // tiny "*" badge for favorites

	// A-Z scrubber children (right column, 10 px wide).
	lv_obj_t* stripBar;
	lv_obj_t* stripLetter;
	lv_obj_t* stripThumb;

	std::vector<Entry> entries;
	uint8_t            cursor    = 0;
	uint8_t            windowTop = 0;

	EntryHandler openCb = nullptr;
	BackHandler  backCb = nullptr;

	void buildCaption();
	void buildListContainer();
	void buildRows();
	void buildAZStrip();
	void buildEmptyLabel();

	void seedFromStorageOrSamples();

	void refreshVisibleRows();
	void refreshHighlight();
	void refreshAZStrip();
	void refreshEmptyState();

	void moveCursorBy(int8_t delta);
	void jumpLetter(int8_t direction);

	static char firstLetterOf(const char* name);
	static int  cmpName(const Entry& a, const Entry& b);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONECONTACTSSCREEN_H
