#ifndef MAKERPHONE_PHONECALENDAR_H
#define MAKERPHONE_PHONECALENDAR_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneCalendar
 *
 * Phase-L utility app (S63): a Sony-Ericsson-style monthly calendar.
 * Slots in next to PhoneCalculator (S60) / PhoneStopwatch (S61) /
 * PhoneTimer (S62) inside the eventual utility-apps grid. Same retro
 * silhouette every other Phone* screen wears:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             JAN 2026                   | <- pixelbasic7 cyan caption
 *   |   SUN MON TUE WED THU FRI SAT          | <- weekday header (pixelbasic7)
 *   |    .   .   .   1   2   3   4           | <- six-row month grid
 *   |    5   6   7   8   9  10  11           |
 *   |   12  13  14 [15] 16  17  18           |    [..] = cursor
 *   |   19  20  21  22  23  24  25           |    cyan ring = today
 *   |   26  27  28  29  30  31   .           |
 *   |    .   .   .   .   .   .   .           |
 *   |   DETAIL                      TODAY    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Two view modes:
 *   - Month: 7x6 day grid with cursor + today highlight. The user
 *     scrubs around with the dialer arrows (4/6 = day, 2/8 = week,
 *     L/R bumper = month). Cursor wraps cleanly through month
 *     boundaries: the "left of day 1" cell reads as the previous
 *     month's last day and pressing LEFT one more time lands the
 *     cursor on it (the visible month flips to that previous month).
 *   - Detail: replaces the grid with a full-page day card showing
 *     the long-form weekday name, the day number in pixelbasic16,
 *     "MONTH YEAR" underneath, and a "TODAY" badge if the cursor
 *     happens to be on the wall-clock day. The right softkey is
 *     swapped for "TODAY" so the user can always one-tap snap back
 *     to the wall-clock day from either view.
 *
 * Controls (Month view):
 *   - BTN_4 / BTN_LEFT  : day -1 (wraps months / years).
 *   - BTN_6 / BTN_RIGHT : day +1 (wraps months / years).
 *   - BTN_2             : day -7 (previous week).
 *   - BTN_8             : day +7 (next week).
 *   - BTN_L             : month -1 (preserves day, clamped to the
 *                         new month's daysInMonth).
 *   - BTN_R             : month +1 (preserves day, clamped).
 *   - BTN_ENTER / 5     : open Detail view on the selected day.
 *   - Right softkey "TODAY" / BTN_R bumper : snap cursor to the
 *                         wall-clock day (PhoneClock::now()).
 *   - BTN_BACK          : short = exit screen; long = exit screen.
 *
 * Controls (Detail view):
 *   - BTN_ENTER / BTN_LEFT-softkey "BACK" / BTN_BACK short :
 *                         return to Month view.
 *   - BTN_BACK long     : exit screen.
 *   - Right softkey "TODAY" : snap cursor to today AND return to
 *                         Month view (matches the user expectation
 *                         that "TODAY" always brings you home).
 *   - Arrows / digit nav : passed through to the month-view step
 *                         logic so the user can keep scrubbing the
 *                         day from inside the detail card.
 *
 * Implementation notes:
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually
 *     part of the MAKERphone family. Data partition cost stays zero.
 *   - The month grid is exactly 7 cols x 6 rows = 42 day labels.
 *     Six rows is the worst case for any month under our calendar
 *     (a month that starts on a Saturday and is 31 days long fills
 *     six rows; never seven). Each row is 14 px tall, each column
 *     22 px wide, totalling 154 x 84 px for the grid -- centred on
 *     the 160 x 128 viewport with 3 px lateral margin.
 *   - Two persistent highlighter lv_obj's float over the grid: a
 *     dim-purple "cursor" rectangle (moves with the user) and a
 *     cyan "today ring" rectangle (moves only when the wall-clock
 *     day changes -- in practice once at midnight). Cells outside
 *     the active month render in MP_LABEL_DIM so the user can
 *     instantly see where the month boundaries are.
 *   - All weekday math is local (no PhoneClock mutation). Today is
 *     read once on onStart() via PhoneClock::now() so re-entering
 *     the screen after editing the wall clock in S54 picks up the
 *     new "today" without a stale cache.
 *   - The leap-year-free calendar matches PhoneClock so the grid
 *     and the wall-clock display agree on every date.
 */
class PhoneCalendar : public LVScreen, private InputListener {
public:
	PhoneCalendar();
	virtual ~PhoneCalendar() override;

	void onStart() override;
	void onStop() override;

	/** Month-grid geometry. Public so a host / test can size buffers. */
	static constexpr uint8_t  GridCols   = 7;
	static constexpr uint8_t  GridRows   = 6;
	static constexpr uint8_t  CellCount  = GridCols * GridRows;

	static constexpr lv_coord_t CellW    = 22;
	static constexpr lv_coord_t CellH    = 14;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs = 600;

	/** Year clamp (kept in sync with PhoneClock::buildEpoch). */
	static constexpr uint16_t YearMin    = 2020;
	static constexpr uint16_t YearMax    = 2099;

	/**
	 * Compute the weekday for an arbitrary (y, m, d) using the same
	 * leap-year-free calendar PhoneClock uses. Static + side-effect-
	 * free so a host (or a test) can sanity-check the grid math
	 * without standing up the screen. Result is 0=SUN..6=SAT, matching
	 * PhoneClock::weekdayName(). Out-of-range inputs are clamped to
	 * the nearest valid civil-time field.
	 */
	static uint8_t weekdayOf(uint16_t year, uint8_t month, uint8_t day);

	/** Currently selected (cursor) date -- public for tests / hosts. */
	uint16_t getCursorYear()  const { return curYear; }
	uint8_t  getCursorMonth() const { return curMonth; }
	uint8_t  getCursorDay()   const { return curDay; }

	/** Currently displayed view mode. */
	enum class Mode : uint8_t {
		Month  = 0,
		Detail = 1,
	};
	Mode getMode() const { return mode; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Month-view widgets.
	lv_obj_t* captionLabel;                 // "JAN 2026"
	lv_obj_t* weekdayHeader;                // row container
	lv_obj_t* weekdayCells[GridCols] = { nullptr }; // SUN..SAT labels
	lv_obj_t* gridContainer;                // anchors all cells
	lv_obj_t* cellLabels[CellCount]  = { nullptr };
	lv_obj_t* cursorBox;                    // dim-purple selection rect
	lv_obj_t* todayRing;                    // cyan ring around today

	// Detail-view widgets (siblings of the grid; toggled visible).
	lv_obj_t* detailContainer;
	lv_obj_t* detailWeekday;                // "WEDNESDAY"
	lv_obj_t* detailDay;                    // "15" (pixelbasic16)
	lv_obj_t* detailMonthYear;              // "JAN 2026"
	lv_obj_t* detailTodayBadge;             // optional "TODAY"

	// State.
	Mode     mode    = Mode::Month;

	uint16_t curYear;       // cursor / displayed-month year
	uint8_t  curMonth;      // cursor / displayed-month month (1..12)
	uint8_t  curDay;        // cursor day-of-month (1..daysInMonth)

	uint16_t todayYear;     // wall-clock today, captured at onStart
	uint8_t  todayMonth;
	uint8_t  todayDay;

	uint8_t  firstWeekday;  // weekday of day 1 of (curYear, curMonth)
	uint8_t  monthLength;   // daysInMonth(curYear, curMonth)

	bool     backLongFired = false;

	void buildCaption();
	void buildWeekdayHeader();
	void buildGrid();
	void buildHighlighters();
	void buildDetailPanel();

	/** Recompute firstWeekday + monthLength from (curYear, curMonth). */
	void recomputeMonthLayout();

	/** Repaint the "JAN 2026" caption from (curYear, curMonth). */
	void refreshCaption();
	/** Repaint every cell label + its color (active/adjacent/today). */
	void refreshGrid();
	/** Move the cursor highlighter to the cell hosting (curYear,curMonth,curDay). */
	void refreshCursor();
	/** Move the today-ring to today's cell, or hide it when off-grid. */
	void refreshTodayRing();
	/** Repaint the day-detail card. */
	void refreshDetail();
	/** Repaint the soft-key labels for the current mode. */
	void refreshSoftKeys();

	/** Toggle visibility between month grid and detail card. */
	void enterMode(Mode newMode);

	/** Step the cursor by `delta` whole days (handles month/year wrap). */
	void stepDay(int32_t delta);
	/** Step the active month by `delta` (preserves day, clamped). */
	void stepMonth(int32_t delta);
	/** Snap cursor to today and return to Month view. */
	void snapToToday();

	/** Day -> (row, col) inside the 7x6 grid (row,col in 0-based). */
	void cellFor(uint8_t day, uint8_t& row, uint8_t& col) const;

	/** Refresh today's snapshot from PhoneClock::now(). */
	void refreshTodaySnapshot();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONECALENDAR_H
