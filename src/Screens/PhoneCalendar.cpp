#include "PhoneCalendar.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the calendar slots in beside PhoneCalculator (S60) / PhoneStopwatch (S61) /
// PhoneTimer (S62) / PhoneAboutScreen (S55) without a visual seam. Inlined
// per the established pattern (see PhoneCalculator.cpp / PhoneTimer.cpp).
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption / today
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream day digits
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple captions / adjacent month
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (cursor + big day)
#define MP_DIM             lv_color_make( 70,  56, 100)  // dim purple (header strip)

// ---------- geometry ------------------------------------------------------
//
// 160x128 budget:
//   y=  0..10  PhoneStatusBar
//   y= 12      "JAN 2026" caption (pixelbasic7, ~7 px tall)
//   y= 22      weekday header strip (pixelbasic7, ~7 px tall)
//   y= 32..116 month grid (6 rows x 14 px = 84 px)
//   y=118..128 PhoneSoftKeyBar
//
// 7 columns x 22 px = 154 px wide; 154 + 6 px margin = 160 px. Anchor
// the whole grid at x=3 to keep it centred without flex math.

static constexpr lv_coord_t kCaptionY    = 12;
static constexpr lv_coord_t kHeaderY     = 22;
static constexpr lv_coord_t kHeaderH     = 9;
static constexpr lv_coord_t kGridY       = 32;
static constexpr lv_coord_t kGridLeftX   = 3;
static constexpr lv_coord_t kGridW       = PhoneCalendar::GridCols * PhoneCalendar::CellW;
static constexpr lv_coord_t kGridH       = PhoneCalendar::GridRows * PhoneCalendar::CellH;

// Detail panel covers the same band as weekday header + grid.
static constexpr lv_coord_t kDetailY     = kHeaderY;
static constexpr lv_coord_t kDetailH     = (kGridY + kGridH) - kHeaderY;

// Detail row anchors (relative to the detail container's top).
static constexpr lv_coord_t kDtlWeekdayY = 6;
static constexpr lv_coord_t kDtlDayY     = 18;
static constexpr lv_coord_t kDtlMonthY   = 50;
static constexpr lv_coord_t kDtlBadgeY   = 66;

// ---------- ctor / dtor ---------------------------------------------------

PhoneCalendar::PhoneCalendar()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  weekdayHeader(nullptr),
		  gridContainer(nullptr),
		  cursorBox(nullptr),
		  todayRing(nullptr),
		  detailContainer(nullptr),
		  detailWeekday(nullptr),
		  detailDay(nullptr),
		  detailMonthYear(nullptr),
		  detailTodayBadge(nullptr),
		  curYear(2026), curMonth(1), curDay(1),
		  todayYear(2026), todayMonth(1), todayDay(1),
		  firstWeekday(0), monthLength(31) {

	// Snapshot today from PhoneClock so the screen opens centred on the
	// current wall-clock day. Same source-of-truth PhoneDateTimeScreen
	// (S54) and PhoneAboutScreen (S55) read.
	refreshTodaySnapshot();
	curYear  = todayYear;
	curMonth = todayMonth;
	curDay   = todayDay;
	if(curYear < YearMin) curYear = YearMin;
	if(curYear > YearMax) curYear = YearMax;

	// Full-screen container, no scrollbars, no padding -- same blank-canvas
	// pattern PhoneCalculator / PhoneTimer / PhoneAboutScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. Same
	// synthwave background every other Phase-L app uses.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildWeekdayHeader();
	buildGrid();           // creates gridContainer + 42 cell labels
	buildHighlighters();   // cursor box and today ring (children of grid)
	buildDetailPanel();    // hidden by default

	// Bottom soft-key bar. Initial Month-view pair.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("DETAIL");
	softKeys->setRight("TODAY");

	setButtonHoldTime(BTN_BACK, BackHoldMs);

	// Initial paint: month layout, cell colours, cursor / today position,
	// detail card hidden.
	recomputeMonthLayout();
	refreshCaption();
	refreshGrid();
	refreshCursor();
	refreshTodayRing();
	refreshDetail();
	refreshSoftKeys();

	enterMode(Mode::Month);
}

PhoneCalendar::~PhoneCalendar() {
	// All children (wallpaper, statusBar, softKeys, labels, grid, detail)
	// are parented to obj and freed by the LVScreen base destructor.
}

