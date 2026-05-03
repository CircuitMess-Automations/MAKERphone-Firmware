#ifndef MAKERPHONE_PHONECOUNTDOWN_H
#define MAKERPHONE_PHONECOUNTDOWN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneT9Input;

/**
 * PhoneCountdown — S142
 *
 * Phase-Q "Pocket Organiser" days-until-event widget. Sits next to
 * PhoneTodo (S136), PhoneHabits (S137), PhonePomodoro (S138),
 * PhoneMoodLog (S139), PhoneScratchpad (S140) and PhoneExpenses
 * (S141) inside the eventual organiser-apps grid. The user logs a
 * named calendar event (e.g. "WEDDING", "EXAM", "XMAS") with an
 * absolute year/month/day and the screen surfaces the rolling
 * "days until" / "days since" tally for every saved event, sorted
 * by closeness to today.
 *
 *   List view (default — at least one event exists):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |          COUNTDOWN  3/5                | <- caption (cyan)
 *   |  ------------------------------------- |
 *   |  > XMAS         IN  235 DAYS  25 DEC | <- cursor row
 *   |    BIRTHDAY     IN   12 DAYS  15 MAY |
 *   |    HALLOWEEN    IN  180 DAYS  31 OCT |
 *   |    EXAM         TODAY!        03 MAY |
 *   |    LASTPARTY    30 DAYS AGO  03 APR  | <- past, dim
 *   |  ------------------------------------- |
 *   |  NEW                            EDIT   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 *   Empty view (no events saved):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |          COUNTDOWN  0/8                |
 *   |  ------------------------------------- |
 *   |       NO COUNTDOWNS YET.               |
 *   |       PRESS "NEW" TO ADD               |
 *   |       AN EVENT TO COUNT DOWN.          |
 *   |  ------------------------------------- |
 *   |  NEW                            BACK   |
 *   +----------------------------------------+
 *
 *   Edit view (after pressing NEW or EDIT):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |             NEW EVENT                  | <- caption
 *   |  +----------------------------------+ |
 *   |  | XMAS DAY|                        | | <- PhoneT9Input (Name stage)
 *   |  | abc                         Abc  | |
 *   |  +----------------------------------+ |
 *   |          2026 - 12 - 25                | <- date row, focused field accent
 *   |     L/R: field   2/8: +/-              | <- hint
 *   |  NEXT                          BACK    | <- PhoneSoftKeyBar (Name stage)
 *   +----------------------------------------+
 *
 *   Edit view advances through three stages:
 *
 *     Name  : PhoneT9Input is live; digits go to T9. Soft-keys read
 *             "NEXT" / "BACK".
 *     Date  : T9 input is dimmed (visual only, the widget is left in
 *             place so the user can read what they typed). Date row is
 *             interactive: BTN_L / BTN_R cycle the focused field
 *             (year, month, day), BTN_2 / BTN_8 increment / decrement
 *             the focused field's value. Soft-keys read "SAVE" /
 *             "BACK".
 *
 *   The two-stage flow keeps the digit keys from doing double duty
 *   (T9 letters in the name vs. date adjustment), avoiding the
 *   "I typed 12 and got Q" surprise that a single-mode screen would
 *   suffer from.
 *
 * Sort order:
 *   Future and today's events render first (smaller daysUntil first);
 *   past events fall to the bottom and sort by recency (most recently
 *   passed first). Ties between events on the same day break
 *   alphabetically by event name so the list is stable across reloads.
 *
 * Persistence:
 *   One small NVS blob in namespace "mpcd" / key "e". Layout:
 *     [0]    magic 'M'
 *     [1]    magic 'P'
 *     [2]    version (1)
 *     [3]    eventCount
 *     [4..N] per-event records:
 *       [0..1] year   (uint16 LE)
 *       [2]    month  (1..12)
 *       [3]    day    (1..31)
 *       [4]    nameLen
 *       [5..]  nameLen bytes of name (no nul terminator)
 *
 *   Read failure (missing blob, bad magic, NVS error) leaves the
 *   in-memory list empty and the screen runs RAM-only. Writes are
 *   best-effort and fail-soft, the same way every other Phase-Q app
 *   degrades.
 *
 * Controls (List view):
 *   - BTN_2 / BTN_L      : cursor up   (cycle to bottom on top edge).
 *   - BTN_8 / BTN_R      : cursor down (cycle to top on bottom edge).
 *   - BTN_LEFT softkey ("NEW")  : enter Edit view on a fresh slot.
 *   - BTN_RIGHT softkey ("EDIT") /
 *     BTN_5            : enter Edit view bound to the cursor row.
 *   - BTN_ENTER          : alias for EDIT (no-op when empty).
 *   - BTN_3              : delete the cursor row (no-op when empty).
 *   - BTN_BACK           : pop the screen.
 *
 * Controls (Edit / Name stage):
 *   - BTN_0..BTN_9       : T9 multi-tap.
 *   - BTN_L              : T9 backspace.
 *   - BTN_R              : T9 case toggle.
 *   - BTN_ENTER          : commit pending letter.
 *   - BTN_LEFT  ("NEXT") : advance to Date stage.
 *   - BTN_RIGHT ("BACK") /
 *     BTN_BACK short     : discard the in-flight edit, back to List.
 *   - BTN_BACK long      : exit the screen entirely.
 *
 * Controls (Edit / Date stage):
 *   - BTN_2              : increment the focused field.
 *   - BTN_8              : decrement the focused field.
 *   - BTN_L              : previous field (Year -> Day wraps).
 *   - BTN_R              : next field (Year -> Month -> Day wraps).
 *   - BTN_LEFT  ("SAVE") : commit the event into the bound slot,
 *                          back to List.
 *   - BTN_RIGHT ("BACK") /
 *     BTN_BACK short     : back to Name stage.
 *   - BTN_BACK long      : exit the screen entirely.
 *
 * Implementation notes:
 *   - 100 % code-only — no SPIFFS asset growth. Reuses
 *     PhoneSynthwaveBg / PhoneStatusBar / PhoneSoftKeyBar /
 *     PhoneT9Input so the screen reads as part of the MAKERphone
 *     family.
 *   - Up to MaxEvents (8) events are kept in RAM; appending past the
 *     cap silently no-ops — the user gets a brief soft-key flash but
 *     no slot is overwritten (same conservative behaviour as
 *     PhoneTimers).
 *   - The screen uses the same leap-year-free 365-day calendar
 *     PhoneClock already exposes, so a Feb-29 entry is impossible by
 *     construction (the Day field clamps to PhoneClock::daysInMonth
 *     after every Year / Month change).
 *   - The "days until" math is exposed as a static helper so a host
 *     or test can reuse it without standing up the whole screen.
 */
