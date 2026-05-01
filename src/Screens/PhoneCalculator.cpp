#include "PhoneCalculator.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <cmath>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the calculator slots in beside PhoneDialerScreen / PhoneAboutScreen / etc.
// without a visual seam. Inlined per the established pattern (see
// PhoneAboutScreen.cpp / PhoneDialerScreen.cpp).
#define MP_BG_DARK         lv_color_make(20, 12, 36)     // deep purple cell body
#define MP_ACCENT          lv_color_make(255, 140, 30)   // sunset orange (operator labels)
#define MP_ACCENT_BRIGHT   lv_color_make(255, 200, 80)   // bright sunset (press flash)
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan (history caption)
#define MP_DIM             lv_color_make(70, 56, 100)    // muted purple (idle borders)
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream (digits + result)
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple (history value)

// ---------- geometry -------------------------------------------------------

// Display strip lives directly under the 10 px PhoneStatusBar. The history
// caption (small, dim) sits on top of the result row, both right-aligned so
// numbers grow from the right just like every Sony-Ericsson calc shipped.
static constexpr lv_coord_t kHistoryY     = 12;
static constexpr lv_coord_t kHistoryH     = 9;
static constexpr lv_coord_t kResultY      = 22;
static constexpr lv_coord_t kResultH      = 16;
static constexpr lv_coord_t kDisplayLeft  = 6;
static constexpr lv_coord_t kDisplayRight = 154;
static constexpr lv_coord_t kDisplayWidth = kDisplayRight - kDisplayLeft;

// 4x4 button grid. Cells are 32x16 -- smaller than the 36x20 PhoneDialerKey
// because we need 4 rows to fit between the 38 px display strip and the
// 10 px PhoneSoftKeyBar (i.e. 80 px of vertical room: 4 * 16 + 3 * 2 = 70 px,
// with a 4 px breathing gap above the softkey bar).
static constexpr uint8_t   kGridCols   = 4;
static constexpr uint8_t   kGridRows   = 4;
static constexpr lv_coord_t kCellW     = 32;
static constexpr lv_coord_t kCellH     = 16;
static constexpr lv_coord_t kGapX      = 2;
static constexpr lv_coord_t kGapY      = 2;
static constexpr lv_coord_t kGridY     = 44;
static constexpr lv_coord_t kGridX     = (160 - (kGridCols * kCellW + (kGridCols - 1) * kGapX)) / 2;

// Visual layout: 16 cells, row-major. See PhoneCalculator::cellGlyph.
//   Row 0:  7 8 9 /
//   Row 1:  4 5 6 x
//   Row 2:  1 2 3 -
//   Row 3:  C 0 = +
static constexpr char kCellLayout[PhoneCalculator::CellCount] = {
		'7', '8', '9', '/',
		'4', '5', '6', 'x',
		'1', '2', '3', '-',
		'C', '0', '=', '+'
};

PhoneCalculator::PhoneCalculator()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  historyLabel(nullptr),
		  resultLabel(nullptr) {

	// Zero the entry buffer up front so refreshDisplay() can read it
	// safely before the user types anything (an empty buffer must
	// render as "0" -- see refreshDisplay()).
	entry[0] = '\0';

	// Full-screen container with no scrollbars, no padding -- same blank
	// canvas pattern PhoneAboutScreen / PhoneDialerScreen use. Children
	// below either pin themselves with IGNORE_LAYOUT or are LVGL
	// primitives that we anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The
	// keypad cells, status bar, soft-keys and display labels all overlay
	// it without any opacity gymnastics. Same z-order pattern every
	// other Phone* screen uses.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	// Display strip + the 4x4 keypad grid.
	buildDisplayStrip();
	buildKeypad();

	// Bottom soft-key bar. "OP" on the left mirrors the BTN_L bumper's
	// staged-operator cycle so the user has two keys for the same
	// action (left softkey OR L bumper). "BACK" on the right is the
	// standard back-out softkey -- a SHORT press doubles as backspace
	// on the entry buffer and a LONG press exits the screen.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("OP");
	softKeys->setRight("BACK");

	// Long-press detection on BTN_BACK so a hold clears all (AC) and a
	// short press is interpreted as backspace. Same 600 ms threshold
	// the rest of the MAKERphone shell uses, so the gesture feels
	// identical from any screen.
	setButtonHoldTime(BTN_BACK, BackHoldMs);
	setButtonHoldTime(BTN_RIGHT, BackHoldMs);

	// Initial paint -- show "0" rather than a blank panel.
	refreshDisplay();
}

