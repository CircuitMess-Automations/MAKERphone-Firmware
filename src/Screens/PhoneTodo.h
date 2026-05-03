#ifndef MAKERPHONE_PHONETODO_H
#define MAKERPHONE_PHONETODO_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneT9Input;

/**
 * PhoneTodo — S136
 *
 * Phase-Q "Pocket Organiser" task list. Three priority levels (HIGH /
 * MED / LOW), per-task done flag with a tick-off animation, persisted
 * across reboots through one NVS blob.
 *
 *   List view (default — at least one task exists):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             TODO   3/8                 | <- pixelbasic7 cyan caption
 *   |  -------------------------------------- |
 *   |  > [ ] H  Buy milk and bread            | <- cursor row, accent (HIGH)
 *   |    [x] M  Pick up Sara                  | <- done row, dim/striked
 *   |    [ ] L  Call dentist                  | <- cream row (LOW)
 *   |    [ ] M  Reply to Marko                |
 *   |    [ ] H  Submit invoice                |
 *   |    [x] L  Drop off package              |
 *   |  -------------------------------------- |
 *   |  NEW                            DONE    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 *   Edit view (after NEW or EDIT):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |              NEW TASK                  |
 *   |  +----------------------------------+ |
 *   |  | Reply to Marko about|            | | <- PhoneT9Input
 *   |  | abc                         Abc  | |
 *   |  +----------------------------------+ |
 *   |   priority H   |  press 5 to cycle    | <- hint strip
 *   |  SAVE                          BACK    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Two view modes:
 *
 *   List
 *     - Up to MaxTasks tasks; rows render a checkbox glyph + a
 *       priority letter + the task text. The cursor row is rendered
 *       with a leading chevron and an accent colour driven by the
 *       row's priority. Done tasks are rendered in MP_LABEL_DIM and
 *       prefixed with the [x] checkbox glyph; the priority letter
 *       fades to dim too so the eye reads the row as "completed".
 *     - The list scrolls automatically: when the cursor leaves the
 *       visible window, the scroll origin shifts by one row so the
 *       cursor is always on screen. Tasks past the bottom of the
 *       window simply are not painted.
 *
 *   Edit
 *     - Wraps a PhoneT9Input (S32) so the user multi-taps the task
 *       text in the same way they compose SMS in ConvoScreen (S33)
 *       and add notes in PhoneNotepad (S64). The composer is bound
 *       to whichever slot the user chose — "NEW" picks the first
 *       empty slot (or replaces the cursor row when full), "EDIT"
 *       prefills with the existing text. While in Edit view the
 *       BTN_5 key cycles the task's priority (HIGH -> MED -> LOW)
 *       so the user can set it without leaving the composer.
 *
 * Tick-off animation
 *
 *   Pressing ENTER on the list view toggles the cursor row's done
 *   flag. The transition is animated:
 *
 *     1) The row colour briefly flashes to MP_HIGHLIGHT cyan for
 *        TickFlashMs (~280 ms) so the user gets a clear visual cue.
 *     2) After the flash a one-shot lv_timer reverts the row to
 *        whatever colour the new state implies (dim if just-marked
 *        done, the priority colour if just-undone).
 *
 *   The flash is one lv_timer per toggle; if the user toggles again
 *   while it is still running, the previous timer is cancelled
 *   first. No allocation lives past the flash.
 *
 * Persistence
 *
 *   One NVS blob in namespace "mptodo" / key "t". Same magic-prefix
 *   pattern PhoneAlarmService and PhoneVirtualPet use. Layout:
 *
 *     [0]    magic 'M'
 *     [1]    magic 'P'
 *     [2]    version (1)
 *     [3]    task count (0..MaxTasks)
 *     for each saved task (count of them):
 *       [..]    flags byte: bit 0 = done, bits 4..5 = priority (0=HIGH, 1=MED, 2=LOW)
 *       [..]    text length (0..MaxLen)
 *       [..]    text bytes (no nul)
 *
 *   The blob is rewritten on every state-changing user call (add /
 *   edit / toggleDone / cyclePriority / remove). A read-back failure
 *   is fail-soft: the screen runs as RAM-only, exactly the way the
 *   pet service degrades when NVS refuses to open.
 *
 * Controls (List view):
 *   - BTN_2 / BTN_L                        : cursor up.
 *   - BTN_8 / BTN_R                        : cursor down.
 *   - BTN_LEFT softkey ("NEW")             : pick the first empty slot
 *                                            (or overwrite cursor when
 *                                            full) and enter Edit.
 *   - BTN_RIGHT softkey ("DONE") /
 *     BTN_ENTER                            : toggle the cursor row's
 *                                            done flag with the
 *                                            tick-off animation.
 *   - BTN_5                                : enter Edit on the cursor
 *                                            row (rename existing).
 *   - BTN_HASH (#) — i.e. BTN_3 chord with the dialer plate is N/A
 *     here. Priority cycling lives on the bumper instead:
 *   - BTN_R-long / BTN_HASH alias is N/A   : priority cycling is on
 *                                            BTN_5 in Edit and on
 *                                            BTN_LEFT-long in List.
 *   - BTN_BACK short                       : pop the screen.
 *
 * Controls (Edit view):
 *   - BTN_0..BTN_9 except BTN_5            : T9 multi-tap (PhoneT9Input).
 *   - BTN_5  (chord while editing)         : cycle bound task priority.
 *   - BTN_L                                : T9 backspace ('*').
 *   - BTN_R                                : T9 case toggle ('#').
 *   - BTN_ENTER                            : commit pending letter.
 *   - BTN_LEFT softkey ("SAVE")            : commit pending letter,
 *                                            snapshot text into the
 *                                            bound slot, return to
 *                                            List. Empty-after-trim
 *                                            text removes the slot
 *                                            (NEW path) or leaves the
 *                                            existing slot untouched
 *                                            (EDIT path).
 *   - BTN_RIGHT softkey ("BACK") /
 *     BTN_BACK short                       : discard the in-flight
 *                                            edit and return to List.
 *   - BTN_BACK long                        : exit the entire screen.
 *
 * Implementation notes
 *   - 100 % code-only — no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar / PhoneT9Input so the screen
 *     reads as part of the MAKERphone family. Data partition cost
 *     stays zero.
 *   - The list rows are pre-allocated lv_label children of `obj`,
 *     repainted on cursor move / toggle — no per-cursor-step alloc
 *     traffic. Six visible rows fit between the caption strip and
 *     the soft-key bar at 12 px row stride.
 *   - The PhoneT9Input is created and torn down per Edit session so
 *     its caret + commit timers do not consume CPU while the user
 *     is in the List view (matches PhoneNotepad's pattern).
 */
