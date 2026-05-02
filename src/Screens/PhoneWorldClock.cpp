#include "PhoneWorldClock.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

// MAKERphone retro palette - kept identical to every other Phone* widget
// so the world clock slots in beside PhoneCalculator (S60),
// PhoneAlarmClock (S124), PhoneTimers (S125), PhoneCurrencyConverter
// (S126) and PhoneUnitConverter (S127) without a visual seam. Same
// inline-#define convention every other Phone* screen .cpp uses.
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream readout
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple unfocused caption
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (cursor + HOME)
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple (unused border)

// ---------- offline zone table ----------------------------------------
//
// Six fixed zones, ordered to read naturally L->R, top->bottom. The
// offsets are signed minutes from a fictional "UTC" used purely as the
// arithmetic anchor -- the screen never claims any of these are the
// "real" time, only that the deltas between zones are correct. We keep
// minutes (not hours) so future sessions can drop in IST (+5:30) or
// NPT (+5:45) without changing the data shape.
//
// Default HOME slot is index 0 (LON), so a brand-new boot opens with
// London showing the device's PhoneClock as-is and the other five
// zones offset relative to it.
static constexpr PhoneWorldClock::Zone kZones[PhoneWorldClock::ZoneCount] = {
		{ "LON",     0 * 60 },   // London (UK), no DST handling
		{ "NYC",    -5 * 60 },   // New York
		{ "LAX",    -8 * 60 },   // Los Angeles
		{ "TYO",     9 * 60 },   // Tokyo
		{ "SYD",    10 * 60 },   // Sydney (base offset, no DST)
		{ "DXB",     4 * 60 },   // Dubai
};

// ---------- geometry --------------------------------------------------
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar (signal + clock + battery)
//   y=12..20   "WORLD CLOCK" caption (pixelbasic7, cyan)
//   y=24..52   row 0 (caption y=26, time y=36)
//   y=54..82   row 1 (caption y=56, time y=66)
//   y=84..112  row 2 (caption y=86, time y=96)
//   y=118..128 PhoneSoftKeyBar
//
// 2-column layout: cells are 80 px wide each. Inside a cell we leave
// a 6 px left/right margin so the captions never bleed into the
// neighbouring column. The bottom row's time label ends at y=112,
// leaving a 6 px gap above the soft-key bar -- same vertical breathing
// room PhoneCalculator and PhoneCurrencyConverter use.

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kCellW         = 80;
static constexpr lv_coord_t kCellInnerMrg  = 6;
static constexpr lv_coord_t kCellInnerW    = kCellW - 2 * kCellInnerMrg;

static constexpr lv_coord_t kRow0CaptionY  = 26;
static constexpr lv_coord_t kRow0TimeY     = 36;
static constexpr lv_coord_t kRow1CaptionY  = 56;
static constexpr lv_coord_t kRow1TimeY     = 66;
static constexpr lv_coord_t kRow2CaptionY  = 86;
static constexpr lv_coord_t kRow2TimeY     = 96;

// Translate (col, row) into pixel offsets. Centralised so a future
// layout change (e.g. a 3x2 grid) only edits this single helper.
static inline lv_coord_t cellX(uint8_t col) {
	return col * kCellW + kCellInnerMrg;
}
static inline lv_coord_t captionYFor(uint8_t row) {
	switch(row) {
		case 0:  return kRow0CaptionY;
		case 1:  return kRow1CaptionY;
		case 2:  return kRow2CaptionY;
		default: return kRow0CaptionY;
	}
}
static inline lv_coord_t timeYFor(uint8_t row) {
	switch(row) {
		case 0:  return kRow0TimeY;
		case 1:  return kRow1TimeY;
		case 2:  return kRow2TimeY;
		default: return kRow0TimeY;
	}
}

// ---------- public statics --------------------------------------------

const PhoneWorldClock::Zone* PhoneWorldClock::zoneAt(uint8_t idx) {
	if(idx >= ZoneCount) return nullptr;
	return &kZones[idx];
}

void PhoneWorldClock::timeOfDay(uint32_t epoch, int32_t delta,
                                uint8_t& hourOut, uint8_t& minOut) {
	// Apply the offset to a 64-bit signed accumulator so the bias
	// step below cannot overflow even at the extreme corners (epoch
	// near 2**32, delta of -12*60 minutes). The bias keeps the result
	// non-negative before the modulo lands it in [0, 86400).
	const int64_t epochS    = static_cast<int64_t>(epoch);
	const int64_t deltaS    = static_cast<int64_t>(delta) * 60LL;
	const int64_t shifted   = epochS + deltaS;

	// Add a generous multiple of 86400 (one week) before the modulo
	// so even a pathological negative delta lands positive. 86400 * 7
	// = 604800; nothing in the zone table approaches a week of offset,
	// so this is comfortably oversized.
	const int64_t biased    = shifted + (int64_t)(86400) * 7;
	const int64_t timeOfDay = biased - (biased / 86400) * 86400;

	const uint32_t tod = static_cast<uint32_t>(timeOfDay);
	hourOut = static_cast<uint8_t>(tod / 3600U);
	minOut  = static_cast<uint8_t>((tod % 3600U) / 60U);
}