PhoneCalculator::~PhoneCalculator() {
	if(flashTimer != nullptr) {
		lv_timer_del(flashTimer);
		flashTimer = nullptr;
	}
	// All other children (wallpaper, statusBar, softKeys, labels, cells)
	// are parented to obj and freed by the LVScreen base destructor.
}

void PhoneCalculator::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneCalculator::onStop() {
	Input::getInstance()->removeListener(this);
	// Cancel any in-flight flash so a freshly-pushed instance does not
	// inherit a flash timer pointed at a previous lifetime's cell.
	if(flashTimer != nullptr) {
		lv_timer_del(flashTimer);
		flashTimer = nullptr;
		if(flashCellIndex >= 0) restoreCellBorder(flashCellIndex);
		flashCellIndex = -1;
	}
}

// ---------- builders ------------------------------------------------------

void PhoneCalculator::buildDisplayStrip() {
	// Small history caption ("12 +") in dim purple, anchored top-right
	// of the display strip so growing numbers always read from the right.
	historyLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(historyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(historyLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(historyLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(historyLabel, kDisplayWidth);
	lv_obj_set_style_text_align(historyLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(historyLabel, "");
	lv_obj_set_pos(historyLabel, kDisplayLeft, kHistoryY);

	// Big result line in pixelbasic16, cream, right-aligned. Wide enough
	// to take the whole display strip so a long number scrolls into the
	// dot-mode ellipsis rather than overflowing the screen.
	resultLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(resultLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(resultLabel, MP_TEXT, 0);
	lv_label_set_long_mode(resultLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(resultLabel, kDisplayWidth);
	lv_obj_set_style_text_align(resultLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(resultLabel, "0");
	lv_obj_set_pos(resultLabel, kDisplayLeft, kResultY);
}

void PhoneCalculator::buildKeypad() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		const uint8_t col = i % kGridCols;
		const uint8_t row = i / kGridCols;
		const lv_coord_t x = kGridX + col * (kCellW + kGapX);
		const lv_coord_t y = kGridY + row * (kCellH + kGapY);

		lv_obj_t* cell = lv_obj_create(obj);
		lv_obj_remove_style_all(cell);
		lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(cell, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(cell, kCellW, kCellH);
		lv_obj_set_pos(cell, x, y);
		lv_obj_set_style_bg_color(cell, MP_BG_DARK, 0);
		lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(cell, 2, 0);
		lv_obj_set_style_pad_all(cell, 0, 0);
		lv_obj_set_style_border_color(cell, MP_DIM, 0);
		lv_obj_set_style_border_width(cell, 1, 0);
		lv_obj_set_style_border_opa(cell, LV_OPA_COVER, 0);

		// Label sits dead-centre in the cell. Operators (/, x, -, +) use
		// MP_ACCENT so they read as "function" keys; the special "C" and
		// "=" cells use MP_HIGHLIGHT to call out the global actions, and
		// plain digits use MP_TEXT so the keypad reads at a glance.
		const char glyph = kCellLayout[i];
		const bool isOperator =
				glyph == '/' || glyph == 'x' || glyph == '-' || glyph == '+';
		const bool isSpecial = glyph == 'C' || glyph == '=';

		lv_obj_t* lab = lv_label_create(cell);
		lv_obj_set_style_text_font(lab, &pixelbasic7, 0);
		lv_color_t labColor = MP_TEXT;
		if(isOperator)      labColor = MP_ACCENT;
		else if(isSpecial)  labColor = MP_HIGHLIGHT;
		lv_obj_set_style_text_color(lab, labColor, 0);

		// Render '/' as 'DIV' would be too wide; keep it as a single
		// slash. Likewise 'x' is the multiply glyph, '-' is minus, '+'
		// is plus -- simple ASCII so the pixelbasic7 font has every
		// character we need without any unicode tables.
		char buf[2] = { glyph, '\0' };
		lv_label_set_text(lab, buf);
		lv_obj_set_align(lab, LV_ALIGN_CENTER);

		cells[i]      = cell;
		cellLabels[i] = lab;
	}
}

// ---------- glyph helpers -------------------------------------------------

char PhoneCalculator::cellGlyph(uint8_t index) {
	if(index >= CellCount) return '\0';
	return kCellLayout[index];
}

int8_t PhoneCalculator::findCellIndex(char c) const {
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(kCellLayout[i] == c) return (int8_t) i;
	}
	return -1;
}

// ---------- math ----------------------------------------------------------

double PhoneCalculator::applyOp(double a, double b, char op, bool& outErr) {
	outErr = false;
	switch(op) {
		case '+': return a + b;
		case '-': return a - b;
		case 'x': return a * b;
		case '/':
			// We trip the error latch on a literal zero divisor rather
			// than relying on IEEE-754 inf/nan because the screen would
			// otherwise paint "inf" or "nan" through %g, which is not
			// the kind of feedback a feature-phone calc gives.
			if(b == 0.0) { outErr = true; return 0.0; }
			return a / b;
		default:
			outErr = true;
			return 0.0;
	}
}

double PhoneCalculator::parseEntry() const {
	if(entryLen == 0) return 0.0;
	return strtod(entry, nullptr);
}

// ---------- state transitions --------------------------------------------

void PhoneCalculator::appendDigit(char c) {
	if(errorState) return;

	// "Just computed" -- next digit press starts a brand new entry on top
	// of the displayed result. The accumulator is also reset so a chain
	// "5 + 3 = | 7" starts the new operand "7" against an empty acc.
	if(justComputed) {
		accumulator  = 0.0;
		accValid     = false;
		pendingOp    = 0;
		entry[0]     = '\0';
		entryLen     = 0;
		justComputed = false;
	}

	if(entryLen >= MaxEntryDigits) return;
	// Reject leading zero loops -- "00" should display as "0", not "00".
	if(entryLen == 1 && entry[0] == '0' && c == '0') return;
	if(entryLen == 1 && entry[0] == '0' && c != '0') {
		entry[0] = c;
		entry[1] = '\0';
		refreshDisplay();
		return;
	}

	entry[entryLen++] = c;
	entry[entryLen]   = '\0';
	refreshDisplay();
}

void PhoneCalculator::backspaceEntry() {
	if(errorState) {
		// First press out of the error latch clears everything so the
		// user does not have to long-press just to recover.
		clearAll();
		return;
	}
	if(justComputed) {
		// Backspace from a freshly-computed result is most useful as a
		// "clear last result" -- pretend the user has not typed anything.
		entry[0]     = '\0';
		entryLen     = 0;
		justComputed = false;
		refreshDisplay();
		return;
	}
	if(entryLen == 0) return;
	entryLen--;
	entry[entryLen] = '\0';
	refreshDisplay();
}

void PhoneCalculator::clearAll() {
	entry[0]     = '\0';
	entryLen     = 0;
	accumulator  = 0.0;
	accValid     = false;
	pendingOp    = 0;
	justComputed = false;
	errorState   = false;
	refreshDisplay();
}

void PhoneCalculator::cycleOperator() {
	if(errorState) return;
	// Operator order: + -> - -> x -> /. Wraps. If no op is staged, start
	// at '+'; otherwise advance from whatever is currently staged.
	static const char order[4] = { '+', '-', 'x', '/' };
	char nextOp = '+';
	if(pendingOp != 0) {
		for(uint8_t i = 0; i < 4; ++i) {
			if(order[i] == pendingOp) {
				nextOp = order[(i + 1) % 4];
				break;
			}
		}
	}
	chooseOperator(nextOp);
}

void PhoneCalculator::chooseOperator(char op) {
	if(errorState) return;

	// If the user has typed an operand, commit it -- either by combining
	// with the running accumulator under the previously-staged op, or by
	// seeding the accumulator if nothing was staged yet. After commit
	// we always clear the entry buffer so the next operand starts fresh.
	if(entryLen > 0) {
		const double rhs = parseEntry();
		if(accValid && pendingOp != 0) {
			bool err = false;
			const double next = applyOp(accumulator, rhs, pendingOp, err);
			if(err) { showError(); return; }
			accumulator = next;
		} else {
			accumulator = rhs;
			accValid    = true;
		}
		entry[0] = '\0';
		entryLen = 0;
	} else if(justComputed) {
		// User pressed '=' then immediately picked a new operator -- carry
		// the last result forward as the new accumulator.
		accValid = true;
	}
	pendingOp    = op;
	justComputed = false;
	refreshDisplay();
}

void PhoneCalculator::computeEquals() {
	if(errorState) return;

	// Equals only makes sense if there is at least an entry to evaluate
	// (or a running accumulator + staged op). Pressing '=' with nothing
	// typed and no op staged is a no-op: the screen still shows the
	// last result, the user gets a softkey flash from the caller.
	if(entryLen == 0 && pendingOp == 0) return;

	double rhs = entryLen > 0 ? parseEntry() : accumulator;
	if(pendingOp != 0 && accValid) {
		bool err = false;
		const double next = applyOp(accumulator, rhs, pendingOp, err);
		if(err) { showError(); return; }
		accumulator = next;
	} else {
		accumulator = rhs;
	}
	accValid     = true;
	pendingOp    = 0;
	entry[0]     = '\0';
	entryLen     = 0;
	justComputed = true;
	refreshDisplay();
}

void PhoneCalculator::showError() {
	errorState = true;
	if(resultLabel)  lv_label_set_text(resultLabel, "ERROR");
	if(historyLabel) lv_label_set_text(historyLabel, "");
}

// ---------- formatting ----------------------------------------------------

void PhoneCalculator::formatDisplay(double value, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;

	// Reject non-finite values up front -- they would otherwise print as
	// "inf" / "nan" through %g, which is not the kind of feedback a
	// feature-phone calc would give. The caller paints "ERROR" instead.
	if(!std::isfinite(value)) {
		strncpy(out, "ERROR", outLen);
		out[outLen - 1] = '\0';
		return;
	}

	// Round the value to a sensible decimal grid before formatting -- a
	// raw double like 0.1 + 0.2 would otherwise print as "0.3" via %g
	// (rounding 0.30000000000000004) but a calc user would still expect
	// "0.3". %.10g picks 10 significant digits, the same ceiling we cap
	// the entry length at, so the worst case is exactly "1234567890".
	char tmp[24];
	snprintf(tmp, sizeof(tmp), "%.10g", value);

	// %g already strips trailing zeros, but a value like 1e+30 or
	// -3.1415927 might still exceed the 11-char display column. If so,
	// try a tighter precision; if still too wide, fall back to "ERROR"
	// (the calc cannot honestly display the value).
	if(strlen(tmp) > MaxDisplayChars) {
		snprintf(tmp, sizeof(tmp), "%.6g", value);
	}
	if(strlen(tmp) > MaxDisplayChars) {
		snprintf(tmp, sizeof(tmp), "%.3g", value);
	}
	if(strlen(tmp) > MaxDisplayChars) {
		strncpy(out, "ERROR", outLen);
		out[outLen - 1] = '\0';
		return;
	}

	strncpy(out, tmp, outLen);
	out[outLen - 1] = '\0';
}

// ---------- repaint -------------------------------------------------------

void PhoneCalculator::refreshDisplay() {
	if(errorState) {
		if(resultLabel)  lv_label_set_text(resultLabel, "ERROR");
		if(historyLabel) lv_label_set_text(historyLabel, "");
		return;
	}

	// Result row: typed entry takes priority; otherwise the running
	// accumulator (post-=); otherwise a literal "0".
	char buf[24];
	if(entryLen > 0) {
		strncpy(buf, entry, sizeof(buf));
		buf[sizeof(buf) - 1] = '\0';
	} else if(accValid || justComputed) {
		formatDisplay(accumulator, buf, sizeof(buf));
	} else {
		strncpy(buf, "0", sizeof(buf));
	}
	if(resultLabel) lv_label_set_text(resultLabel, buf);

	// History row: "<acc> <op>" while an operator is staged and the
	// running acc is meaningful; "<acc> =" right after equals so the
	// user sees what they just resolved; empty otherwise.
	char history[24];
	history[0] = '\0';
	if(pendingOp != 0 && accValid) {
		char accBuf[24];
		formatDisplay(accumulator, accBuf, sizeof(accBuf));
		// pendingOp is one of '+', '-', 'x', '/'.
		snprintf(history, sizeof(history), "%s %c", accBuf, pendingOp);
	}
	if(historyLabel) lv_label_set_text(historyLabel, history);
}

// ---------- press flash --------------------------------------------------

void PhoneCalculator::flashCell(int8_t index) {
	if(index < 0 || index >= (int8_t) CellCount) return;
	if(cells[index] == nullptr) return;

	// Cancel a still-running flash on a different cell so it does not
	// expire and reset our newly-flashed cell's border. The previous
	// cell snaps back to MP_DIM the moment the new flash starts.
	if(flashTimer != nullptr) {
		lv_timer_del(flashTimer);
		flashTimer = nullptr;
		if(flashCellIndex >= 0 && flashCellIndex != index) {
			restoreCellBorder(flashCellIndex);
		}
	}

	flashCellIndex = index;
	lv_obj_set_style_border_color(cells[index], MP_ACCENT_BRIGHT, 0);

	flashTimer = lv_timer_create(onFlashTimer, FlashDurationMs, this);
	lv_timer_set_repeat_count(flashTimer, 1);
}

void PhoneCalculator::restoreCellBorder(int8_t index) {
	if(index < 0 || index >= (int8_t) CellCount) return;
	if(cells[index] == nullptr) return;
	lv_obj_set_style_border_color(cells[index], MP_DIM, 0);
}

void PhoneCalculator::onFlashTimer(lv_timer_t* timer) {
	auto* self = static_cast<PhoneCalculator*>(timer->user_data);
	if(self == nullptr) return;
	const int8_t idx = self->flashCellIndex;
	self->flashTimer     = nullptr;
	self->flashCellIndex = -1;
	if(idx >= 0) self->restoreCellBorder(idx);
}

// ---------- input ---------------------------------------------------------

void PhoneCalculator::buttonPressed(uint i) {
	switch(i) {
		case BTN_0: appendDigit('0'); flashCell(findCellIndex('0')); break;
		case BTN_1: appendDigit('1'); flashCell(findCellIndex('1')); break;
		case BTN_2: appendDigit('2'); flashCell(findCellIndex('2')); break;
		case BTN_3: appendDigit('3'); flashCell(findCellIndex('3')); break;
		case BTN_4: appendDigit('4'); flashCell(findCellIndex('4')); break;
		case BTN_5: appendDigit('5'); flashCell(findCellIndex('5')); break;
		case BTN_6: appendDigit('6'); flashCell(findCellIndex('6')); break;
		case BTN_7: appendDigit('7'); flashCell(findCellIndex('7')); break;
		case BTN_8: appendDigit('8'); flashCell(findCellIndex('8')); break;
		case BTN_9: appendDigit('9'); flashCell(findCellIndex('9')); break;

		case BTN_L:
			// L bumper cycles the staged operator forward. Flash the
			// matching cell on the right column so the user sees which
			// operator is now staged.
			cycleOperator();
			if(pendingOp != 0) flashCell(findCellIndex(pendingOp));
			break;

		case BTN_R:
			// R bumper = equals. Flash the '=' cell so direct-key input
			// gives the same visual cue as picking '=' on a cursor pad.
			computeEquals();
			flashCell(findCellIndex('='));
			break;

		case BTN_ENTER:
			// Friendlier alias for BTN_R -- the Chatter's centre A button
			// is the muscle-memory "confirm" on every other Phone* screen,
			// so a calc user would expect ENTER to commit the result too.
			computeEquals();
			flashCell(findCellIndex('='));
			break;

		case BTN_LEFT:
			// Left softkey == "OP" cycle. Same action as BTN_L.
			if(softKeys) softKeys->flashLeft();
			cycleOperator();
			if(pendingOp != 0) flashCell(findCellIndex(pendingOp));
			break;

		case BTN_RIGHT:
			// Right softkey == "BACK" -- short = backspace, long = exit.
			// Defer the actual short-press action to buttonReleased so a
			// long-press exit does not double-fire on key release.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		case BTN_BACK:
			// Hardware BACK button -- same dual short/long behaviour as
			// the right softkey, just routed through the dedicated key.
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneCalculator::buttonReleased(uint i) {
	switch(i) {
		case BTN_RIGHT:
		case BTN_BACK:
			if(!backLongFired) {
				if(errorState || entryLen > 0 || justComputed) {
					// Something on screen to backspace away.
					backspaceEntry();
					flashCell(findCellIndex('C'));
				} else if(accValid || pendingOp != 0) {
					// Empty entry, but state is non-trivial -- short
					// press resets to "0" rather than exiting outright,
					// matching how every Sony-Ericsson basic calc
					// "C" button worked. Flashes the C cell for cue.
					clearAll();
					flashCell(findCellIndex('C'));
				} else {
					// Pristine state + short BACK -- exit the screen.
					pop();
				}
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneCalculator::buttonHeld(uint i) {
	switch(i) {
		case BTN_RIGHT:
		case BTN_BACK:
			// Long-press: clear-all once. A second long-press from a
			// pristine state pops the screen, matching the dialer's
			// "hold to exit" muscle memory.
			backLongFired = true;
			if(softKeys) softKeys->flashRight();
			if(entryLen == 0 && !accValid && pendingOp == 0
			   && !errorState && !justComputed) {
				pop();
			} else {
				clearAll();
				flashCell(findCellIndex('C'));
			}
			break;

		default:
			break;
	}
}
