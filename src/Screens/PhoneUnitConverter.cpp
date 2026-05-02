#include "PhoneUnitConverter.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget
// so the converter slots in beside PhoneCalculator (S60), PhoneAlarmClock
// (S124), PhoneTimers (S125) and PhoneCurrencyConverter (S126) without a
// visual seam. Inlined per the established pattern.
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption / active code
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream amount
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple side caption / hint
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (active marker)
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple (unused border tint)

// ---------- offline unit tables ---------------------------------------
//
// Conversion model: every Unit carries (factor, offset) such that
//   value_in_unit = base * factor + offset
//   base          = (value_in_unit - offset) / factor
// where `base` is the category's anchor (meter / kg / Celsius / liter).
// This collapses linear families (length, mass, volume) and the
// affine temperature family into a single uniform branch in convert().
//
// Numbers are conventional NIST-style mid-precision constants -- enough
// significant figures for a feature-phone display column without
// pretending to micrometre-grade accuracy. A user who needs more
// precision can update the table in source.

// Length: base = meter.
static constexpr PhoneUnitConverter::Unit kLength[] = {
		{ "m",     1.0,            0.0 },
		{ "km",    0.001,          0.0 },
		{ "cm",    100.0,          0.0 },
		{ "mm",    1000.0,         0.0 },
		{ "mi",    0.000621371,    0.0 },
		{ "yd",    1.09361,        0.0 },
		{ "ft",    3.28084,        0.0 },
		{ "in",    39.3701,        0.0 },
};

// Mass: base = kilogram.
static constexpr PhoneUnitConverter::Unit kMass[] = {
		{ "kg",    1.0,            0.0 },
		{ "g",     1000.0,         0.0 },
		{ "mg",    1.0e6,          0.0 },
		{ "t",     0.001,          0.0 },   // metric tonne
		{ "lb",    2.20462,        0.0 },
		{ "oz",    35.274,         0.0 },
		{ "st",    0.157473,       0.0 },   // imperial stone
};

// Temperature: base = Celsius. Offsets shift the zero so the same
// (factor, offset) shape covers F and K without a special branch in
// convert(). 1 C = 1 C; 1 C = 1.8 F + 32; 1 C = 1 K + 273.15.
static constexpr PhoneUnitConverter::Unit kTemperature[] = {
		{ "C",     1.0,            0.0    },
		{ "F",     1.8,           32.0    },
		{ "K",     1.0,          273.15   },
};

// Volume: base = liter.
static constexpr PhoneUnitConverter::Unit kVolume[] = {
		{ "L",     1.0,            0.0 },
		{ "mL",    1000.0,         0.0 },
		{ "m3",    0.001,          0.0 },
		{ "gal",   0.264172,       0.0 },   // US gallon
		{ "qt",    1.05669,        0.0 },   // US quart
		{ "pt",    2.11338,        0.0 },   // US pint
		{ "cup",   4.22675,        0.0 },   // US cup
		{ "floz",  33.814,         0.0 },   // US fluid ounce
};

static constexpr PhoneUnitConverter::Category kCategories[] = {
		{ "LENGTH", kLength,      sizeof(kLength)      / sizeof(kLength[0])      },
		{ "MASS",   kMass,        sizeof(kMass)        / sizeof(kMass[0])        },
		{ "TEMP",   kTemperature, sizeof(kTemperature) / sizeof(kTemperature[0]) },
		{ "VOLUME", kVolume,      sizeof(kVolume)      / sizeof(kVolume[0])      },
};
static constexpr uint8_t kCategoryCount =
		sizeof(kCategories) / sizeof(kCategories[0]);

// ---------- geometry --------------------------------------------------
//
// Identical Y-grid to PhoneCurrencyConverter so a user flicking between
// the two converters sees the same labelled columns in the same place.
// The 160x128 display gives 118 px between the 10 px PhoneStatusBar at
// y=0 and the 10 px PhoneSoftKeyBar pinned at y=118. Within that band:
//
//   y=12   caption         "LENGTH"              pixelbasic7 cyan
//   y=24   FROM caption    "FROM" / "FROM >"     pixelbasic7 dim
//   y=34   FROM code+amt   "m"        "100"      pixelbasic16
//   y=58   TO   caption    "TO"   / "TO >"       pixelbasic7 dim
//   y=68   TO   code+amt   "ft"       "328.084"  pixelbasic16
//   y=92   hint            "<>unit L/R cat ..."  pixelbasic7 dim
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

// ---------- public statics --------------------------------------------

uint8_t PhoneUnitConverter::categoryCount() {
	return kCategoryCount;
}