// ---------- ctor / dtor -----------------------------------------------

PhoneWorldClock::PhoneWorldClock()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr) {

	for(uint8_t i = 0; i < ZoneCount; ++i) {
		cellCaption[i] = nullptr;
		cellTime[i]    = nullptr;
	}

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern as every other Phone* utility screen.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, 160);
	lv_obj_set_pos(captionLabel, 0, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "WORLD CLOCK");

	buildCells();

	// Bottom soft-key bar; populated by refreshSoftKeys().
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("HOME");
	softKeys->setRight("BACK");
}

PhoneWorldClock::~PhoneWorldClock() {
	stopTickTimer();
}

// ---------- build helpers ---------------------------------------------

void PhoneWorldClock::buildCells() {
	// Two-pass build keeps the LVGL z-order stable: first all the
	// pixelbasic7 city captions, then all the pixelbasic16 readouts.
	// The contents are filled in by refreshCells() once the cursor /
	// home indices and the live PhoneClock have been queried.
	for(uint8_t i = 0; i < ZoneCount; ++i) {
		const uint8_t col = i % Cols;
		const uint8_t row = i / Cols;

		lv_obj_t* caption = lv_label_create(obj);
		lv_obj_set_style_text_font(caption, &pixelbasic7, 0);
		lv_obj_set_style_text_color(caption, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
		lv_obj_set_width(caption, kCellInnerW);
		lv_obj_set_pos(caption, cellX(col), captionYFor(row));
		lv_label_set_text(caption, "");
		cellCaption[i] = caption;

		lv_obj_t* timeLbl = lv_label_create(obj);
		lv_obj_set_style_text_font(timeLbl, &pixelbasic16, 0);
		lv_obj_set_style_text_color(timeLbl, MP_TEXT, 0);
		lv_obj_set_width(timeLbl, kCellInnerW);
		lv_obj_set_pos(timeLbl, cellX(col), timeYFor(row));
		lv_label_set_text(timeLbl, "--:--");
		cellTime[i] = timeLbl;
	}
}

// ---------- lifecycle -------------------------------------------------

void PhoneWorldClock::onStart() {
	Input::getInstance()->addListener(this);
	refreshAll();
	startTickTimer();
}

void PhoneWorldClock::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ---------- repaint ---------------------------------------------------

void PhoneWorldClock::refreshAll() {
	refreshCells();
	refreshSoftKeys();
}

void PhoneWorldClock::refreshCells() {
	const Zone* home = zoneAt(homeIdx);
	if(home == nullptr) return;

	const uint32_t epoch = PhoneClock::nowEpoch();

	for(uint8_t i = 0; i < ZoneCount; ++i) {
		const Zone* z = zoneAt(i);
		if(z == nullptr) continue;
		if(cellCaption[i] == nullptr || cellTime[i] == nullptr) continue;

		// Caption: "CODE HOME" for the home cell, "CODE +H" / "CODE -H"
		// for the rest. The offset is shown in whole hours (deltas
		// between any default pair of zones lands on a clean hour --
		// future half-hour zones can extend this format). The whole
		// caption fits comfortably in the 68 px inner cell width.
		char captionBuf[20];
		if(i == homeIdx) {
			snprintf(captionBuf, sizeof(captionBuf), "%s HOME", z->code);
		} else {
			const int32_t deltaMin = static_cast<int32_t>(z->offsetMin)
			                       - static_cast<int32_t>(home->offsetMin);
			const int32_t deltaHr  = deltaMin / 60;
			if(deltaHr >= 0) {
				snprintf(captionBuf, sizeof(captionBuf),
				         "%s +%ld", z->code, (long)deltaHr);
			} else {
				snprintf(captionBuf, sizeof(captionBuf),
				         "%s %ld", z->code, (long)deltaHr);
			}
		}
		lv_label_set_text(cellCaption[i], captionBuf);

		// Caption colour: HOME = sunset orange, cursor = cyan,
		// otherwise = dim purple. The cursor takes precedence over
		// HOME so the user never loses sight of where the keypad will
		// land its next "HOME" press.
		lv_color_t capCol;
		if(i == cursor)        capCol = MP_HIGHLIGHT;
		else if(i == homeIdx)  capCol = MP_ACCENT;
		else                   capCol = MP_LABEL_DIM;
		lv_obj_set_style_text_color(cellCaption[i], capCol, 0);

		// Time: pull a fresh HH:MM from PhoneClock for every cell.
		// Cheap (epoch is read once above and re-used).
		uint8_t hh = 0, mm = 0;
		const int32_t deltaMin = static_cast<int32_t>(z->offsetMin)
		                       - static_cast<int32_t>(home->offsetMin);
		timeOfDay(epoch, deltaMin, hh, mm);
		char timeBuf[8];
		snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u",
		         (unsigned)hh, (unsigned)mm);
		lv_label_set_text(cellTime[i], timeBuf);

		// Time colour mirrors the caption logic but without the
		// HOME-vs-cursor split: the cursor cell's time renders cyan,
		// every other cell's time renders cream. That keeps the cursor
		// unambiguous even when the user has parked it on the HOME
		// cell.
		lv_color_t timeCol;
		if(i == cursor) timeCol = MP_HIGHLIGHT;
		else            timeCol = MP_TEXT;
		lv_obj_set_style_text_color(cellTime[i], timeCol, 0);
	}
}