class PhoneCountdown : public LVScreen, private InputListener {
public:
	PhoneCountdown();
	virtual ~PhoneCountdown() override;

	void onStart() override;
	void onStop() override;

	/** Maximum number of saved events. Eight is plenty for a feature
	 *  phone — well above what a paper countdown list would carry,
	 *  and small enough that the NVS blob stays well under 200 bytes
	 *  even with every slot full. */
	static constexpr uint8_t  MaxEvents      = 8;

	/** Hard cap on each event's name length. 16 keeps a single row
	 *  fitting in the 152 px list width with the days/date columns
	 *  alongside it at pixelbasic7. */
	static constexpr uint8_t  MaxNameLen     = 16;

	/** Visible list window. Five rows fit cleanly between the caption
	 *  strip (y=22) and the bottom divider (y=98) at 14 px row stride. */
	static constexpr uint8_t  VisibleRows    = 5;

	/** Year clamp range. Matches PhoneDateTimeScreen / PhoneClock. */
	static constexpr uint16_t YearMin        = 2020;
	static constexpr uint16_t YearMax        = 2099;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs     = 600;

	/** Pixel height of one list row at pixelbasic7. */
	static constexpr lv_coord_t RowHeight    = 14;

	/** View modes. Public so a host / test can introspect state. */
	enum class Mode : uint8_t {
		List = 0,
		Edit = 1,
	};

	/** Edit-view sub-stage. Public for the same introspection reason. */
	enum class EditStage : uint8_t {
		Name = 0,
		Date = 1,
	};

	/** Date-picker focused field. Public for the same reason. */
	enum class DateField : uint8_t {
		Year  = 0,
		Month = 1,
		Day   = 2,
	};

	Mode      getMode()         const { return mode; }
	EditStage getEditStage()    const { return editStage; }
	DateField getDateField()    const { return dateField; }
	uint8_t   getCursor()       const { return cursor; }
	uint8_t   getEventCount()   const { return eventCount; }

	/** Read-only accessors for tests / future hosts. */
	const char* getEventName(uint8_t slot)  const;
	uint16_t    getEventYear(uint8_t slot)  const;
	uint8_t     getEventMonth(uint8_t slot) const;
	uint8_t     getEventDay(uint8_t slot)   const;

