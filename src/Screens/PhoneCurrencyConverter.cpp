#include "PhoneCurrencyConverter.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the converter slots in beside PhoneCalculator (S60), PhoneAlarmClock (S124),
// PhoneTimers (S125) without a visual seam. Inlined per the established
// pattern (see PhoneCalculator.cpp / PhoneTimers.cpp).
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption / active code
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream amount
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple side caption / hint
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (active marker)
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple (unused border tint)

// ---------- offline rate table ------------------------------------------
//
// Plausible mid-2020s mid-rates expressed as "units of THIS currency per
// 1 USD". The list is deliberately short (12 picks) so cycling through
// the active currency stays fast on the keypad and the table fits in
// rodata with zero alloc traffic.
//
// The firmware has no network path, so this is offline / approximate;
// a user who needs a precise rate can update the table in source. The
// per-USD baseline keeps the ratio math symmetric: convert(a, src, dst)
// = a * (perUSD[dst] / perUSD[src]).
//
// Indices are stable for the lifetime of the build -- buildContent()
// stores fromIdx / toIdx as plain uint8_t into this table, so re-ordering
// the table during a release would silently flip a user's saved picks.
// (We do not currently persist them; a future S133/S159-style profile
// might.)
static constexpr PhoneCurrencyConverter::Currency kCurrencies[] = {
		{ "USD",   1.00 },
		{ "EUR",   0.92 },
		{ "GBP",   0.79 },
		{ "JPY", 153.00 },
		{ "CHF",   0.88 },
		{ "CAD",   1.36 },
		{ "AUD",   1.51 },
		{ "CNY",   7.25 },
		{ "INR",  83.10 },
		{ "MXN",  17.20 },
		{ "BRL",   5.05 },
		{ "HKD",   7.82 },
};
static constexpr uint8_t kCurrencyCount =
		sizeof(kCurrencies) / sizeof(kCurrencies[0]);

// ---------- geometry ----------------------------------------------------
//
// The 160x128 display gives us 118 px between the 10 px PhoneStatusBar at
// y=0 and the 10 px PhoneSoftKeyBar pinned at y=118. Within that band:
//
//   y=12   caption         "CURRENCY"            pixelbasic7 cyan
//   y=24   FROM caption    "FROM" / "FROM >"     pixelbasic7 dim
//   y=34   FROM code+amt   "USD"      "100"      pixelbasic16
//   y=58   TO   caption    "TO"   / "TO >"       pixelbasic7 dim
//   y=68   TO   code+amt   "EUR"      "92"       pixelbasic16
//   y=92   hint            "L/R chg  ENT swap"   pixelbasic7 dim
//
// pixelbasic16 rows are 16 px tall; the 24 px column gap (34->58, 68->92)
// gives breathing room around each row without any per-row dividers.
static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kFromCaptionY  = 24;
static constexpr lv_coord_t kFromRowY      = 34;
static constexpr lv_coord_t kToCaptionY    = 58;
static constexpr lv_coord_t kToRowY        = 68;
static constexpr lv_coord_t kHintY         = 92;

static constexpr lv_coord_t kSideLeft      = 6;
static constexpr lv_coord_t kSideRight     = 154;
static constexpr lv_coord_t kSideWidth     = kSideRight - kSideLeft;
static constexpr lv_coord_t kCodeLeft      = 6;
static constexpr lv_coord_t kCodeWidth     = 50;
static constexpr lv_coord_t kAmountLeft    = 60;
static constexpr lv_coord_t kAmountWidth   = 96;

// ---------- public statics ----------------------------------------------

uint8_t PhoneCurrencyConverter::currencyCount() {
	return kCurrencyCount;
}

const PhoneCurrencyConverter::Currency*
PhoneCurrencyConverter::currencyAt(uint8_t idx) {
	if(idx >= kCurrencyCount) return nullptr;
	return &kCurrencies[idx];
}

double PhoneCurrencyConverter::convert(double amount,
                                       uint8_t srcIdx,
                                       uint8_t dstIdx) {
	if(srcIdx >= kCurrencyCount) return 0.0;
	if(dstIdx >= kCurrencyCount) return 0.0;
	const double srcPerUSD = kCurrencies[srcIdx].perUSD;
	const double dstPerUSD = kCurrencies[dstIdx].perUSD;
	if(srcPerUSD == 0.0) return 0.0;
	// Identity short-circuit so a user converting USD->USD (or any
	// same-pair) sees the typed amount echoed back exactly, with no
	// floating-point round-trip noise from the * and / pair.
	if(srcIdx == dstIdx) return amount;
	return amount * (dstPerUSD / srcPerUSD);
}