void PhoneWorldClock::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	// Left softkey is always "HOME" because the cursor can always
	// promote the focused cell -- even if it is already home, the
	// op is a harmless no-op. Right is always "BACK" because the
	// screen has only the one navigation route out.
	softKeys->setLeft("HOME");
	softKeys->setRight("BACK");
}

// ---------- cursor + home ---------------------------------------------

void PhoneWorldClock::moveCursor(int8_t deltaCol, int8_t deltaRow) {
	int8_t col = static_cast<int8_t>(cursor % Cols) + deltaCol;
	int8_t row = static_cast<int8_t>(cursor / Cols) + deltaRow;

	// Wrap inside [0, Cols) and [0, Rows). The +Cols / +Rows bias
	// keeps a -1 step from underflowing under signed arithmetic.
	col = static_cast<int8_t>((col + (int8_t)Cols) % (int8_t)Cols);
	row = static_cast<int8_t>((row + (int8_t)Rows) % (int8_t)Rows);

	const uint8_t newCursor = static_cast<uint8_t>(row) * Cols
	                        + static_cast<uint8_t>(col);
	if(newCursor == cursor) return;
	cursor = newCursor;
	refreshCells();
}

void PhoneWorldClock::promoteHome() {
	if(cursor == homeIdx) return;   // already home, nothing to do
	homeIdx = cursor;
	refreshAll();
}

// ---------- LVGL timers -----------------------------------------------

void PhoneWorldClock::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneWorldClock::onTickStatic,
	                            TickPeriodMs, this);
}

void PhoneWorldClock::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneWorldClock::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneWorldClock*>(timer->user_data);
	if(self == nullptr) return;
	// Cheap: rebuild the six readouts from a single PhoneClock read.
	// Skips the soft-key refresh because it never changes from the
	// tick alone (HOME / BACK are static for the screen's lifetime).
	self->refreshCells();
}

// ---------- input -----------------------------------------------------

void PhoneWorldClock::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			moveCursor(-1, 0);
			break;

		case BTN_RIGHT:
		case BTN_6:
			moveCursor(+1, 0);
			break;

		case BTN_2:
			moveCursor(0, -1);
			break;
		case BTN_8:
			moveCursor(0, +1);
			break;

		case BTN_L:
			// Left bumper = same as the left softkey ("HOME") -- the
			// PhoneCalculator / PhoneCurrencyConverter family pairs
			// the bumper with the left softkey so the muscle memory
			// carries.
			if(softKeys) softKeys->flashLeft();
			promoteHome();
			break;

		case BTN_5:
		case BTN_ENTER:
			// Centre dialer key + ENTER both promote the cursor cell
			// to HOME. ENTER also flashes the softkey so the user
			// gets the same visual feedback regardless of which key
			// they pressed.
			if(softKeys) softKeys->flashLeft();
			promoteHome();
			break;

		case BTN_R:
			// Right bumper = same as the right softkey ("BACK"). The
			// short-press fire happens here directly (no long-press
			// distinction is needed on this screen).
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneWorldClock::buttonReleased(uint i) {
	switch(i) {
		case BTN_BACK:
			if(!backLongFired) {
				pop();
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneWorldClock::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK is the same as a short tap -- exit the
			// screen. The flag suppresses the matching short-press
			// fire-back on release.
			backLongFired = true;
			pop();
			break;

		default:
			break;
	}
}