class PhoneTodo : public LVScreen, private InputListener {
public:
	PhoneTodo();
	virtual ~PhoneTodo() override;

	void onStart() override;
	void onStop() override;

	/** Maximum number of saved tasks. Eight rows is a comfortable cap
	 *  for a feature-phone — well above what a paper to-do list
	 *  would carry, and small enough that the NVS blob stays under
	 *  half a KB even with every slot full. */
	static constexpr uint8_t  MaxTasks      = 8;

	/** Hard cap on each task's text length. 40 keeps a single task
	 *  fitting in one row at pixelbasic7 with the checkbox + priority
	 *  prefix, so we never have to worry about LVGL hard-truncation. */
	static constexpr uint8_t  MaxLen        = 40;

	/** Visible list window — 6 rows fit between the caption strip
	 *  (y=22) and the bottom divider (y=98). Anything past this is
	 *  reachable by scrolling the cursor. */
	static constexpr uint8_t  VisibleRows   = 6;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs    = 600;

	/** How long the tick-off cyan flash is held before the row
	 *  reverts to its done/undone colour. Tuned so the user sees a
	 *  clear "click" cue without the row feeling laggy. */
	static constexpr uint32_t TickFlashMs   = 280;

	/** Three-level priority. The integer values are the on-disk
	 *  encoding inside the NVS blob's flags byte (bits 4..5). */
	enum class Priority : uint8_t {
		High = 0,
		Med  = 1,
		Low  = 2,
	};

	/** View modes. Public so a host / test can introspect state. */
	enum class Mode : uint8_t {
		List = 0,
		Edit = 1,
	};

