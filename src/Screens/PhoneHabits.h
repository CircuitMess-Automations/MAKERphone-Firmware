#ifndef MAKERPHONE_PHONEHABITS_H
#define MAKERPHONE_PHONEHABITS_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneT9Input;

/**
 * PhoneHabits — S137
 *
 * Phase-Q "Pocket Organiser" five-habit daily tracker. Up to five
 * named habits, each with a 28-day completion bitmap, a streak
 * counter and a 7-day heatmap strip. Persists across reboots through
 * one NVS blob. Day-rollover is detected from PhoneClock::nowEpoch()
 * so a habit that was last touched five days ago correctly shifts
 * its history into the right historical slots when the user opens
 * the screen on the new day.
 *
 *   List view (default — at least one habit exists):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             HABITS  3/5                | <- pixelbasic7 cyan caption
 *   |  -------------------------------------- |
 *   |  >[x] Drink water  [][][]  [][][][]  5 | <- cursor row, accent
 *   |   [ ] Read 30 min  [][][][][][][]    2 |
 *   |   [x] Stretch      [][][][][][][]    1 |
 *   |   [ ] Journal      [][][][][][][]    0 |
 *   |                                          |
 *   |  -------------------------------------- |
 *   |  NEW                            TICK    |
 *   +----------------------------------------+
 *
 *   Edit view (after NEW or EDIT):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |              NEW HABIT                 |
 *   |  +----------------------------------+ |
 *   |  | Drink water|                Abc | | <- PhoneT9Input
 *   |  +----------------------------------+ |
 *   |     name up to 16 characters           | <- hint strip
 *   |  SAVE                          BACK    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Two view modes — the same structure PhoneTodo (S136) uses so the
 * organiser apps share a muscle memory:
 *
 *   List
 *     - Up to MaxHabits (5) habits, each rendered as a name + a
 *       7-day heatmap strip + a streak counter. The heatmap is
 *       left-to-right, oldest first, today is the rightmost cell.
 *       Done days paint MP_ACCENT (today done paints MP_HIGHLIGHT
 *       cyan); missed days paint MP_DIM. The cursor row's name and
 *       streak boost to MP_ACCENT.
 *     - Up/down moves the cursor, ENTER (or the "TICK" softkey)
 *       toggles today's done flag on the cursor habit with a
 *       cyan tick-off flash.
 *
 *   Edit
 *     - Wraps a PhoneT9Input (S32) for the habit name. NEW always
 *       picks the first empty slot (or overwrites the cursor row
 *       when the list is full, replacing that habit with a fresh
 *       blank history). EDIT prefills the existing name and
 *       preserves history. Saving with an empty trimmed name
 *       removes the slot (EDIT) or no-ops (NEW) — same gesture
 *       PhoneNotepad and PhoneTodo expose so the user does not
 *       have to learn a new "delete" key for every Phase-Q app.
 *
 * Day rollover
 *
 *   On every load() and on every state-change call the screen
 *   shifts each habit's 28-bit history left by
 *   (todayDay - lastSyncDay) so "today" always lives in bit 0 and
 *   "n days ago" in bit n. A clock-set-backward leaves history
 *   alone (delta <= 0). The day index is
 *   `PhoneClock::nowEpoch() / 86400`, which gives a stable
 *   monotonic value once the user has set the wall clock.
 *
 * Streak counter
 *
 *   Counts the run length of consecutive 1 bits starting at bit 0.
 *   Today not done -> streak displays 0; today done -> streak ==
 *   1 plus consecutive prior days. Capped at HistoryDays (28)
 *   since that's the size of the on-disk history.
 *
 * Persistence
 *
 *   One NVS blob in namespace "mphabits" / key "h". Layout:
 *
 *     [0]    magic 'M'
 *     [1]    magic 'P'
 *     [2]    version (1)
 *     [3]    habit count (0..MaxHabits)
 *     [4..7] last-sync day index (uint32_t little-endian)
 *     for each saved habit (count of them):
 *       [..]    name length (0..MaxNameLen)
 *       [..]    name bytes (no nul terminator)
 *       [..]    history (uint32_t little-endian, only low 28 bits)
 *
 *   The blob is rewritten on every state-changing user call (add /
 *   edit / tick / remove). A read-back failure is fail-soft: the
 *   screen runs RAM-only, exactly the way PhoneTodo (S136),
 *   PhoneVirtualPet (S129) and PhoneAlarmService degrade.
 *
 * Controls (List view):
 *   - BTN_2 / BTN_L                     : cursor up.
 *   - BTN_8 / BTN_R                     : cursor down.
 *   - BTN_LEFT softkey ("NEW")          : pick the first empty slot
 *                                         (or overwrite the cursor
 *                                         row when full) and enter
 *                                         Edit.
 *   - BTN_RIGHT softkey ("TICK"/"UNDO")
 *     / BTN_ENTER                       : toggle today's done flag
 *                                         on the cursor habit with
 *                                         the tick-off flash.
 *   - BTN_5                              : enter Edit on the cursor
 *                                         row (rename / clear-to-
 *                                         delete).
 *   - BTN_BACK short                     : pop the screen.
 *
 * Controls (Edit view):
 *   - BTN_0..BTN_9                       : T9 multi-tap.
 *   - BTN_L                              : T9 backspace ('*').
 *   - BTN_R                              : T9 case toggle ('#').
 *   - BTN_ENTER                          : commit pending letter.
 *   - BTN_LEFT softkey ("SAVE")          : commit + snapshot name +
 *                                          return to List. Empty-
 *                                          after-trim name removes
 *                                          the slot on the EDIT
 *                                          path; NEW path no-ops
 *                                          on empty.
 *   - BTN_RIGHT softkey ("BACK") /
 *     BTN_BACK short                     : discard the in-flight
 *                                          edit and return to List.
 *   - BTN_BACK long                      : exit the entire screen.
 *
 * Implementation notes
 *   - 100 % code-only — no SPIFFS asset growth. Reuses
 *     PhoneSynthwaveBg / PhoneStatusBar / PhoneSoftKeyBar /
 *     PhoneT9Input so the screen reads as part of the MAKERphone
 *     family. The 5 * 7 = 35 heatmap cells are pre-allocated
 *     lv_obj rectangles + repainted on refreshRows; no per-cursor
 *     alloc traffic.
 *   - Five habit rows fit between y = 22 (top divider) and
 *     y = 98 (bottom divider) at a 14 px row stride, leaving the
 *     standard 10 px PhoneSoftKeyBar at the bottom and the 10 px
 *     PhoneStatusBar at the top.
 *   - Documents control chord, on-disk layout, geometry, and day
 *     rollover semantics inline so the next Phase-Q sessions
 *     (S138-S143) can mirror the same structure.
 */