void PhoneCalendar::onStart() {
	Input::getInstance()->addListener(this);

	// Re-snapshot today on every entry so the cyan ring follows the
	// wall clock if the user just edited it in PhoneDateTimeScreen.
	refreshTodaySnapshot();
	refreshTodayRing();
	refreshGrid();        // active-month colour depends on today coords
}

void PhoneCalendar::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- weekday math --------------------------------------------------

uint8_t PhoneCalendar::weekdayOf(uint16_t year, uint8_t month, uint8_t day) {
	// Mirror PhoneClock::buildEpoch's clamping so this matches the rest
	// of the firmware's calendar exactly.
	if(year < YearMin) year = YearMin;
	if(year > YearMax) year = YearMax;
	if(month < 1)  month = 1;
	if(month > 12) month = 12;
	uint8_t maxDay = PhoneClock::daysInMonth(year, month);
	if(maxDay == 0) maxDay = 31;
	if(day < 1)        day = 1;
	if(day > maxDay)   day = maxDay;

	// PhoneClock anchors at Thu 2026-01-01 (weekday 4 under SUN=0).
	// Compute days from anchor with signed math so years before 2026
	// produce a correct (positive) weekday after the modulo step.
	int32_t daysFromAnchor = ((int32_t) year - 2026) * 365;
	for(uint8_t i = 1; i < month; ++i) {
		daysFromAnchor += (int32_t) PhoneClock::daysInMonth(year, i);
	}
	daysFromAnchor += ((int32_t) day - 1);

	// Anchor weekday is THU = 4. Use a safe positive-modulo
	// formulation: ((x % 7) + 7) % 7 handles negative deltas.
	int32_t wd = ((daysFromAnchor + 4) % 7 + 7) % 7;
	return (uint8_t) wd;
}

void PhoneCalendar::recomputeMonthLayout() {
	monthLength  = PhoneClock::daysInMonth(curYear, curMonth);
	if(monthLength == 0) monthLength = 31;     // defensive
	firstWeekday = weekdayOf(curYear, curMonth, 1);
}

void PhoneCalendar::refreshTodaySnapshot() {
	uint16_t y; uint8_t mo, d, hh, mm, ss, wd;
	PhoneClock::now(y, mo, d, hh, mm, ss, wd);
	todayYear  = y;
	todayMonth = mo;
	todayDay   = d;
}

// ---------- builders ------------------------------------------------------

void PhoneCalendar::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
}