const PhoneUnitConverter::Category*
PhoneUnitConverter::categoryAt(uint8_t idx) {
	if(idx >= kCategoryCount) return nullptr;
	return &kCategories[idx];
}

const PhoneUnitConverter::Unit*
PhoneUnitConverter::unitAt(uint8_t catIdxArg, uint8_t unitIdx) {
	const Category* c = categoryAt(catIdxArg);
	if(c == nullptr) return nullptr;
	if(unitIdx >= c->unitCount) return nullptr;
	return &c->units[unitIdx];
}

double PhoneUnitConverter::convert(double amount,
                                   const Unit* srcUnit,
                                   const Unit* dstUnit) {
	if(srcUnit == nullptr || dstUnit == nullptr) return 0.0;
	if(srcUnit->factor == 0.0)                   return 0.0;
	// Identity short-circuit so a same-unit pair (e.g. m -> m) echoes
	// the typed amount back exactly, with no floating-point round-trip
	// noise from the * / / pair. Same convention as
	// PhoneCurrencyConverter::convert().
	if(srcUnit == dstUnit)                       return amount;

	const double base   = (amount - srcUnit->offset) / srcUnit->factor;
	const double result = base * dstUnit->factor + dstUnit->offset;
	return result;
}

void PhoneUnitConverter::formatAmount(double value,
                                      char* out,
                                      size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(outLen < 2) {
		out[0] = '\0';
		return;
	}

	// Sign handling: temperature flips below zero (e.g. K -> C with
	// a small input). Render with a leading "-" so the column reads
	// naturally; the entry buffer itself is positive-only because
	// there is no minus key on the dialer pad.
	const bool negative = value < 0.0;
	double v = negative ? -value : value;

	// Bound the magnitude so we never spill into scientific notation
	// for the printable column. 1e9 is wildly outside any practical
	// length / mass / temp / volume conversion the user will type;
	// fall back to "----" so the result is unmistakably "too big to
	// show" rather than silently truncated.
	if(!std::isfinite(value) || v >= 1.0e9) {
		const char* placeholder = "----";
		size_t n = strlen(placeholder);
		if(n >= outLen) n = outLen - 1;
		memcpy(out, placeholder, n);
		out[n] = '\0';
		return;
	}

	char buf[24];
	// %.6g matches the PhoneCurrencyConverter / PhoneCalculator family
	// formatters: up to 6 significant digits, no trailing zeros, no
	// exponent for our bounded magnitudes.
	snprintf(buf, sizeof(buf), "%.6g", v);

	// Strip a trailing decimal point so a whole number prints as "100"
	// not "100." -- the same belt-and-braces guard the rest of the
	// MAKERphone formatters use.
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
		// Result too long to fit the display column. Fall back to
		// the placeholder rather than truncating digits, which would
		// silently mislead the user.
		const char* placeholder = "----";
		size_t pn = strlen(placeholder);
		if(pn >= outLen) pn = outLen - 1;
		memcpy(out, placeholder, pn);
		out[pn] = '\0';
		return;
	}
	memcpy(out, composed, n + 1);
}

// ---------- ctor / dtor -----------------------------------------------

PhoneUnitConverter::PhoneUnitConverter()
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

	// Sensible default pair: LENGTH, m -> ft. This is the unit
	// conversion the average user reaches for first ("how tall is
	// 180 cm in feet?") and gives the active/inactive contrast
	// instantly because the codes are obviously different.
	catIdx  = 0;
	fromIdx = 0;
	const Category* cat0 = categoryAt(catIdx);
	toIdx = (cat0 != nullptr && cat0->unitCount > 1) ? 1 : 0;

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern PhoneCalculator / PhoneAlarmClock / PhoneTimers /
	// PhoneCurrencyConverter use.
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

	// Bottom soft-key bar. LEFT="SWAP" mirrors BTN_ENTER's swap action;
	// RIGHT defaults to "BACK" and refreshSoftKeys() flips it to "DEL"
	// while the entry buffer holds digits, so a short BACK pops one
	// character and a long-press clears. Identical caption grammar to
	// PhoneCurrencyConverter so muscle memory carries.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->set("SWAP", "BACK");

	// Long-press detection on BTN_BACK. Same 600 ms threshold the
	// rest of the MAKERphone shell uses. (BTN_RIGHT is the unit-
	// cycle axis on this screen, not a softkey alias, so it does not
	// need a hold time.)
	setButtonHoldTime(BTN_BACK, BackHoldMs);

	// Initial paint so the user sees the default pair immediately.
	refreshDisplay();
}

PhoneUnitConverter::~PhoneUnitConverter() {
	// All children (wallpaper, statusBar, softKeys, labels) are
	// parented to obj and freed by the LVScreen base destructor.
	// No timers held.
}