void PhoneCurrencyConverter::formatAmount(double value,
                                          char* out,
                                          size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(outLen < 2) {
		out[0] = '\0';
		return;
	}

	// Negative inputs cannot come from typed entry (no minus key) but
	// could come from a future call site; guard the sign so the print
	// path is uniform.
	const bool negative = value < 0.0;
	double v = negative ? -value : value;

	// Bound the magnitude so we never spill the printf buffer with an
	// overflow exponent ("1e+12" etc.). The display column is 8 chars
	// wide, so anything beyond ~99,999,999 is not useful as a number
	// the user can read at a glance. Fall back to "----" instead.
	if(!isfinite(value) || v >= 1.0e9) {
		const char* placeholder = "----";
		size_t n = strlen(placeholder);
		if(n >= outLen) n = outLen - 1;
		memcpy(out, placeholder, n);
		out[n] = '\0';
		return;
	}

	char buf[24];
	// %.6g prints up to 6 significant digits, no trailing zeros, no
	// scientific notation for small enough values. For the rate table
	// magnitudes this gives us "100", "92.5", "153", "1.23e+04" only at
	// the very high end -- which we already filtered above by the 1e9
	// guard.
	snprintf(buf, sizeof(buf), "%.6g", v);

	// Strip a trailing decimal point so a whole number prints as "100"
	// not "100." (which can happen if %g picked a fractional precision
	// that rounded to a whole). Belt-and-braces: the formatter we
	// inherited from PhoneCalculator does the same.
	size_t bn = strlen(buf);
	if(bn > 0 && buf[bn - 1] == '.') {
		buf[bn - 1] = '\0';
		bn--;
	}

	// Compose with optional minus.
	char composed[28];
	if(negative) {
		snprintf(composed, sizeof(composed), "-%s", buf);
	} else {
		snprintf(composed, sizeof(composed), "%s", buf);
	}

	size_t n = strlen(composed);
	if(n + 1 > outLen) {
		// Result too long to fit -- caller-bound display column. Fall
		// back to the placeholder rather than truncating digits, which
		// would silently mislead the user.
		const char* placeholder = "----";
		size_t pn = strlen(placeholder);
		if(pn >= outLen) pn = outLen - 1;
		memcpy(out, placeholder, pn);
		out[pn] = '\0';
		return;
	}
	memcpy(out, composed, n + 1);
}

// ---------- ctor / dtor -------------------------------------------------

PhoneCurrencyConverter::PhoneCurrencyConverter()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  fromCaption(nullptr),
		  fromCode(nullptr),
		  fromAmount(nullptr),
		  toCaption(nullptr),
		  toCode(nullptr),
		  toAmount(nullptr),
		  hintLabel(nullptr) {

	// Zero the entry buffer up front so refreshDisplay() can read it
	// safely before the user types anything (an empty buffer must
	// render as "0").
	for(uint8_t i = 0; i <= MaxEntryDigits; ++i) entry[i] = 0;

	// Sensible default pair: FROM=USD (idx 0), TO=EUR (idx 1). The
	// user can cycle from there via L/R bumpers / arrow softkeys.
	fromIdx = 0;
	toIdx   = (kCurrencyCount > 1) ? 1 : 0;

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern PhoneCalculator / PhoneAlarmClock / PhoneTimers
	// use. Children below either pin themselves with IGNORE_LAYOUT or
	// are LVGL primitives that we anchor manually on the 160x128 grid.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The
	// content labels overlay it without any opacity gymnastics. Same
	// z-order pattern every other Phone* screen uses.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildContent();

	// Bottom soft-key bar. LEFT="SWAP" mirrors BTN_ENTER's swap action
	// (ENTER is the muscle-memory "primary" key on the rest of the
	// shell, so a thumb on the bar reaches the same flip). RIGHT is
	// "BACK" by default; refreshSoftKeys() flips it to "DEL" while
	// the entry buffer holds digits so a short BACK pops one.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->set("SWAP", "BACK");

	// Long-press detection on BTN_BACK / BTN_RIGHT so a hold clears
	// the entry and a short press is interpreted as backspace. Same
	// 600 ms threshold the rest of the MAKERphone shell uses.
	setButtonHoldTime(BTN_BACK, BackHoldMs);
	setButtonHoldTime(BTN_RIGHT, BackHoldMs);

	// Initial paint so the user sees the default pair immediately.
	refreshDisplay();
}

PhoneCurrencyConverter::~PhoneCurrencyConverter() {
	// All children (wallpaper, statusBar, softKeys, labels) are parented
	// to obj and freed by the LVScreen base destructor. No timers held.
}