	/**
	 * Signed days until (`evtY`, `evtM`, `evtD`) given a "today" of
	 * (`todayY`, `todayM`, `todayD`). Both inputs are clamped to the
	 * supported civil-time ranges before the math runs. Uses the
	 * leap-year-free 365-day calendar PhoneClock exposes, so the
	 * result is exact and stable across devices.
	 *
	 * Returns 0 when the event is "today", a positive value for
	 * future events, a negative value for past events.
	 */
	static int32_t daysUntilDate(uint16_t todayYear, uint8_t todayMonth, uint8_t todayDay,
	                             uint16_t evtYear,   uint8_t evtMonth,   uint8_t evtDay);

	/**
	 * Days since the project epoch (Jan 1 of YearMin) under the same
	 * leap-year-free calendar. Public so tests can sanity check the
	 * monotone property without standing up the screen.
	 */
	static int32_t daysSinceEpoch(uint16_t year, uint8_t month, uint8_t day);

	/**
	 * Trim leading + trailing ASCII whitespace from `in` into `out`.
	 * Always nul-terminates `out` when `outLen > 0`.
	 */
	static void trimText(const char* in, char* out, size_t outLen);

private:
	// One event row.
	struct Event {
		char     name[MaxNameLen + 1] = {};
		uint16_t year  = YearMin;
		uint8_t  month = 1;
		uint8_t  day   = 1;
	};

	// One precomputed visible row in the sorted list. Captured on
	// every refresh() so the LVGL labels can render without re-walking
	// the events array on every cursor tick.
	struct SortedRow {
		uint8_t slot;        // index into events[]
		int32_t daysUntil;   // signed days against today
	};

	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// List-view widgets.
	lv_obj_t* captionLabel  = nullptr;          // "COUNTDOWN N/M"
	lv_obj_t* topDivider    = nullptr;
	lv_obj_t* bottomDivider = nullptr;
	lv_obj_t* emptyHint     = nullptr;
	lv_obj_t* rowLabels[VisibleRows] = { nullptr };

	// Edit-view widgets.
	PhoneT9Input* t9Input        = nullptr;
	lv_obj_t*     editCaption    = nullptr;
	lv_obj_t*     editDateLabel  = nullptr;     // "2026 - 12 - 25"
	lv_obj_t*     editHint       = nullptr;

	// State.
	Mode      mode           = Mode::List;
	EditStage editStage      = EditStage::Name;
	DateField dateField      = DateField::Year;
	uint8_t   cursor         = 0;               // selected row in sorted order
	uint8_t   editingSlot    = 0;               // events[] slot bound to the active Edit
	bool      editingNew     = false;
	bool      backLongFired  = false;

	// Staged date during Edit. Default values are populated from
	// PhoneClock::now() on entry to a fresh NEW edit so the user
	// starts on today's date and just nudges from there.
	uint16_t  stagedYear     = YearMin;
	uint8_t   stagedMonth    = 1;
	uint8_t   stagedDay      = 1;

	// Cached "today" so the list renders stable values inside one
	// onStart cycle. Refreshed on every list-mode entry.
	uint16_t  todayYear      = YearMin;
	uint8_t   todayMonth     = 1;
	uint8_t   todayDay       = 1;

	// Storage.
	Event   events[MaxEvents] = {};
	uint8_t eventCount = 0;

	// Sort buffer — repopulated on every list-mode refresh so cursor
	// motion stays O(eventCount) (insertion-sort).
	SortedRow sorted[MaxEvents] = {};

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
	void refreshEditDate();
	void refreshEditHint();

	// ---- model helpers ----
	void rebuildSorted();
	void cacheToday();
	void clampStagedDay();

	// ---- mode / stage transitions ----
	void enterList();
	void enterEdit(uint8_t slot, bool prefill, bool isNew);
	void enterEditDateStage();
	void enterEditNameStage();

	// ---- list actions ----
	void moveCursor(int8_t delta);
	void onNewPressed();
	void onEditPressed();
	void onDeletePressed();

	// ---- edit actions ----
	void onNamePressed();   // SAVE / NEXT softkey in Name stage
	void onSavePressed();   // SAVE softkey in Date stage
	void onBackPressed();   // BACK softkey, stage-aware
	void onDateAdjust(int8_t delta);
	void onDateFieldShift(int8_t delta);

	// ---- persistence ----
	void load();
	void save();

	// ---- input ----
	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONECOUNTDOWN_H
