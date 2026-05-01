#ifndef MAKERPHONE_PHONEDATETIMESCREEN_H
#define MAKERPHONE_PHONEDATETIMESCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneDateTimeScreen
 *
 * Phase-J sub-screen (S54): the Date & Time settings screen reachable
 * from the SYSTEM section of PhoneSettingsScreen (S50). Replaces the
 * DATE & TIME placeholder stub with a five-field civil-time editor:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             DATE & TIME                | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |          2026 - 01 - 15                | <- date row (pixelbasic16)
 *   |                THU                     | <- weekday tag (pixelbasic7)
 *   |              12 : 34                   | <- time row (pixelbasic16)
 *   |                                        |
 *   |       L/R: field   2/8: value          | <- pixelbasic7 dim hint
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The screen edits a snapshot of PhoneClock::now() (the in-memory
 * wall-clock service introduced in S54). The five tappable fields are
 * Year (2020..2099), Month (1..12), Day (1..daysInMonth), Hour (0..23)
 * and Minute (0..59). The currently focused field renders in sunset
 * orange while the rest stay cyan, so the user always knows which
 * value the value-keys (BTN_2 / BTN_8) will adjust. The weekday tag
 * just under the date row updates live every time the date changes,
 * so the user can see "wait, that's a Sunday, not what I wanted" as
 * they scrub.
 *
 * Behavior:
 *  - LEFT / 4: move the cursor to the previous field. Wraps through
 *    Year .. Minute (cycling feels more feature-phone-like than a
 *    hard stop on a 5-element list).
 *  - RIGHT / 6: move the cursor to the next field. Wraps the same
 *    way as the LEFT direction.
 *  - 2: increment the focused field's value (year +1, month +1, etc).
 *    Day clamps to daysInMonth(year, month) so a Feb 30 is impossible
 *    by construction. Hour / minute wrap inside their natural range.
 *  - 8: decrement the focused field. Same clamps as 2.
 *  - ENTER (SAVE softkey): write the edited value through
 *    PhoneClock::setEpoch() and pop. The next screen that calls
 *    PhoneClock::now() (S55 About, future status-bar swap, ...)
 *    picks up the new wall time on the next frame.
 *  - BACK: pop without committing - the standard Sony-Ericsson
 *    "discard changes" affordance.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen feels visually part of the rest of
 *    the MAKERphone family.
 *  - The date / time rows are flex-laid-out lv_obj containers (not
 *    single pre-formatted labels) so each field is its own lv_label.
 *    That lets us color-cue the focused field without sub-character
 *    highlight tricks. The flex container's gap stays at 1 px so the
 *    digits read as "2026-01-15" with thin separators rather than as
 *    five disjoint labels.
 *  - PhoneClock::now()/buildEpoch() do all the calendar arithmetic
 *    behind the scenes so the screen never has to think about months,
 *    leap years (we ignore them — same convention as PhoneClockFace),
 *    or epoch encoding. Round-tripping a value through the screen is
 *    exact.
 *  - 160x128 budget: 10 px status bar (y=0..10), caption strip
 *    (y=12..20), date row at y=26 (14 px tall), weekday at y=44,
 *    time row at y=58, hint at y=82, soft-key bar at y=118..128.
 */
class PhoneDateTimeScreen : public LVScreen, private InputListener {
public:
	enum class Field : uint8_t {
		Year   = 0,
		Month  = 1,
		Day    = 2,
		Hour   = 3,
		Minute = 4,
	};

	/** Number of selectable fields. */
	static constexpr uint8_t FieldCount = 5;

	/** Year clamp range, kept in sync with PhoneClock::buildEpoch(). */
	static constexpr uint16_t YearMin = 2020;
	static constexpr uint16_t YearMax = 2099;

	PhoneDateTimeScreen();
	virtual ~PhoneDateTimeScreen() override;

	void onStart() override;
	void onStop() override;

	/** Currently focused field (useful for tests / hosts that introspect). */
	Field getCurrentField() const { return static_cast<Field>(cursor); }

	/** Currently displayed civil-time fields (post-edit, pre-save). */
	uint16_t getYear()   const { return year; }
	uint8_t  getMonth()  const { return month; }
	uint8_t  getDay()    const { return day; }
	uint8_t  getHour()   const { return hour; }
	uint8_t  getMinute() const { return minute; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;     // "DATE & TIME"
	lv_obj_t* dateRow;          // flex row hosting year / sep / month / sep / day
	lv_obj_t* timeRow;          // flex row hosting hour / colon / minute
	lv_obj_t* weekdayLabel;     // "MON" tag between the rows
	lv_obj_t* hintLabel;        // "L/R: field   2/8: value"

	lv_obj_t* yearLabel;        // pixelbasic16
	lv_obj_t* dateSep1;         // "-"
	lv_obj_t* monthLabel;
	lv_obj_t* dateSep2;
	lv_obj_t* dayLabel;

	lv_obj_t* hourLabel;
	lv_obj_t* timeColon;        // ":"
	lv_obj_t* minuteLabel;

	uint16_t year;
	uint8_t  month;
	uint8_t  day;
	uint8_t  hour;
	uint8_t  minute;

	/** Snapshot of PhoneClock::nowEpoch() at construction (used by BACK). */
	uint32_t initialEpoch;

	uint8_t cursor;             // 0..FieldCount-1

	void buildCaption();
	void buildDateRow();
	void buildTimeRow();
	void buildWeekday();
	void buildHint();

	/** Repaint every value label + recolor the focused / unfocused pair. */
	void refreshDisplay();

	/** Move the cursor by ±1 (wraps through the 5 fields). */
	void moveCursorBy(int8_t delta);

	/** Step the focused field's value by ±1 with field-specific clamping. */
	void adjustValueBy(int8_t delta);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;

	/**
	 * Clamp `day` so it never exceeds the days available in the
	 * current (year, month). Called whenever year or month changes
	 * so the display never shows e.g. "Feb 30".
	 */
	void clampDayToMonth();
};

#endif // MAKERPHONE_PHONEDATETIMESCREEN_H
