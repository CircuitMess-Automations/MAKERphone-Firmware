#ifndef MAKERPHONE_PHONECOMPOSER_H
#define MAKERPHONE_PHONECOMPOSER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneComposer (S121)
 *
 * Phase-Q kickoff: a Sony-Ericsson-style "Composer" screen for hand-
 * authoring monophonic ringtones on the keypad. This session ships
 * the **UI only** -- the keypad-driven note buffer, ribbon display
 * and live "current-note" preview. The follow-on sessions wire the
 * surrounding plumbing:
 *
 *   S122 -> RTTTL parser + serializer (so the user's buffer can be
 *           round-tripped to/from the same string format
 *           PhoneRingtoneLibrary already uses).
 *   S123 -> Save-slots + wire to PhoneRingtoneEngine so the user
 *           can preview their composition through the piezo and
 *           store it as a ringtone.
 *
 * Slots in beside the rest of the Phase-L utility apps
 * (PhoneCalculator S60, PhoneStopwatch S61, PhoneTimer S62,
 *  PhoneCalendar S63, PhoneNotepad S64) so the screen wears the
 * same retro silhouette every other Phone* screen wears.
 *
 * Buffer model
 *   The screen holds an in-memory array of Note structs (size
 *   MaxNotes). Each Note captures everything the RTTTL serializer
 *   in S122 will need: tone ('C'..'B' or 'P' rest), sharp flag,
 *   octave (3..7), length (1/2/4/8/16/32), dotted flag.
 *
 * Controls
 *   BTN_1                 : enter rest ('P') at cursor.
 *   BTN_2..BTN_8          : enter tone C..B at cursor.
 *                            2=C 3=D 4=E 5=F 6=G 7=A 8=B
 *   BTN_9                 : duplicate the previous note.
 *   BTN_0                 : cycle the next-note duration stamp
 *                            (1/2/4/8/16/32 -> wraps).
 *   BTN_L                 : octave -- (clamped 3..7).
 *   BTN_R                 : octave ++ (clamped 3..7).
 *   BTN_ENTER             : insert a duplicate of the cursor row at
 *                            the cursor (pushes the rest right).
 *   BTN_LEFT softkey      : "CLR" -- clear the entire buffer.
 *   BTN_RIGHT softkey /
 *     BTN_BACK short      : delete the cursor's note (collapses
 *                            everything past it left). On an empty
 *                            buffer pops the screen.
 *   BTN_BACK long         : pop the screen unconditionally.
 *
 *   Pressing a tone or rest key when the buffer is full is a no-op
 *   flash so the user gets a visible cue WHY nothing happened.
 *
 * Implementation notes
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen reads as part of
 *     the MAKERphone family. Data partition cost stays zero.
 *   - The ribbon is pre-allocated as a fixed set of lv_label rows
 *     (RibbonRows count). Cursor moves repaint the labels rather than
 *     building/freeing children -- same pattern PhoneNotepad uses.
 *   - This header is intentionally framed so the S122 parser can be
 *     bolted on without disturbing the public surface: noteAt(i)
 *     returns a const Note& and appendNote / insertNoteAt /
 *     deleteNoteAt / clearAll are exposed so the parser can fill the
 *     buffer from a string.
 *   - The "next-note stamp" (octave + length + sharp) is what gets
 *     committed when the user presses a tone key. It is rendered
 *     above the ribbon so the user can see at a glance which octave
 *     and which duration the next press will use, and the stamp
 *     persists across presses (so typing "C C C C" gives four notes
 *     at the same length without re-stamping).
 */
class PhoneComposer : public LVScreen, private InputListener {
public:
	PhoneComposer();
	virtual ~PhoneComposer() override;

	void onStart() override;
	void onStop() override;

	/** Maximum number of notes the buffer can hold. 64 is comfortably
	 *  large enough for a verse-length feature-phone ringtone (the
	 *  Nokia / Sony-Ericsson originals all sit around 40-50 notes)
	 *  and small enough that the worst-case buffer is 256 bytes --
	 *  nothing for the ESP32 RAM budget. */
	static constexpr uint8_t  MaxNotes     = 64;

	/** Number of ribbon rows visible at once. */
	static constexpr uint8_t  RibbonRows   = 5;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs   = 600;

	/** Octave bounds. */
	static constexpr uint8_t  OctaveMin    = 3;
	static constexpr uint8_t  OctaveMax    = 7;
	static constexpr uint8_t  OctaveDef    = 4;

	/** RTTTL-style durations available via the *-key cycle. */
	static constexpr uint8_t  LengthCount  = 6;

	/** S123 — number of save slots reachable from the screen. Mirrors
	 *  PhoneComposerStorage::MaxSlots so the slot-cycle gesture wraps
	 *  cleanly at the same boundary the storage layer enforces. */
	static constexpr uint8_t  SaveSlotCount = 4;

	/** Long-press threshold for the new S123 bindings (play / save /
	 *  load / cycle-slot). Same hold time as BTN_BACK so muscle memory
	 *  transfers between every long-press the screen recognises. */
	static constexpr uint16_t SlotHoldMs   = 600;

	/** Default tempo committed alongside a save. The composer itself
	 *  doesn't expose a BPM editor (yet), so every save is stamped
	 *  with this value; later sessions can wire a tempo widget to the
	 *  same field without touching the storage layer. */
	static constexpr uint16_t DefaultBpm   = 63;