class PhoneHabits : public LVScreen, private InputListener {
public:
	PhoneHabits();
	virtual ~PhoneHabits() override;

	void onStart() override;
	void onStop() override;

	/** Maximum number of saved habits. Five rows fits cleanly between
	 *  the dividers at 14 px stride and matches the roadmap entry. */
	static constexpr uint8_t  MaxHabits     = 5;

	/** Hard cap on each habit's name length. 16 characters fits the
	 *  visible name strip at pixelbasic7 with the cursor +
	 *  checkbox prefix without LVGL hard-truncation. */
	static constexpr uint8_t  MaxNameLen    = 16;

	/** History bitmap depth. 28 days = four weeks; bit 0 = today,
	 *  bit n = n days ago. Capped at 28 so the on-disk word fits a
	 *  uint32_t and so streak values fit a uint8_t. */
	static constexpr uint8_t  HistoryDays   = 28;

	/** How many days the heatmap strip displays. 7 == one week, the
	 *  classic "this-week" feature-phone widget size. */
	static constexpr uint8_t  HeatmapDays   = 7;

	/** Long-press threshold (matches the rest of the MAKERphone
	 *  shell — PhoneTodo, PhoneNotepad, PhoneStopwatch all use 600). */
	static constexpr uint16_t BackHoldMs    = 600;

