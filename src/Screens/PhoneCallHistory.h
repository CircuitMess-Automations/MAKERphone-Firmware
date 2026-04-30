#ifndef MAKERPHONE_PHONECALLHISTORY_H
#define MAKERPHONE_PHONECALLHISTORY_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include <vector>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneCallHistory
 *
 * The MAKERphone 2.0 call-log screen (S27). Fifth Phase-D screen and the
 * natural successor to PhoneCallEnded - once a call wraps up the user
 * lands back at PhoneHomeScreen, but they often want to see the recent
 * history (and one-press redial). This screen is the feature-phone
 * equivalent of that:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             CALL HISTORY               | <- pixelbasic7 cyan caption
 *   |   < ALEX KIM            12:42          | <- highlighted row (cursor)
 *   |   > MOM                 11:30          | <- outgoing (cream)
 *   |   x JOHN DOE            09:15          | <- missed (sunset orange)
 *   |   < +1 555 0123         YDAY           | <- incoming (cyan)
 *   |   > UNKNOWN             MON            | <- outgoing (cream)
 *   |                                        |
 *   |   CALL                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * S27 ships the screen *class* + a small set of hard-coded sample entries
 * so the screen is fully driveable today (S28 will start populating real
 * entries as LoRa calls arrive). The screen exposes a public addEntry()
 * API up-front so the eventual S28 wiring can push entries in without
 * subclassing - same forward-looking pattern PhoneIncomingCall /
 * PhoneActiveCall / PhoneCallEnded already use.
 *
 * Implementation notes:
 *  - Code-only - no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually part
 *    of the same MAKERphone family. Data partition cost stays zero.
 *  - 160x128 budget: 10 px status bar at top, 10 px caption strip just
 *    under it (y = 11..18), 10 px softkey bar at the bottom (y = 118),
 *    leaves 98 px (y = 22..118) for the list. Each row is ~13 px tall
 *    so up to 7 rows fit; a longer history is window-clipped with a
 *    scrolling cursor (the row strip slides as the cursor walks past
 *    the visible window's edges - same affordance the legacy
 *    PicMenu/MainMenu carousel already uses elsewhere in this codebase).
 *  - Each row paints its type indicator as a single ASCII glyph in a
 *    type-specific colour:
 *       Incoming -> "<" in cyan
 *       Outgoing -> ">" in warm cream
 *       Missed   -> "x" in sunset orange
 *    The glyph + name + timestamp are three children of a row container
 *    so each row is self-contained and easy to recolor on selection
 *    change. Glyph-only differentiation costs zero SPIFFS bytes and
 *    reads cleanly even on the 160-px display.
 *  - Cursor is a translucent purple highlight rectangle that slides
 *    behind the focused row. Using a single moving rect (rather than
 *    per-row styling) keeps the redraw cost low - one lv_obj position
 *    update per cursor move instead of touching every row's properties.
 *  - BTN_2 / BTN_LEFT (UP) and BTN_8 / BTN_RIGHT (DOWN) move the cursor.
 *    Wrap-around is on by default so a feature-phone user can flick
 *    around the list without thinking about edges.
 *  - BTN_ENTER ("CALL") fires the optional onCall callback with the
 *    selected entry; with no callback wired the softkey just flashes
 *    so the press is still felt. BTN_BACK ("BACK") fires the optional
 *    onBack callback, defaulting to pop().
 *  - Sample entries are seeded in the ctor so the screen reads as a
 *    real call log out of the box. clearEntries() + addEntry() let
 *    a future host (S28) replace them with the live LoRa-driven log.
 */
class PhoneCallHistory : public LVScreen, private InputListener {
public:
	enum class Type : uint8_t {
		Incoming = 0,
		Outgoing = 1,
		Missed   = 2,
	};

	struct Entry {
		Type        type;
		// Name is a fixed-size internal buffer; the constructor / addEntry
		// truncate to MaxNameLen so a runaway string can't blow the
		// per-screen allocation.
		char        name[24 + 1];
		// Timestamp label rendered on the right (e.g. "12:42", "YDAY",
		// "MON"). Caller decides the format - the screen just paints
		// the bytes verbatim. Capped at MaxTsLen for the same reason.
		char        timestamp[8 + 1];
		// Optional duration in whole seconds. 0 means "no duration"
		// (e.g. a missed call) and is hidden by the row layout. Currently
		// not displayed (the timestamp column carries the freshness cue),
		// but kept in the struct so a future "details" screen can pick
		// it up without changing the public API.
		uint32_t    durationSeconds = 0;
		// Avatar seed forwarded to PhonePixelAvatar wherever a peer's
		// row is later expanded. S27 itself does not paint avatars (the
		// list is too dense at 13 px / row), but storing the seed now
		// means S28 can push entries without losing the avatar identity.
		uint8_t     avatarSeed = 0;
	};

	using EntryHandler = void (*)(PhoneCallHistory* self, const Entry& entry);
	using BackHandler  = void (*)(PhoneCallHistory* self);