	/** A single composed note. Fields are tone / sharp / octave /
	 *  length / dotted so a future S122 RTTTL serializer can build
	 *  the canonical "8c#5." token in one strncpy chain. */
	struct Note {
		char    tone;     // 'C' 'D' 'E' 'F' 'G' 'A' 'B'  or 'P' for rest
		bool    sharp;    // sharp == half-step up
		uint8_t octave;   // 3..7  (ignored for 'P')
		uint8_t length;   // 1, 2, 4, 8, 16, 32
		bool    dotted;   // 50 % length extension
	};

	/** Read-only buffer accessors useful for S122/S123 + tests. */
	uint8_t       getNoteCount() const { return noteCount; }
	const Note&   noteAt(uint8_t i) const;
	uint8_t       getCursor() const    { return cursor; }

	/** Live "next-note stamp" introspection (S122 will let the parser
	 *  prime this so a freshly-loaded ringtone keeps editing where the
	 *  song left off). */
	uint8_t       getStampOctave() const { return stampOctave; }
	uint8_t       getStampLength() const { return stampLength; }
	bool          getStampSharp()  const { return stampSharp;  }
	bool          getStampDotted() const { return stampDotted; }

	/** Mutators. Bounds-check + repaint internally; safe for tests
	 *  and for the S122 parser to call directly. */
	bool          appendNote(const Note& n);
	bool          insertNoteAt(uint8_t i, const Note& n);
	bool          deleteNoteAt(uint8_t i);
	void          clearAll();

	/** S123 — slot index the screen is currently pointed at (0..3).
	 *  The view changes when the user long-presses the LEFT softkey
	 *  to cycle through slots. */
	uint8_t       getActiveSlot() const { return activeSlot; }

	/** S123 — replace the buffer with the contents of the active slot
	 *  via PhoneComposerStorage. Returns false if the slot was empty
	 *  or could not be loaded. The screen still repaints in either
	 *  case so the caption / softkeys reflect the latest state. */
	bool          loadFromActiveSlot();

	/** S123 — persist the current buffer into the active slot via
	 *  PhoneComposerStorage. Returns false if NVS rejected the write
	 *  (out of space, partition unmounted, etc.). */
	bool          saveToActiveSlot();

	/** S123 — kick off / cancel a buzzer preview of the current
	 *  buffer through PhoneComposerPlayback. `togglePreview()`
	 *  stops a running preview if one is already in flight, so the
	 *  same key acts as both play and stop. */
	void          togglePreview();
	bool          isPreviewing() const;

	/** Returns the canonical 1-char tone label for `tone`:
	 *    'C' 'D' 'E' 'F' 'G' 'A' 'B' -> the same character
	 *    'P'                          -> '-'
	 *    anything else                -> '?'   */
	static char   labelForTone(char tone);

	/** Spells the rest as "REST" rather than "-", used in the big
	 *  "current-note" preview. Writes at most outLen-1 characters
	 *  and always nul-terminates if outLen > 0. */
	static void   formatNote(const Note& n, char* out, size_t outLen);

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// ---- widgets ----
	lv_obj_t* captionLabel  = nullptr;          // "COMPOSER  N/64"
	lv_obj_t* topDivider    = nullptr;          // 1 px line under caption
	lv_obj_t* previewLabel  = nullptr;          // big current-note label
	lv_obj_t* stampLabel    = nullptr;          // stamp summary (small)
	lv_obj_t* midDivider    = nullptr;          // 1 px line between preview and ribbon
	lv_obj_t* botDivider    = nullptr;          // 1 px line above hints
	lv_obj_t* ribbonLabels[RibbonRows] = { nullptr };
	lv_obj_t* hintLine1     = nullptr;
	lv_obj_t* hintLine2     = nullptr;

	// ---- state ----
	Note     buffer[MaxNotes] = {};
	uint8_t  noteCount  = 0;
	uint8_t  cursor     = 0;
	// "next-note stamp" applied to the next tone/rest press.
	uint8_t  stampOctave = OctaveDef;
	uint8_t  stampLength = 4;        // quarter note default
	bool     stampSharp  = false;
	bool     stampDotted = false;
	bool     backLongFired = false;
	// S123 -- which slot the screen currently targets for save/load
	// + a record of whether a long-press of the LEFT softkey already
	// cycled the slot (so the short-press CLR semantics still fire
	// only on real short presses). The "Played the preview as a
	// hold" flag does the same for BTN_9.
	uint8_t  activeSlot     = 0;
	bool     leftLongFired  = false;
	bool     dupLongFired   = false;
	bool     enterLongFired = false;
	bool     cycleLongFired = false;

	// ---- builders ----
	void buildHeader();
	void buildPreview();
	void buildRibbon();
	void buildHints();

	// ---- repainters ----
	void refreshCaption();
	void refreshPreview();
	void refreshStamp();
	void refreshRibbon();
	void refreshSoftKeys();
	// S123 -- repaints the second hint line so the slot indicator
	// always reflects the active slot index and the play/stop
	// gesture's current outcome.
	void refreshHints();

	// ---- key actions ----
	void onToneKey(char tone);              // 'C'..'B' (uppercase) or 'P'
	void onDuplicateKey();                  // BTN_9
	void onCycleLength();                   // BTN_0
	void onOctaveDelta(int8_t delta);       // BTN_L / BTN_R
	void onInsertCopy();                    // BTN_ENTER
	void onDeleteCursor();                  // short BTN_BACK / softkey RIGHT

	// ---- helpers ----
	static uint8_t lengthIndexOf(uint8_t length);   // length -> idx
	static uint8_t advanceLength(uint8_t length);   // cycle helper
	void           toneRowLabel(const Note& n, char* out, size_t outLen) const;

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONECOMPOSER_H