void PhoneUnitConverter::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneUnitConverter::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- builders --------------------------------------------------

void PhoneUnitConverter::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, kSideWidth);
	lv_obj_set_pos(captionLabel, kSideLeft, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	// refreshDisplay() will overwrite this with the active category
	// name; placeholder text just keeps the label non-empty for the
	// initial layout pass.
	lv_label_set_text(captionLabel, "LENGTH");
}

void PhoneUnitConverter::buildContent() {
	// FROM caption (small, dim, left-aligned). The active marker ">"
	// is appended to the text in refreshDisplay() rather than being a
	// separate widget -- one less node to manage in the z-order, same
	// trick as PhoneCurrencyConverter.
	fromCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(fromCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(fromCaption, MP_LABEL_DIM, 0);
	lv_obj_set_width(fromCaption, kSideWidth);
	lv_obj_set_pos(fromCaption, kSideLeft, kFromCaptionY);
	lv_obj_set_style_text_align(fromCaption, LV_TEXT_ALIGN_LEFT, 0);
	lv_label_set_text(fromCaption, "FROM");

	// FROM unit code (left-aligned, pixelbasic16). Color depends on
	// active state; refreshDisplay() flips it.
	fromCode = lv_label_create(obj);
	lv_obj_set_style_text_font(fromCode, &pixelbasic16, 0);
	lv_obj_set_style_text_color(fromCode, MP_HIGHLIGHT, 0);
	lv_obj_set_width(fromCode, kCodeWidth);
	lv_obj_set_pos(fromCode, kCodeLeft, kFromRowY);
	lv_obj_set_style_text_align(fromCode, LV_TEXT_ALIGN_LEFT, 0);
	lv_label_set_text(fromCode, "m");

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
	lv_label_set_text(toCode, "ft");

	toAmount = lv_label_create(obj);
	lv_obj_set_style_text_font(toAmount, &pixelbasic16, 0);
	lv_obj_set_style_text_color(toAmount, MP_TEXT, 0);
	lv_label_set_long_mode(toAmount, LV_LABEL_LONG_DOT);
	lv_obj_set_width(toAmount, kAmountWidth);
	lv_obj_set_pos(toAmount, kAmountLeft, kToRowY);
	lv_obj_set_style_text_align(toAmount, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(toAmount, "0");

	// Hint line at the bottom -- explains the non-obvious controls.
	// The string is intentionally compact so it fits the 148-px-wide
	// pixelbasic7 row without wrapping; "<>" stands in for the d-pad
	// LEFT/RIGHT keys, the "L/R" pair calls out the shoulder bumpers.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(hintLabel, kSideWidth);
	lv_obj_set_pos(hintLabel, kSideLeft, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "<> unit  L/R cat  ENT swap");
}

// ---------- entry buffer ----------------------------------------------

double PhoneUnitConverter::parseEntry() const {
	if(entryLen == 0) return 0.0;
	// strtod ignores any trailing NUL we placed; the buffer is
	// guaranteed null-terminated by appendDigit() and clearEntry().
	return strtod(entry, nullptr);
}

void PhoneUnitConverter::seedEntryFromValue(double value) {
	// Drop any sign / fractional digits so the seeded entry is a
	// clean integer string; the user can re-type fractional digits
	// if they want (well, integer-only -- this converter has no
	// decimal-point key, by design parity with S126). Bound the
	// magnitude so a swap from a high-factor unit (e.g. m -> mm gives
	// x1000) does not splat thousands of digits into the entry buffer.
	if(!std::isfinite(value) || value < 0.0) value = 0.0;
	double rounded = std::floor(value + 0.5);
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

void PhoneUnitConverter::appendDigit(char c) {
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

void PhoneUnitConverter::backspaceEntry() {
	if(entryLen == 0) return;
	entryLen--;
	entry[entryLen] = '\0';
	refreshDisplay();
}

void PhoneUnitConverter::clearEntry() {
	entry[0] = '\0';
	entryLen = 0;
	refreshDisplay();
}

// ---------- unit / category cycling -----------------------------------

void PhoneUnitConverter::cycleActiveUnit(int8_t delta) {
	const Category* cat = activeCategory();
	if(cat == nullptr || cat->unitCount == 0) return;
	uint8_t* idx = (active == Side::From) ? &fromIdx : &toIdx;
	int next = (int) *idx + (int) delta;
	while(next < 0)                  next += cat->unitCount;
	while(next >= cat->unitCount)    next -= cat->unitCount;
	*idx = (uint8_t) next;
	refreshDisplay();
}

void PhoneUnitConverter::cycleCategory(int8_t delta) {
	if(kCategoryCount == 0) return;
	int next = (int) catIdx + (int) delta;
	while(next < 0)               next += kCategoryCount;
	while(next >= kCategoryCount) next -= kCategoryCount;
	catIdx = (uint8_t) next;

	// Re-anchor the per-side picks to the first two slots in the new
	// category so the (from, to) pair is always valid and renders a
	// useful default (m -> km, kg -> g, C -> F, L -> mL). The user
	// can still cycle from there. Keep the typed entry untouched so
	// their number survives the category flip.
	const Category* cat = activeCategory();
	fromIdx = 0;
	toIdx   = (cat != nullptr && cat->unitCount > 1) ? 1 : 0;

	refreshDisplay();
}

void PhoneUnitConverter::swapSides() {
	// Capture the OTHER side's currently-displayed amount. That value
	// becomes the new active entry so the user keeps a sensible
	// starting number for the just-flipped flow ("show me what 328 ft
	// is in m" -- they were just typing 100 m; swap gives them 328 as
	// the new entry on the ft side). Identical to S126's swap.
	const double activeAmount = parseEntry();
	const double otherAmount  = convert(activeAmount, activeUnit(), otherUnit());

	active = (active == Side::From) ? Side::To : Side::From;
	seedEntryFromValue(otherAmount);
	refreshDisplay();
}

// ---------- helpers ---------------------------------------------------

const PhoneUnitConverter::Category*
PhoneUnitConverter::activeCategory() const {
	return categoryAt(catIdx);
}

const PhoneUnitConverter::Unit*
PhoneUnitConverter::activeUnit() const {
	return unitAt(catIdx, active == Side::From ? fromIdx : toIdx);
}

const PhoneUnitConverter::Unit*
PhoneUnitConverter::otherUnit() const {
	return unitAt(catIdx, active == Side::From ? toIdx : fromIdx);
}

// ---------- repaint ---------------------------------------------------

void PhoneUnitConverter::refreshDisplay() {
	const Category* cat = activeCategory();
	if(cat == nullptr) return;

	// Clamp per-side picks defensively in case someone (or a future
	// caller) leaves them out of range for the current category.
	if(fromIdx >= cat->unitCount) fromIdx = 0;
	if(toIdx   >= cat->unitCount) toIdx   = (cat->unitCount > 1) ? 1 : 0;

	const Unit* fromU = &cat->units[fromIdx];
	const Unit* toU   = &cat->units[toIdx];

	// Caption row -- category name (cyan, centered).
	lv_label_set_text(captionLabel, cat->name);

	// Side captions get a ">" marker on the active side. Same colour
	// grammar as PhoneCurrencyConverter: active caption flips to
	// MP_ACCENT (sunset orange), inactive stays MP_LABEL_DIM (dim
	// purple); active code flips to MP_HIGHLIGHT (cyan), inactive
	// stays MP_TEXT (cream).
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

	lv_label_set_text(fromCode, fromU->code);
	lv_label_set_text(toCode,   toU->code);

	// Compute both displayed amounts off the active entry. The active
	// side echoes the typed buffer (with "0" placeholder for empty);
	// the other side is computed via convert().
	const double activeAmount = parseEntry();
	const double otherAmount  = convert(activeAmount, activeUnit(), otherUnit());

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

void PhoneUnitConverter::refreshSoftKeys() {
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

// ---------- input handlers --------------------------------------------

void PhoneUnitConverter::buttonPressed(uint i) {
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
			// d-pad LEFT cycles the active unit one step backward
			// through the current category (wraps).
			cycleActiveUnit(-1);
			break;

		case BTN_RIGHT:
			// d-pad RIGHT cycles the active unit one step forward
			// (wraps).
			cycleActiveUnit(+1);
			break;

		case BTN_L:
			// Shoulder L cycles the CATEGORY one step backward.
			cycleCategory(-1);
			break;

		case BTN_R:
			// Shoulder R cycles the CATEGORY one step forward.
			cycleCategory(+1);
			break;

		case BTN_ENTER:
			// SWAP active side. Same flash + flip choreography as
			// PhoneCurrencyConverter so the muscle memory carries.
			if(softKeys) softKeys->flashLeft();
			swapSides();
			break;

		case BTN_BACK:
			// Hardware BACK -- short=backspace, long=clear-all. The
			// final action runs in buttonReleased() if the long-press
			// did not fire first, identical to PhoneCalculator and
			// PhoneCurrencyConverter.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneUnitConverter::buttonReleased(uint i) {
	switch(i) {
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

void PhoneUnitConverter::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press: clear the entry once. A second long-press
			// from a pristine state pops the screen, matching the
			// dialer's "hold to exit" muscle memory and
			// PhoneCurrencyConverter's long-BACK behaviour.
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