void PhoneCalendar::buildWeekdayHeader() {
	weekdayHeader = lv_obj_create(obj);
	lv_obj_remove_style_all(weekdayHeader);
	lv_obj_set_size(weekdayHeader, kGridW, kHeaderH);
	lv_obj_set_pos(weekdayHeader, kGridLeftX, kHeaderY);
	lv_obj_set_scrollbar_mode(weekdayHeader, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(weekdayHeader, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(weekdayHeader, 0, 0);

	// Subtle dim-purple strip behind the labels so the header reads
	// as a header rather than floating text. Semi-transparent so the
	// synthwave wallpaper still shows through.
	lv_obj_set_style_bg_color(weekdayHeader, MP_DIM, 0);
	lv_obj_set_style_bg_opa(weekdayHeader, LV_OPA_30, 0);

	// Weekday letters under the SUN..SAT order PhoneClock uses (0=SUN).
	// Two-letter caption ("SU", "MO", ...) keeps each column under 22 px.
	static const char* kWeekdayShort[GridCols] = {
		"SU", "MO", "TU", "WE", "TH", "FR", "SA"
	};
	for(uint8_t c = 0; c < GridCols; ++c) {
		lv_obj_t* l = lv_label_create(weekdayHeader);
		lv_obj_set_style_text_font(l, &pixelbasic7, 0);
		lv_obj_set_style_text_color(l, MP_LABEL_DIM, 0);
		// Sundays sit at index 0 -- pop them in MP_ACCENT so weekend
		// columns read as accented, matching the feature-phone idiom.
		if(c == 0 || c == 6) {
			lv_obj_set_style_text_color(l, MP_ACCENT, 0);
		}
		lv_label_set_text(l, kWeekdayShort[c]);
		lv_obj_set_size(l, CellW, kHeaderH);
		lv_obj_set_pos(l, c * CellW, 0);
		lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
		// Center horizontally inside the column by leaving the label at
		// its natural width and using LV_TEXT_ALIGN_CENTER on the label
		// (works because we've explicitly sized the label to CellW).
		weekdayCells[c] = l;
	}
}

void PhoneCalendar::buildGrid() {
	gridContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(gridContainer);
	lv_obj_set_size(gridContainer, kGridW, kGridH);
	lv_obj_set_pos(gridContainer, kGridLeftX, kGridY);
	lv_obj_set_scrollbar_mode(gridContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(gridContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(gridContainer, 0, 0);

	// Ultra-thin 1 px MP_DIM frame so the user can see the grid edges
	// even in cells that have no day rendered (leading/trailing blanks).
	lv_obj_set_style_bg_opa(gridContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(gridContainer, MP_DIM, 0);
	lv_obj_set_style_border_opa(gridContainer, LV_OPA_40, 0);
	lv_obj_set_style_border_width(gridContainer, 1, 0);

	// 42 day labels, row-major. Anchored individually rather than via
	// flex so the cursor / today highlighter math indexes the same way.
	for(uint8_t i = 0; i < CellCount; ++i) {
		const uint8_t row = i / GridCols;
		const uint8_t col = i % GridCols;
		lv_obj_t* l = lv_label_create(gridContainer);
		lv_obj_set_style_text_font(l, &pixelbasic7, 0);
		lv_obj_set_style_text_color(l, MP_TEXT, 0);
		lv_obj_set_size(l, CellW, CellH);
		lv_obj_set_pos(l, col * CellW, row * CellH);
		lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
		// Pad-top aligns the digit baseline with the cyan-ring centre
		// (cells are 14 px tall, glyphs are ~7 px). 4 px nudges the
		// digit to sit visually centred without overlapping the bottom
		// edge of the cell border.
		lv_obj_set_style_pad_top(l, 4, 0);
		lv_label_set_text(l, "");
		cellLabels[i] = l;
	}
}

void PhoneCalendar::buildHighlighters() {
	if(gridContainer == nullptr) return;

	// Today ring sits below the cursor in z-order so a cursor landing
	// ON today still shows its sunset bg. Both rings hide initially;
	// refreshCursor() / refreshTodayRing() position them on first paint.
	todayRing = lv_obj_create(gridContainer);
	lv_obj_remove_style_all(todayRing);
	lv_obj_set_size(todayRing, CellW, CellH);
	lv_obj_set_pos(todayRing, 0, 0);
	lv_obj_clear_flag(todayRing, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(todayRing, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_bg_color(todayRing, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(todayRing, LV_OPA_30, 0);
	lv_obj_set_style_border_color(todayRing, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_opa(todayRing, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(todayRing, 1, 0);
	lv_obj_add_flag(todayRing, LV_OBJ_FLAG_HIDDEN);

	cursorBox = lv_obj_create(gridContainer);
	lv_obj_remove_style_all(cursorBox);
	lv_obj_set_size(cursorBox, CellW, CellH);
	lv_obj_set_pos(cursorBox, 0, 0);
	lv_obj_clear_flag(cursorBox, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(cursorBox, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_bg_color(cursorBox, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(cursorBox, LV_OPA_70, 0);
	lv_obj_set_style_border_color(cursorBox, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(cursorBox, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(cursorBox, 1, 0);

	// Re-order so day labels paint over the highlighters: highlighters
	// were created before the labels in buildGrid()? No -- they were
	// created AFTER. Move them to the background so the day digits
	// stay on top.
	lv_obj_move_background(todayRing);
	lv_obj_move_background(cursorBox);
	// Keep cursorBox above todayRing.
	lv_obj_move_foreground(cursorBox);
	// And then the labels were already at the top of the z-stack
	// from buildGrid(); moving the highlighters to background restores
	// label > cursor > todayRing > container ordering.
}

void PhoneCalendar::buildDetailPanel() {
	detailContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(detailContainer);
	lv_obj_set_size(detailContainer, 160, kDetailH);
	lv_obj_set_pos(detailContainer, 0, kDetailY);
	lv_obj_set_scrollbar_mode(detailContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(detailContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(detailContainer, 0, 0);
	// Slight darkening so the detail card reads as a panel without
	// completely obscuring the synthwave wallpaper.
	lv_obj_set_style_bg_color(detailContainer, MP_DIM, 0);
	lv_obj_set_style_bg_opa(detailContainer, LV_OPA_40, 0);
	lv_obj_set_style_border_width(detailContainer, 0, 0);

	detailWeekday = lv_label_create(detailContainer);
	lv_obj_set_style_text_font(detailWeekday, &pixelbasic7, 0);
	lv_obj_set_style_text_color(detailWeekday, MP_HIGHLIGHT, 0);
	lv_label_set_text(detailWeekday, "");
	lv_obj_set_align(detailWeekday, LV_ALIGN_TOP_MID);
	lv_obj_set_y(detailWeekday, kDtlWeekdayY);

	detailDay = lv_label_create(detailContainer);
	lv_obj_set_style_text_font(detailDay, &pixelbasic16, 0);
	lv_obj_set_style_text_color(detailDay, MP_ACCENT, 0);
	lv_label_set_text(detailDay, "");
	lv_obj_set_align(detailDay, LV_ALIGN_TOP_MID);
	lv_obj_set_y(detailDay, kDtlDayY);

	detailMonthYear = lv_label_create(detailContainer);
	lv_obj_set_style_text_font(detailMonthYear, &pixelbasic7, 0);
	lv_obj_set_style_text_color(detailMonthYear, MP_TEXT, 0);
	lv_label_set_text(detailMonthYear, "");
	lv_obj_set_align(detailMonthYear, LV_ALIGN_TOP_MID);
	lv_obj_set_y(detailMonthYear, kDtlMonthY);

	detailTodayBadge = lv_label_create(detailContainer);
	lv_obj_set_style_text_font(detailTodayBadge, &pixelbasic7, 0);
	lv_obj_set_style_text_color(detailTodayBadge, MP_TEXT, 0);
	lv_obj_set_style_bg_color(detailTodayBadge, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(detailTodayBadge, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_top(detailTodayBadge, 1, 0);
	lv_obj_set_style_pad_bottom(detailTodayBadge, 1, 0);
	lv_obj_set_style_pad_left(detailTodayBadge, 4, 0);
	lv_obj_set_style_pad_right(detailTodayBadge, 4, 0);
	lv_obj_set_style_radius(detailTodayBadge, 1, 0);
	lv_label_set_text(detailTodayBadge, "TODAY");
	lv_obj_set_align(detailTodayBadge, LV_ALIGN_TOP_MID);
	lv_obj_set_y(detailTodayBadge, kDtlBadgeY);

	lv_obj_add_flag(detailContainer, LV_OBJ_FLAG_HIDDEN);
}

// ---------- repaint -------------------------------------------------------

void PhoneCalendar::refreshCaption() {
	if(captionLabel == nullptr) return;
	char buf[16];
	snprintf(buf, sizeof(buf), "%s %u",
			 PhoneClock::monthName(curMonth),
			 (unsigned) curYear);
	lv_label_set_text(captionLabel, buf);
}

void PhoneCalendar::refreshGrid() {
	if(gridContainer == nullptr) return;

	// Compute leading offset (number of blank cells before day 1) and
	// repaint every cell with the right day number + colour.
	const int8_t leading = (int8_t) firstWeekday;

	// Adjacent-month context for the leading and trailing fillers.
	uint16_t prevYear  = curYear;
	int16_t  prevMonth = (int16_t) curMonth - 1;
	if(prevMonth < 1) { prevMonth = 12; prevYear--; }
	const uint8_t prevLength = PhoneClock::daysInMonth(prevYear, (uint8_t) prevMonth);

	for(uint8_t i = 0; i < CellCount; ++i) {
		lv_obj_t* l = cellLabels[i];
		if(l == nullptr) continue;

		const int16_t dayInActive = (int16_t) i - leading + 1; // 1-based
		char buf[4];
		lv_color_t col = MP_TEXT;

		if(dayInActive < 1) {
			// Leading filler from previous month.
			const uint8_t pd = (uint8_t)((int16_t) prevLength + dayInActive);
			snprintf(buf, sizeof(buf), "%u", (unsigned) pd);
			col = MP_LABEL_DIM;
		} else if(dayInActive > (int16_t) monthLength) {
			// Trailing filler from next month.
			const uint8_t nd = (uint8_t)(dayInActive - (int16_t) monthLength);
			snprintf(buf, sizeof(buf), "%u", (unsigned) nd);
			col = MP_LABEL_DIM;
		} else {
			snprintf(buf, sizeof(buf), "%u", (unsigned) dayInActive);
			// Active-month days: warm cream by default; sunset on today
			// (cyan ring will paint behind it). Sundays / Saturdays
			// keep the cream colour -- the header already accentuates
			// the weekend columns.
			col = MP_TEXT;
			if((uint16_t) dayInActive == (uint16_t) todayDay &&
			   curMonth == todayMonth && curYear == todayYear) {
				col = MP_ACCENT;
			}
		}
		lv_label_set_text(l, buf);
		lv_obj_set_style_text_color(l, col, 0);
	}
}

void PhoneCalendar::cellFor(uint8_t day, uint8_t& row, uint8_t& col) const {
	if(day < 1) day = 1;
	if(day > monthLength) day = monthLength;
	const uint16_t cellIndex = (uint16_t) firstWeekday + (uint16_t)(day - 1);
	row = (uint8_t)(cellIndex / GridCols);
	col = (uint8_t)(cellIndex % GridCols);
}

void PhoneCalendar::refreshCursor() {
	if(cursorBox == nullptr) return;
	uint8_t r, c;
	cellFor(curDay, r, c);
	lv_obj_set_pos(cursorBox, c * CellW, r * CellH);
	lv_obj_clear_flag(cursorBox, LV_OBJ_FLAG_HIDDEN);
}

void PhoneCalendar::refreshTodayRing() {
	if(todayRing == nullptr) return;
	if(todayMonth != curMonth || todayYear != curYear) {
		// Today is not in the visible month -- hide the ring.
		lv_obj_add_flag(todayRing, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	uint16_t cellIndex = (uint16_t) firstWeekday + (uint16_t)(todayDay - 1);
	const uint8_t r = (uint8_t)(cellIndex / GridCols);
	const uint8_t c = (uint8_t)(cellIndex % GridCols);
	lv_obj_set_pos(todayRing, c * CellW, r * CellH);
	lv_obj_clear_flag(todayRing, LV_OBJ_FLAG_HIDDEN);
}

void PhoneCalendar::refreshDetail() {
	if(detailContainer == nullptr) return;
	// Long-form weekday name (uppercase, 9 chars max -- "WEDNESDAY"
	// is the longest). Pixelbasic7 at 8 px/glyph keeps it well under
	// the 160 px width.
	static const char* kLongWeekday[7] = {
		"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
		"THURSDAY", "FRIDAY", "SATURDAY"
	};
	const uint8_t wd = weekdayOf(curYear, curMonth, curDay);
	if(detailWeekday) lv_label_set_text(detailWeekday, kLongWeekday[wd]);

	char dbuf[4];
	snprintf(dbuf, sizeof(dbuf), "%u", (unsigned) curDay);
	if(detailDay) lv_label_set_text(detailDay, dbuf);

	char mbuf[16];
	snprintf(mbuf, sizeof(mbuf), "%s %u",
			 PhoneClock::monthName(curMonth),
			 (unsigned) curYear);
	if(detailMonthYear) lv_label_set_text(detailMonthYear, mbuf);

	const bool isToday = (curYear == todayYear &&
						  curMonth == todayMonth &&
						  curDay == todayDay);
	if(detailTodayBadge) {
		if(isToday) lv_obj_clear_flag(detailTodayBadge, LV_OBJ_FLAG_HIDDEN);
		else        lv_obj_add_flag(detailTodayBadge, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneCalendar::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(mode) {
		case Mode::Month:
			softKeys->setLeft("DETAIL");
			softKeys->setRight("TODAY");
			break;
		case Mode::Detail:
			softKeys->setLeft("BACK");
			softKeys->setRight("TODAY");
			break;
	}
}

// ---------- mode toggle ---------------------------------------------------

void PhoneCalendar::enterMode(Mode newMode) {
	mode = newMode;
	switch(newMode) {
		case Mode::Month:
			if(weekdayHeader)   lv_obj_clear_flag(weekdayHeader, LV_OBJ_FLAG_HIDDEN);
			if(gridContainer)   lv_obj_clear_flag(gridContainer, LV_OBJ_FLAG_HIDDEN);
			if(detailContainer) lv_obj_add_flag(detailContainer, LV_OBJ_FLAG_HIDDEN);
			break;
		case Mode::Detail:
			if(weekdayHeader)   lv_obj_add_flag(weekdayHeader, LV_OBJ_FLAG_HIDDEN);
			if(gridContainer)   lv_obj_add_flag(gridContainer, LV_OBJ_FLAG_HIDDEN);
			if(detailContainer) lv_obj_clear_flag(detailContainer, LV_OBJ_FLAG_HIDDEN);
			refreshDetail();
			break;
	}
	refreshSoftKeys();
}

// ---------- navigation ----------------------------------------------------

void PhoneCalendar::stepDay(int32_t delta) {
	int32_t y = (int32_t) curYear;
	int32_t m = (int32_t) curMonth;
	int32_t d = (int32_t) curDay + delta;

	// Borrow from the previous month while d <= 0.
	while(d <= 0) {
		m--;
		if(m < 1) { m = 12; y--; }
		if(y < (int32_t) YearMin) {
			// Underflow -- pin to the very first cell.
			y = YearMin; m = 1; d = 1;
			break;
		}
		d += (int32_t) PhoneClock::daysInMonth((uint16_t) y, (uint8_t) m);
	}
	// Carry to the next month while d > daysInMonth.
	while(true) {
		uint8_t len = PhoneClock::daysInMonth((uint16_t) y, (uint8_t) m);
		if(d <= (int32_t) len) break;
		d -= (int32_t) len;
		m++;
		if(m > 12) { m = 1; y++; }
		if(y > (int32_t) YearMax) {
			// Overflow -- pin to the very last cell.
			y = YearMax; m = 12;
			d = (int32_t) PhoneClock::daysInMonth((uint16_t) y, (uint8_t) m);
			break;
		}
	}

	const bool monthChanged = (y != (int32_t) curYear) || (m != (int32_t) curMonth);
	curYear  = (uint16_t) y;
	curMonth = (uint8_t) m;
	curDay   = (uint8_t) d;

	if(monthChanged) {
		recomputeMonthLayout();
		refreshCaption();
		refreshGrid();
		refreshTodayRing();
	}
	refreshCursor();
	if(mode == Mode::Detail) refreshDetail();
}

void PhoneCalendar::stepMonth(int32_t delta) {
	int32_t y = (int32_t) curYear;
	int32_t m = (int32_t) curMonth + delta;
	while(m < 1)  { m += 12; y--; }
	while(m > 12) { m -= 12; y++; }
	if(y < (int32_t) YearMin) { y = YearMin; m = 1;  }
	if(y > (int32_t) YearMax) { y = YearMax; m = 12; }

	curYear  = (uint16_t) y;
	curMonth = (uint8_t) m;
	const uint8_t len = PhoneClock::daysInMonth(curYear, curMonth);
	if(curDay > len) curDay = len;
	if(curDay < 1)   curDay = 1;

	recomputeMonthLayout();
	refreshCaption();
	refreshGrid();
	refreshCursor();
	refreshTodayRing();
	if(mode == Mode::Detail) refreshDetail();
}

void PhoneCalendar::snapToToday() {
	curYear  = todayYear;
	curMonth = todayMonth;
	curDay   = todayDay;
	if(curYear < YearMin) curYear = YearMin;
	if(curYear > YearMax) curYear = YearMax;
	const uint8_t len = PhoneClock::daysInMonth(curYear, curMonth);
	if(curDay > len) curDay = len;
	if(curDay < 1)   curDay = 1;

	recomputeMonthLayout();
	refreshCaption();
	refreshGrid();
	refreshCursor();
	refreshTodayRing();
	enterMode(Mode::Month);
}

// ---------- input ---------------------------------------------------------

void PhoneCalendar::buttonPressed(uint i) {
	// Numeric arrow keys behave the same in both modes -- they keep
	// scrubbing the cursor while the detail card is open, which lines
	// up with how feature-phone calendars let you flick through days
	// without first dismissing the detail panel.
	switch(i) {
		case BTN_4:
		case BTN_LEFT:
			if(softKeys && i == BTN_LEFT) softKeys->flashLeft();
			stepDay(-1);
			return;
		case BTN_6:
		case BTN_RIGHT:
			if(softKeys && i == BTN_RIGHT) softKeys->flashRight();
			stepDay(+1);
			return;
		case BTN_2:
			stepDay(-7);
			return;
		case BTN_8:
			stepDay(+7);
			return;
		case BTN_L:
			stepMonth(-1);
			return;
		case BTN_R:
			stepMonth(+1);
			return;
		default:
			break;
	}

	switch(i) {
		case BTN_ENTER:
		case BTN_5:
			if(softKeys && i == BTN_ENTER) softKeys->flashLeft();
			if(mode == Mode::Month) enterMode(Mode::Detail);
			else                    enterMode(Mode::Month);
			break;
		case BTN_BACK:
			backLongFired = false;
			break;
		default:
			break;
	}
}

void PhoneCalendar::buttonReleased(uint i) {
	switch(i) {
		case BTN_BACK:
			if(!backLongFired) {
				if(mode == Mode::Detail) {
					enterMode(Mode::Month);
				} else {
					pop();
				}
			}
			backLongFired = false;
			break;
		default:
			break;
	}
}

void PhoneCalendar::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK always exits, regardless of which view we
			// are in. Same gesture every other Phone* utility honours.
			backLongFired = true;
			pop();
			break;
		default:
			break;
	}
}