	Mode    getMode()       const { return mode; }
	uint8_t getCursor()     const { return cursor; }
	uint8_t getTaskCount()  const { return taskCount; }

	/** Read-only accessors for tests / future hosts. */
	const char* getTaskText(uint8_t slot)     const;
	bool        isTaskDone(uint8_t slot)      const;
	Priority    getTaskPriority(uint8_t slot) const;

	/**
	 * Trim leading + trailing ASCII whitespace from `in` into `out`.
	 * Static + side-effect-free so a host (or a test) can sanity
	 * check the trim semantics without standing up the screen.
	 * Always nul-terminates `out` when `outLen > 0`.
	 */
	static void trimText(const char* in, char* out, size_t outLen);

	/** Cycle a priority in the canonical order HIGH -> MED -> LOW -> HIGH. */
	static Priority nextPriority(Priority p);

	/** One-letter mnemonic for a priority ("H", "M", "L"). */
	static char priorityLetter(Priority p);

private:
	// Underlying task slot. Kept tiny so the on-disk blob stays
	// dense even with MaxTasks (8) slots filled.
	struct Task {
		char     text[MaxLen + 1] = {};
		Priority priority         = Priority::Med;
		bool     done             = false;
	};

	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// List-view widgets.
	lv_obj_t* captionLabel  = nullptr;          // "TODO  N/M"
	lv_obj_t* topDivider    = nullptr;
	lv_obj_t* bottomDivider = nullptr;
	lv_obj_t* emptyHint     = nullptr;          // multi-line "no tasks" hint
	lv_obj_t* rowLabels[VisibleRows] = { nullptr };

	// Edit-view widgets.
	lv_obj_t*     editCaption = nullptr;        // "NEW TASK" / "EDIT TASK"
	lv_obj_t*     editHint    = nullptr;        // "priority H | press 5 to cycle"
	PhoneT9Input* t9Input     = nullptr;        // composed lazily on enter

	// Tick-off animation timer. One-shot; cancelled if a fresh toggle
	// fires while it is still alive.
	lv_timer_t* tickTimer    = nullptr;
	uint8_t     tickRow      = 0xFF;            // visible-row index being flashed

	// State.
	Mode    mode           = Mode::List;
	uint8_t cursor         = 0;                 // selected task index (0..taskCount-1)
	uint8_t scrollTop      = 0;                 // index of the top visible row
	uint8_t editingSlot    = 0;                 // task slot bound to the active Edit
	bool    editingNew     = false;             // true if Edit was entered via NEW
	Priority editingPriority = Priority::Med;   // staged priority during Edit
	bool    backLongFired  = false;             // suppresses double-fire on hold

	// Task storage. Fixed-size array; zero allocation on add/remove.
	Task    tasks[MaxTasks] = {};
	uint8_t taskCount = 0;

	// ---- builders ----
	void buildListView();
	void buildEditView();
	void teardownEditView();

	// ---- repainters ----
	void refreshCaption();
	void refreshRows();
	void refreshSoftKeys();
	void refreshEmptyHint();
	void refreshEditCaption();
	void refreshEditHint();

	// ---- model helpers ----
	int8_t   firstEmptySlot() const;            // -1 if full, else taskCount
	void     writeSlot(uint8_t slot, Priority pr, const char* text);
	void     removeSlot(uint8_t slot);
	void     ensureCursorVisible();

	// ---- mode transitions ----
	void enterList();
	void enterEdit(uint8_t slot, bool prefill, bool isNew);

	// ---- list actions ----
	void moveCursor(int8_t delta);
	void onNewPressed();
	void onDonePressed();
	void onEditPressed();   // BTN_5 in list view
	void onCyclePriority(); // BTN_R-long in list view

	// ---- edit actions ----
	void onSavePressed();
	void onBackPressed();
	void onCyclePriorityEdit();  // BTN_5 in edit view

	// ---- tick-off animation ----
	void startTickFlash(uint8_t visibleRow);
	void stopTickFlash();
	static void tickTimerCb(lv_timer_t* t);

	// ---- persistence ----
	void load();
	void save();

	// ---- input ----
	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONETODO_H
