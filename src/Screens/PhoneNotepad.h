#ifndef MAKERPHONE_PHONENOTEPAD_H
#define MAKERPHONE_PHONENOTEPAD_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneT9Input;

/**
 * PhoneNotepad
 *
 * Phase-L utility app (S64): a Sony-Ericsson-style note-taking app.
 * Slots in next to PhoneCalculator (S60), PhoneStopwatch (S61),
 * PhoneTimer (S62) and PhoneCalendar (S63) inside the eventual
 * utility-apps grid (S65). Same retro silhouette every other Phone*
 * screen wears:
 *
 *   List view (the entry mode)
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |               NOTES   2/4              | <- pixelbasic7 caption
 *   |  -------------------------------------- |
 *   |   1. Buy milk and bread                 |
 *   |  >2. Pick up Sara from school<          | <- cursor row (cyan accent)
 *   |   3. Call dentist                       |
 *   |   .  (empty)                            |
 *   |  -------------------------------------- |
 *   |  NEW                            OPEN    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 *   Edit view (after NEW or OPEN)
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |              NOTE 2/4                  | <- caption + slot index
 *   |  +----------------------------------+ |
 *   |  | Pick up Sara                     | | <- PhoneT9Input (S32)
 *   |  | abc                          Abc | |
 *   |  +----------------------------------+ |
 *   |  3 of 120 chars                        | <- char counter / hint
 *   |  SAVE                          BACK    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Two view modes:
 *
 *   List
 *     - Up to MaxNotes saved notes; each row shows the slot index +
 *       a 1-line preview (the first line of the note's text, hard-
 *       truncated at PreviewChars). The cursor row is rendered in
 *       MP_HIGHLIGHT cyan with leading + trailing chevrons; idle
 *       rows are MP_TEXT cream. Empty slots render in MP_LABEL_DIM
 *       as "(empty)" so the user can see at a glance how many
 *       notes are stored.
 *
 *   Edit
 *     - Wraps a PhoneT9Input (S32) so the user can multi-tap a note
 *       in the same way they compose SMS in ConvoScreen (S33). The
 *       composer is bound to whichever slot the user chose -- "NEW"
 *       picks the first empty slot (or, if all slots are full,
 *       overwrites the cursor row), "OPEN" prefills the input with
 *       the current row's saved text. Saving back-writes the slot
 *       and returns to List view; BACK discards the in-flight edit.
 *
 * Persistence
 *   - The notes live in an in-memory array on the screen instance
 *     (RAM-only). Same lifetime model PhoneStopwatch (S61) and
 *     PhoneTimer (S62) use for their state -- the data outlives a
 *     navigation away from the screen for as long as the screen
 *     instance itself is alive (which, in the current shell, is the
 *     duration of the boot session). A future session can promote
 *     this to disk via the existing Storage layer without changing
 *     this header's API.
 *
 * Controls (List view):
 *   - BTN_2 / BTN_8                       : cursor up / down (wraps).
 *   - BTN_L (bumper)                      : same as BTN_2.
 *   - BTN_R (bumper)                      : same as BTN_8.
 *   - BTN_LEFT softkey ("NEW")            : create a new blank note in
 *                                           the first empty slot (or
 *                                           overwrite the cursor row
 *                                           when full) and enter Edit.
 *   - BTN_RIGHT softkey ("OPEN") /
 *     BTN_ENTER                           : enter Edit on the cursor
 *                                           row, prefilled with the
 *                                           saved text. On an empty
 *                                           slot this is a no-op
 *                                           flash so the user knows
 *                                           why nothing happened.
 *   - BTN_BACK short                      : exit screen (pop()).
 *   - BTN_BACK long                       : exit screen + flash.
 *
 * Controls (Edit view):
 *   - BTN_0..BTN_9                        : T9 multi-tap (PhoneT9Input).
 *   - BTN_L                               : T9 backspace (forwards '*').
 *   - BTN_R                               : T9 case toggle (forwards '#').
 *   - BTN_ENTER                           : commit any in-flight pending
 *                                           letter (matches ConvoScreen).
 *   - BTN_LEFT softkey ("SAVE")           : commit pending letter,
 *                                           snapshot text into the
 *                                           bound slot, return to List.
 *                                           Empty text after trim
 *                                           clears the slot.
 *   - BTN_RIGHT softkey ("BACK") /
 *     BTN_BACK short                      : discard the in-flight edit
 *                                           and return to List.
 *   - BTN_BACK long                       : exit the entire screen.
 *
 * Implementation notes
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar / PhoneT9Input so the screen
 *     reads as part of the MAKERphone family. Data partition cost
 *     stays zero.
 *   - The list rows are pre-allocated lv_label children of `obj`,
 *     repainted on cursor move -- no per-cursor-step alloc traffic.
 *   - The PhoneT9Input is created and torn down per Edit session so
 *     its caret + commit timers do not consume CPU while the user is
 *     in the List view. Same pattern PhoneContactEdit uses.
 */