	/** How long the tick-off cyan flash is held before the row
	 *  reverts to its done/undone colour. Tuned so the user gets
	 *  a clear "click" cue without the row feeling laggy. */
	static constexpr uint32_t TickFlashMs   = 280;

	/** View modes. Public so a host / test can introspect state. */
	enum class Mode : uint8_t {
		List = 0,
		Edit = 1,
	};

	Mode    getMode()        const { return mode; }
	uint8_t getCursor()      const { return cursor; }
	uint8_t getHabitCount()  const { return habitCount; }

	/** Read-only accessors for tests / future hosts. */
	const char* getHabitName(uint8_t slot)              const;
	bool        isCompletedToday(uint8_t slot)          const;
	bool        isCompletedDaysAgo(uint8_t slot,
	                               uint8_t daysAgo)     const;
	uint8_t     streakOf(uint8_t slot)                  const;
	uint32_t    getHistory(uint8_t slot)                const;

	/** Pure helper exposed for tests: streak length encoded as the
	 *  number of consecutive 1 bits starting at bit 0. */
	static uint8_t streakOfBits(uint32_t history);

	/**
	 * Trim leading + trailing ASCII whitespace from `in` into `out`.
	 * Always nul-terminates `out` when `outLen > 0`. Pure / static
	 * so a host (or a test) can sanity-check the trim semantics
	 * without standing up the screen.
	 */
	static void trimText(const char* in, char* out, size_t outLen);

private:
	// Underlying habit slot. Kept tiny so the on-disk blob stays
	// dense even with MaxHabits (5) slots filled.
	struct Habit {
		char     name[MaxNameLen + 1] = {};
		uint32_t history              = 0;  // bit 0 = today
	};

	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// List-view widgets.
	lv_obj_t* captionLabel  = nullptr;          // "HABITS  N/M"
	lv_obj_t* topDivider    = nullptr;
	lv_obj_t* bottomDivider = nullptr;
	lv_obj_t* emptyHint     = nullptr;          // multi-line "no habits" hint
	lv_obj_t* nameLabels[MaxHabits]   = { nullptr };
	lv_obj_t* streakLabels[MaxHabits] = { nullptr };
	lv_obj_t* heatmapCells[MaxHabits][HeatmapDays] = { { nullptr } };

	// Edit-view widgets.
	lv_obj_t*     editCaption = nullptr;        // "NEW HABIT" / "EDIT HABIT"
	lv_obj_t*     editHint    = nullptr;
	PhoneT9Input* t9Input     = nullptr;        // composed lazily on enter

	// Tick-off animation timer. One-shot; cancelled if a fresh toggle
	// fires while it is still alive.
	lv_timer_t* tickTimer = nullptr;
	uint8_t     tickRow   = 0xFF;               // visible-row index being flashed

	// State.
	Mode     mode          = Mode::List;
	uint8_t  cursor        = 0;
	uint8_t  editingSlot   = 0;
	bool     editingNew    = false;
	bool     backLongFired = false;             // suppresses double-fire on hold

	// Habit storage. Fixed-size array; zero allocation on add/remove.
	Habit    habits[MaxHabits] = {};
	uint8_t  habitCount        = 0;
	uint32_t lastSyncDay       = 0;             // last day index history was synced to

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

	// ---- model helpers ----
	int8_t   firstEmptySlot() const;
	void     writeName(uint8_t slot, const char* text);
	void     removeSlot(uint8_t slot);
	void     toggleToday(uint8_t slot);

	// ---- day rollover ----
	void     syncToToday();
	static uint32_t todayIndex();

	// ---- mode transitions ----
	void enterList();
	void enterEdit(uint8_t slot, bool prefill, bool isNew);

	// ---- list actions ----
	void moveCursor(int8_t delta);
	void onNewPressed();
	void onTickPressed();
	void onEditPressed();

	// ---- edit actions ----
	void onSavePressed();
	void onBackPressed();

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

#endif // MAKERPHONE_PHONEHABITS_H