void PhoneCurrencyConverter::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneCurrencyConverter::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- builders ----------------------------------------------------

void PhoneCurrencyConverter::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, kSideWidth);
	lv_obj_set_pos(captionLabel, kSideLeft, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "CURRENCY");
}

void PhoneCurrencyConverter::buildContent() {
	// FROM caption (small, dim, left-aligned). The active marker ">"
	// is appended to the text in refreshDisplay() rather than being a
	// separate widget -- one less node to manage in the z-order.
	fromCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(fromCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(fromCaption, MP_LABEL_DIM, 0);
	lv_obj_set_width(fromCaption, kSideWidth);
	lv_obj_set_pos(fromCaption, kSideLeft, kFromCaptionY);
	lv_obj_set_style_text_align(fromCaption, LV_TEXT_ALIGN_LEFT, 0);
	lv_label_set_text(fromCaption, "FROM");

	// FROM currency code (left-aligned, pixelbasic16). Color depends on
	// active state; refreshDisplay() flips it.
	fromCode = lv_label_create(obj);
	lv_obj_set_style_text_font(fromCode, &pixelbasic16, 0);
	lv_obj_set_style_text_color(fromCode, MP_HIGHLIGHT, 0);
	lv_obj_set_width(fromCode, kCodeWidth);
	lv_obj_set_pos(fromCode, kCodeLeft, kFromRowY);
	lv_obj_set_style_text_align(fromCode, LV_TEXT_ALIGN_LEFT, 0);
	lv_label_set_text(fromCode, "USD");

	// FROM amount (right-aligned, pixelbasic16, cream). LV_LABEL_LONG_DOT
	// so a giant computed result truncates with an ellipsis instead of
	// overflowing the column.
	fromAmount = lv_label_create(obj);
	lv_obj_set_style_text_font(fromAmount, &pixelbasic16, 0);
	lv_obj_set_style_text_color(fromAmount, MP_TEXT, 0);
	lv_label_set_long_mode(fromAmount, LV_LABEL_LONG_DOT);
	lv_obj_set_width(fromAmount, kAmountWidth);
	lv_obj_set_pos(fromAmount, kAmountLeft, kFromRowY);
	lv_obj_set_style_text_align(fromAmount, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(fromAmount, "0");

	// TO caption / code / amount mirror the FROM trio.
	toCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(toCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(toCaption, MP_LABEL_DIM, 0);
	lv_obj_set_width(toCaption, kSideWidth);
	lv_obj_set_pos(toCaption, kSideLeft, kToCaptionY);
	lv_obj_set_style_text_align(toCaption, LV_TEXT_ALIGN_LEFT, 0);
	lv_label_set_text(toCaption, "TO");

	toCode = lv_label_create(obj);
	lv_obj_set_style_text_font(toCode, &pixelbasic16, 0);
	lv_obj_set_style_text_color(toCode, MP_TEXT, 0);
	lv_obj_set_width(toCode, kCodeWidth);
	lv_obj_set_pos(toCode, kCodeLeft, kToRowY);
	lv_obj_set_style_text_align(toCode, LV_TEXT_ALIGN_LEFT, 0);
	lv_label_set_text(toCode, "EUR");

	toAmount = lv_label_create(obj);
	lv_obj_set_style_text_font(toAmount, &pixelbasic16, 0);
	lv_obj_set_style_text_color(toAmount, MP_TEXT, 0);
	lv_label_set_long_mode(toAmount, LV_LABEL_LONG_DOT);
	lv_obj_set_width(toAmount, kAmountWidth);
	lv_obj_set_pos(toAmount, kAmountLeft, kToRowY);
	lv_obj_set_style_text_align(toAmount, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(toAmount, "0");

	// Hint line at the bottom -- explains the non-obvious controls.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(hintLabel, kSideWidth);
	lv_obj_set_pos(hintLabel, kSideLeft, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "L/R chg  ENT swap");
}

// ---------- entry buffer ------------------------------------------------

double PhoneCurrencyConverter::parseEntry() const {
	if(entryLen == 0) return 0.0;
	// strtod ignores any trailing NUL we placed; the buffer is
	// guaranteed null-terminated by appendDigit() and clearEntry().
	return strtod(entry, nullptr);
}

void PhoneCurrencyConverter::seedEntryFromValue(double value) {
	// Drop any sign / fractional digits so the seeded entry is a clean
	// integer string; the user can re-type fractional digits if they
	// want by clearing first. Bound the magnitude so a giant computed
	// value (e.g. 1.5M JPY converted to USD) does not splat thousands of
	// digits into the entry buffer.
	if(!isfinite(value) || value < 0.0) value = 0.0;
	double rounded = floor(value + 0.5);
	if(rounded >= 1.0e7) rounded = 1.0e7 - 1;   // cap at MaxEntryDigits

	uint64_t whole = (uint64_t) rounded;
	// snprintf into a small stack buffer; copy out only what fits.
	char buf[16];
	snprintf(buf, sizeof(buf), "%llu", (unsigned long long) whole);

	uint8_t n = (uint8_t) strlen(buf);
	if(n > MaxEntryDigits) n = MaxEntryDigits;
	memcpy(entry, buf, n);
	entry[n] = '\0';
	entryLen = n;

	// Treat a "0" seed as an empty buffer so the next digit replaces
	// the placeholder rather than appending. Same convention as
	// PhoneCalculator's display.
	if(entryLen == 1 && entry[0] == '0') {
		entry[0] = '\0';
		entryLen = 0;
	}
}

void PhoneCurrencyConverter::appendDigit(char c) {
	if(c < '0' || c > '9') return;
	if(entryLen >= MaxEntryDigits) return;
	// Suppress a leading-zero entry: typing "0" then "0" stays at "0",
	// typing "0" then "5" rolls to "5". Matches every basic calc.
	if(entryLen == 1 && entry[0] == '0' && c == '0') return;
	if(entryLen == 1 && entry[0] == '0' && c != '0') {
		entry[0] = c;
		entry[1] = '\0';
		entryLen = 1;
	} else {
		entry[entryLen]     = c;
		entry[entryLen + 1] = '\0';
		entryLen++;
	}
	refreshDisplay();
}

void PhoneCurrencyConverter::backspaceEntry() {
	if(entryLen == 0) return;
	entryLen--;
	entry[entryLen] = '\0';
	refreshDisplay();
}

void PhoneCurrencyConverter::clearEntry() {
	entry[0] = '\0';
	entryLen = 0;
	refreshDisplay();
}

// ---------- currency cycling -------------------------------------------

void PhoneCurrencyConverter::cycleActiveCurrency(int8_t delta) {
	if(kCurrencyCount == 0) return;
	uint8_t* idx = (active == Side::From) ? &fromIdx : &toIdx;
	int next = (int) *idx + (int) delta;
	// Modulo with proper negative wrap.
	while(next < 0)                next += kCurrencyCount;
	while(next >= kCurrencyCount)  next -= kCurrencyCount;
	*idx = (uint8_t) next;
	refreshDisplay();
}

void PhoneCurrencyConverter::swapSides() {
	// Capture the OTHER side's currently-displayed amount. That value
	// becomes the new active entry so the user keeps a sensible
	// starting number for the just-flipped flow ("show me what 92 EUR
	// is in USD" -- they were just typing 100 USD; swap gives them 92
	// as the new entry on the EUR side).
	const double activeAmount = parseEntry();
	const double otherAmount  = convert(activeAmount, activeIdx(), otherIdx());

	active = (active == Side::From) ? Side::To : Side::From;
	seedEntryFromValue(otherAmount);
	refreshDisplay();
}

// ---------- repaint -----------------------------------------------------

void PhoneCurrencyConverter::refreshDisplay() {
	const Currency* fromC = currencyAt(fromIdx);
	const Currency* toC   = currencyAt(toIdx);
	if(fromC == nullptr || toC == nullptr) return;

	// Side captions get a ">" marker on the active side. Keeping it in
	// the caption text (rather than as a separate cursor sprite) means
	// one less node on each refresh, and the visual reads identically.
	if(active == Side::From) {
		lv_label_set_text(fromCaption, "FROM >");
		lv_label_set_text(toCaption,   "TO");
		lv_obj_set_style_text_color(fromCaption, MP_ACCENT, 0);
		lv_obj_set_style_text_color(toCaption,   MP_LABEL_DIM, 0);
		lv_obj_set_style_text_color(fromCode,    MP_HIGHLIGHT, 0);
		lv_obj_set_style_text_color(toCode,      MP_TEXT, 0);
	} else {
		lv_label_set_text(fromCaption, "FROM");
		lv_label_set_text(toCaption,   "TO >");
		lv_obj_set_style_text_color(fromCaption, MP_LABEL_DIM, 0);
		lv_obj_set_style_text_color(toCaption,   MP_ACCENT, 0);
		lv_obj_set_style_text_color(fromCode,    MP_TEXT, 0);
		lv_obj_set_style_text_color(toCode,      MP_HIGHLIGHT, 0);
	}

	lv_label_set_text(fromCode, fromC->code);
	lv_label_set_text(toCode,   toC->code);

	// Compute both displayed amounts off the active entry. The active
	// side echoes the typed buffer (with "0" placeholder for empty);
	// the other side is computed via convert().
	const double activeAmount = parseEntry();
	const double otherAmount  = convert(activeAmount, activeIdx(), otherIdx());

	char activeBuf[16];
	if(entryLen == 0) {
		// Empty buffer renders as "0" so the column does not look
		// uninhabited before the user types anything.
		strcpy(activeBuf, "0");
	} else {
		// The typed entry is already ASCII digits; just copy it out.
		// Bounded by MaxEntryDigits + 1 so this fits in activeBuf.
		strncpy(activeBuf, entry, sizeof(activeBuf) - 1);
		activeBuf[sizeof(activeBuf) - 1] = '\0';
	}

	char otherBuf[16];
	formatAmount(otherAmount, otherBuf, sizeof(otherBuf));

	if(active == Side::From) {
		lv_label_set_text(fromAmount, activeBuf);
		lv_label_set_text(toAmount,   otherBuf);
	} else {
		lv_label_set_text(fromAmount, otherBuf);
		lv_label_set_text(toAmount,   activeBuf);
	}

	refreshSoftKeys();
}

void PhoneCurrencyConverter::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	// Right softkey: "DEL" while there is something to delete, "BACK"
	// once the entry buffer is empty so the same key cleanly exits.
	// LEFT stays "SWAP" -- the action is always available.
	if(entryLen > 0) {
		softKeys->set("SWAP", "DEL");
	} else {
		softKeys->set("SWAP", "BACK");
	}
}

// ---------- input handlers ---------------------------------------------

void PhoneCurrencyConverter::buttonPressed(uint i) {
	switch(i) {
		case BTN_0: appendDigit('0'); break;
		case BTN_1: appendDigit('1'); break;
		case BTN_2: appendDigit('2'); break;
		case BTN_3: appendDigit('3'); break;
		case BTN_4: appendDigit('4'); break;
		case BTN_5: appendDigit('5'); break;
		case BTN_6: appendDigit('6'); break;
		case BTN_7: appendDigit('7'); break;
		case BTN_8: appendDigit('8'); break;
		case BTN_9: appendDigit('9'); break;

		case BTN_LEFT:
			// Left softkey alias = "SWAP". Flash the bar so the user
			// gets the same tactile cue picking SWAP from the bar that
			// they would picking ENTER from the keypad.
			if(softKeys) softKeys->flashLeft();
			swapSides();
			break;

		case BTN_RIGHT:
			// Right softkey alias = "BACK"/"DEL" -- short = backspace,
			// long = clear, second long = exit. We defer the actual
			// short-press action to buttonReleased() so a long-press
			// exit does not double-fire on key release. Mirrors
			// PhoneCalculator's BTN_RIGHT handler exactly.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		case BTN_L:
			// L bumper cycles the active currency one step *back*.
			// Wraps around the table so the user can spin through it.
			cycleActiveCurrency(-1);
			break;

		case BTN_R:
			// R bumper cycles the active currency one step *forward*.
			cycleActiveCurrency(+1);
			break;

		case BTN_ENTER:
			// SWAP active side. The other side's previously-shown
			// amount becomes the new active entry, so a "100 USD =
			// 92 EUR" flow flips to "92 EUR = 100 USD" intuitively.
			if(softKeys) softKeys->flashLeft();
			swapSides();
			break;

		case BTN_BACK:
			// Hardware BACK -- short=backspace, long=clear-all. Same
			// dual-meaning the right softkey carries.
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneCurrencyConverter::buttonReleased(uint i) {
	switch(i) {
		case BTN_RIGHT:
		case BTN_BACK:
			if(!backLongFired) {
				if(entryLen > 0) {
					// Something to backspace away.
					backspaceEntry();
				} else {
					// Empty entry + short BACK -- exit the screen.
					pop();
				}
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneCurrencyConverter::buttonHeld(uint i) {
	switch(i) {
		case BTN_RIGHT:
		case BTN_BACK:
			// Long-press: clear the entry once. A second long-press
			// from a pristine state pops the screen, matching the
			// dialer's "hold to exit" muscle memory and PhoneCalculator's
			// long-BACK behaviour.
			backLongFired = true;
			if(softKeys) softKeys->flashRight();
			if(entryLen == 0) {
				pop();
			} else {
				clearEntry();
			}
			break;

		default:
			break;
	}
}