class PhoneNotepad : public LVScreen, private InputListener {
public:
	PhoneNotepad();
	virtual ~PhoneNotepad() override;

	void onStart() override;
	void onStop() override;

	/** Maximum number of saved notes. Matches the 4-row visual budget
	 *  the screen reserves between the caption and the soft-key bar
	 *  (49 px / 12 px per row = 4 rows fit cleanly with a tiny gutter).
	 */
	static constexpr uint8_t  MaxNotes      = 4;

	/** Hard cap on each note's text length. 120 keeps a single note
	 *  comfortably below the SMS-payload limit PhoneT9Input defaults
	 *  to (160) so a future "send as SMS" hook fits without spill.
	 */
	static constexpr uint16_t MaxNoteLen    = 120;

	/** Visual cap for the list-row preview. Anything longer is shown
	 *  with an ellipsis suffix.
	 */
	static constexpr uint8_t  PreviewChars  = 22;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs    = 600;

	/** View modes. Public so a host / test can introspect state. */
	enum class Mode : uint8_t {
		List = 0,
		Edit = 1,
	};
	Mode getMode() const { return mode; }

	/** Read-only accessors useful for tests and future hosts. */
	uint8_t getCursor()    const { return cursor; }
	uint8_t getNoteCount() const;
	const char* getNoteText(uint8_t slot) const;

	/** True if the slot index is in [0, MaxNotes) AND has non-empty text. */
	bool isSlotFilled(uint8_t slot) const;

	/**
	 * Trim leading + trailing ASCII whitespace from `in` into `out`.
	 * Static + side-effect-free so a host (or a test) can sanity-check
	 * the trim semantics without standing up the screen. Always nul-
	 * terminates `out` when `outLen > 0`.
	 */
	static void trimText(const char* in, char* out, size_t outLen);

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// List-view widgets.
	lv_obj_t* captionLabel = nullptr;          // "NOTES  N/4"
	lv_obj_t* topDivider   = nullptr;          // 1 px line under caption
	lv_obj_t* bottomDivider= nullptr;          // 1 px line above softkeys
	lv_obj_t* rowLabels[MaxNotes] = { nullptr };

	// Edit-view widgets.
	lv_obj_t*     editCaption = nullptr;       // "NOTE 2/4"
	lv_obj_t*     charCounter = nullptr;       // "12 of 120 chars"
	PhoneT9Input* t9Input     = nullptr;       // composed lazily on enter

	// State.
	Mode    mode    = Mode::List;
	uint8_t cursor  = 0;                       // selected slot in List view
	uint8_t editingSlot = 0;                   // slot bound to the active Edit
	bool    backLongFired = false;             // suppresses double-fire on hold

	// Note storage. Each slot is a fixed-size buffer so the screen has
	// zero per-edit allocation traffic. An empty note is signalled by
	// notes[i][0] == '\0'.
	char notes[MaxNotes][MaxNoteLen + 1] = {};

	// ---- builders ----
	void buildListView();
	void buildEditView();
	void teardownEditView();

	// ---- repainters ----
	void refreshCaption();
	void refreshRows();
	void refreshSoftKeys();
	void refreshEditCaption();
	void refreshCharCounter();

	// ---- model helpers ----
	int8_t firstEmptySlot() const;     // -1 if all full
	void   clearSlot(uint8_t slot);
	void   writeSlot(uint8_t slot, const char* text);

	// ---- mode transitions ----
	void enterList();
	void enterEdit(uint8_t slot, bool prefill);

	// ---- list actions ----
	void moveCursor(int8_t delta);
	void onNewPressed();
	void onOpenPressed();

	// ---- edit actions ----
	void onSavePressed();
	void onBackPressed();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONENOTEPAD_H