	PhoneCallHistory();
	virtual ~PhoneCallHistory() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Bind a callback fired when the user presses CALL (BTN_ENTER) on
	 * the focused row. With no callback wired the softkey just flashes
	 * so the press is felt - the screen stays driveable for visual
	 * testing without committing to S28's call dispatch design.
	 */
	void setOnCall(EntryHandler cb);

	/**
	 * Bind a callback fired on BTN_BACK. Default (when nullptr) is to
	 * pop() the screen so the user falls back to whatever was on the
	 * stack underneath (typically PhoneMainMenu).
	 */
	void setOnBack(BackHandler cb);

	/** Replace the visible label of the left softkey (default "CALL"). */
	void setLeftLabel(const char* label);

	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);

	/** Replace the caption above the list (default "CALL HISTORY"). */
	void setCaption(const char* text);

	/**
	 * Append an entry to the bottom of the list. The screen copies the
	 * caller-supplied strings into the internal Entry buffer so the
	 * caller doesn't have to keep them alive. Truncates name /
	 * timestamp to MaxNameLen / MaxTsLen. Re-paints the visible window.
	 *
	 * @return The flat index of the newly appended entry (or
	 *         entryCount()-1 if the oldest entry was dropped to keep
	 *         the log under MaxEntries).
	 */
	uint8_t addEntry(Type        type,
					 const char* name,
					 const char* timestamp,
					 uint32_t    durationSeconds = 0,
					 uint8_t     avatarSeed       = 0);

	/** Wipe every entry from the log and reset the cursor to 0. */
	void clearEntries();

	/** Number of entries currently held by the screen. */
	uint8_t entryCount() const { return (uint8_t) entries.size(); }

	/**
	 * Random-access fetch. Returns a const reference to the indexed
	 * entry; behaviour is undefined if `idx >= entryCount()`. Useful
	 * for hosts that want to introspect after a CALL callback fires.
	 */
	const Entry& getEntry(uint8_t idx) const { return entries[idx]; }

	/** Flat index of the currently focused row (0..entryCount()-1). */
	uint8_t getCursor() const { return cursor; }

	/**
	 * Move the cursor to a specific entry. No-op if idx is out of range
	 * (an empty list cannot be focused). Repaints the visible window
	 * and slides the highlight rect to the new row.
	 */
	void setCursor(uint8_t idx);

	/**
	 * S21-style press-feedback flash on the left/right softkey. Exposed
	 * so a host can flash from outside (e.g. when programmatically
	 * triggering a call during a transition).
	 */
	void flashLeftSoftKey();
	void flashRightSoftKey();

	/**
	 * Hard caps for the per-entry strings. Sized to fit the 160 px
	 * display: a name truncates with an ellipsis at MaxNameLen, the
	 * timestamp column is fixed-width and shorter labels left-pad with
	 * spaces. Both caps include the trailing NUL slot.
	 */
	static constexpr uint8_t MaxNameLen = 24;
	static constexpr uint8_t MaxTsLen   = 8;

	/** Maximum entries the log retains; oldest is dropped on overflow. */
	static constexpr uint8_t MaxEntries = 32;

	/** Number of rows visible in the list window at once. */
	static constexpr uint8_t VisibleRows = 7;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t*         captionLabel;
	lv_obj_t*         listContainer;   // host for the row strip + highlight
	lv_obj_t*         highlight;       // single moving cursor rect
	lv_obj_t*         emptyLabel;      // shown when the log has no entries

	// Per-row visuals. rowGlyphs[i] / rowNames[i] / rowTimes[i] are
	// VisibleRows-deep ring buffers; the on-screen `i` slot maps to the
	// (windowTop + i)-th entry. Updating windowTop rewrites the bytes
	// in-place rather than reallocating LVGL labels per cursor move.
	lv_obj_t*         rowGlyphs[VisibleRows];
	lv_obj_t*         rowNames[VisibleRows];
	lv_obj_t*         rowTimes[VisibleRows];

	std::vector<Entry> entries;
	uint8_t            cursor    = 0;
	uint8_t            windowTop = 0;

	EntryHandler callCb = nullptr;
	BackHandler  backCb = nullptr;

	void buildCaption();
	void buildListContainer();
	void buildRows();
	void buildEmptyLabel();

	void seedSampleEntries();

	/**
	 * Re-render the visible row strip from entries[windowTop..]. Hides
	 * empty rows beyond the end of the log. Cheap - touches up to
	 * VisibleRows lv_label_set_text calls.
	 */
	void refreshVisibleRows();

	/**
	 * Slide the highlight rect to (cursor - windowTop), or hide it if
	 * the log is empty. Adjusts windowTop first if cursor has walked
	 * outside the visible window.
	 */
	void refreshHighlight();

	/** Show "no recent calls" centre label when the log is empty. */
	void refreshEmptyState();

	/** Resolve a Type to its row-glyph string + colour. */
	static const char* glyphFor(Type t);
	static lv_color_t  colorFor(Type t);

	void copyName(Entry& e, const char* src);
	void copyTimestamp(Entry& e, const char* src);

	void moveCursorBy(int8_t delta);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONECALLHISTORY_H
