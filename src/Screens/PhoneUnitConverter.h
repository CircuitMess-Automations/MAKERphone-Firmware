#ifndef MAKERPHONE_PHONEUNITCONVERTER_H
#define MAKERPHONE_PHONEUNITCONVERTER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneUnitConverter - S127
 *
 * Phase-P utility screen: an offline two-column unit converter for the
 * four households a feature-phone owner reaches for most often --
 * LENGTH / MASS / TEMPERATURE / VOLUME. Sits next to PhoneCalculator
 * (S60), PhoneAlarmClock (S124), PhoneTimers (S125) and the freshly
 * shipped PhoneCurrencyConverter (S126) inside the eventual utility-
 * apps grid; the visual silhouette deliberately mirrors S126 so muscle
 * memory carries between the two screens.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |              LENGTH                    | <- pixelbasic7 cyan
 *   |   FROM           >                     | <- active-side marker
 *   |   m                    100             | <- pixelbasic16 row
 *   |   TO                                   |
 *   |   ft                  328.084          | <- pixelbasic16 row
 *   |   <> unit  L/R cat  ENT swap           | <- pixelbasic7 dim hint
 *   |   SWAP                        BACK     | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Two-column converter sharing one typed-amount on the active side; the
 * other side is computed live from a small (factor, offset) table,
 * which collapses linear conversions (m <-> ft) and affine ones
 * (C <-> F, C <-> K) into a single uniform shape.
 *
 * Controls:
 *   - BTN_0..BTN_9   : append a digit to the active side's amount.
 *                      Caps at MaxEntryDigits to keep the result
 *                      column readable.
 *   - BTN_LEFT       : cycle the *active* unit one step backward
 *                      through the current category. Wraps.
 *   - BTN_RIGHT      : cycle the active unit one step forward. Wraps.
 *   - BTN_L          : cycle CATEGORY one step backward. Resets the
 *                      from/to indices to the first two slots so the
 *                      pair always renders coherently. The typed
 *                      entry is preserved so the user can keep their
 *                      number and just see "what's that in the next
 *                      family of units?".
 *   - BTN_R          : cycle CATEGORY one step forward.
 *   - BTN_ENTER      : SWAP active side (FROM <-> TO). The other
 *                      side's previous displayed amount becomes the
 *                      new active entry, identical to S126's swap
 *                      semantics so the gesture is portable.
 *                      Also wired to the LEFT softkey ("SWAP").
 *   - BTN_BACK       : short-press = backspace one digit; long-press
 *                      = clear the entry to zero; second long-press
 *                      from a pristine state pops the screen. Same
 *                      gesture muscle memory as PhoneCalculator (S60)
 *                      and PhoneCurrencyConverter (S126).
 *   - BTN_RIGHT softkey alias ("BACK" / "DEL"): mirrors BTN_BACK. We
 *                      reuse BTN_RIGHT as the *unit-cycle* axis on
 *                      this screen, so the right-softkey BACK action
 *                      is reached via the dedicated BTN_BACK key. The
 *                      bar caption stays "BACK"/"DEL" for visual
 *                      parity with PhoneCurrencyConverter.
 *
 * Design notes:
 *   - Conversions are uniform: every unit has a (factor, offset) pair
 *     describing value = base * factor + offset, where `base` is the
 *     category's SI-style anchor (meter, kilogram, Celsius, liter).
 *     Inverse: base = (value - offset) / factor. This collapses the
 *     temperature edge cases into the same code path as length / mass
 *     / volume -- no special branches in convert().
 *   - The active side is marked with a small ">" glyph in the side
 *     caption, and the active unit code is highlighted in MP_HIGHLIGHT
 *     cyan; the inactive side stays in MP_TEXT cream so the contrast
 *     tells the user where keypad input is going. Same colour grammar
 *     as S126.
 *   - 100% code-only: PhoneSynthwaveBg + PhoneStatusBar + PhoneSoftKeyBar
 *     + LVGL labels, no SPIFFS assets.
 *   - convert() / formatAmount() are static + side-effect-free so
 *     tests / future call sites can sanity-check the math without
 *     instantiating the screen, exactly the way PhoneCalculator::applyOp()
 *     and PhoneCurrencyConverter::convert() are factored.
 *   - Entry buffer is digits-only positive integer (no minus key, no
 *     decimal point) -- a deliberate match with S126 so the keypad
 *     model is identical. Negative results in temperature still print
 *     correctly because formatAmount() handles the sign on output.
 */
class PhoneUnitConverter : public LVScreen, private InputListener {
public:
	PhoneUnitConverter();
	virtual ~PhoneUnitConverter() override;

	void onStart() override;
	void onStop() override;

	/** Cap on how many digits the user can type for the active amount.
	 *  Bounds the parse at 9,999,999 so the typed value comfortably
	 *  fits in the 8-char display column even after a high-factor
	 *  conversion (e.g. m -> mm gives a x1000 multiplier). */
	static constexpr uint8_t  MaxEntryDigits   = 7;

	/** Cap on how many chars the right-aligned amount label can render
	 *  before formatAmount() falls back to a clipped placeholder. */
	static constexpr uint8_t  MaxDisplayChars  = 10;

	/** Long-press threshold (ms). Same value the rest of the MAKERphone
	 *  shell uses so the gesture feels identical from any screen. */
	static constexpr uint16_t BackHoldMs       = 600;

	/** A single unit-table entry. Public for the file-scope unit-table
	 *  definitions in PhoneUnitConverter.cpp.
	 *
	 *  Conversion semantics (with `base` = category anchor):
	 *      value = base * factor + offset
	 *      base  = (value - offset) / factor
	 *
	 *  For purely linear units (length/mass/volume) offset is 0 and
	 *  the math collapses to value = base * factor. Temperature uses
	 *  offset to encode the F / K shift. */
	struct Unit {
		const char* code;
		double      factor;
		double      offset;
	};

	/** A category groups a set of units around a shared base. */
	struct Category {
		const char* name;     // display caption ("LENGTH", "MASS", ...)
		const Unit* units;
		uint8_t     unitCount;
	};

	/**
	 * Convert `amount` from `srcUnit` to `dstUnit` within the same
	 * category. Static and side-effect-free for the same testability
	 * reason as PhoneCalculator::applyOp() / PhoneCurrencyConverter::convert().
	 *
	 * If either pointer is null or srcUnit->factor is 0, returns 0.0.
	 * Otherwise applies:
	 *     base   = (amount - srcUnit->offset) / srcUnit->factor
	 *     result = base * dstUnit->factor + dstUnit->offset
	 */
	static double convert(double amount,
	                      const Unit* srcUnit,
	                      const Unit* dstUnit);

	/**
	 * Format `value` as a fixed-width display string. Same formatter
	 * shape as PhoneCurrencyConverter::formatAmount(): %g with
	 * trailing-zero strip, "----" placeholder if the value does not
	 * fit. Negative inputs are rendered with a leading "-" so
	 * temperature flips below zero render correctly. */
	static void formatAmount(double value, char* out, size_t outLen);

	/** Number of categories the inline table exposes. */
	static uint8_t categoryCount();

	/** Read-only access to a category. Returns nullptr if `idx` is
	 *  out of range. */
	static const Category* categoryAt(uint8_t idx);

	/** Read-only access to a unit within a category. Returns nullptr
	 *  if either index is out of range. Convenience wrapper so call
	 *  sites do not have to dereference Category* manually. */
	static const Unit* unitAt(uint8_t catIdx, uint8_t unitIdx);

private:
	/** Which column the keypad is feeding right now. */
	enum class Side : uint8_t {
		From = 0,
		To   = 1,
	};

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Caption + four content rows + hint label.
	lv_obj_t* captionLabel;     // category name ("LENGTH" / "MASS" / ...)
	lv_obj_t* fromCaption;      // "FROM" / "FROM >" when active
	lv_obj_t* fromCode;         // pixelbasic16 unit code (e.g. "m")
	lv_obj_t* fromAmount;       // pixelbasic16 amount string
	lv_obj_t* toCaption;        // "TO"   / "TO >"   when active
	lv_obj_t* toCode;           // pixelbasic16 unit code
	lv_obj_t* toAmount;         // pixelbasic16 amount string
	lv_obj_t* hintLabel;        // small dim hint line

	// Currently-selected category + per-side unit index inside it.
	// fromIdx / toIdx are bounded by the *active* category's unitCount;
	// cycleCategory() resets them to (0, 1) so the pair stays valid.
	uint8_t catIdx  = 0;
	uint8_t fromIdx = 0;
	uint8_t toIdx   = 1;

	// Active-side editor state.
	Side    active        = Side::From;
	char    entry[MaxEntryDigits + 1];   // typed digits + NUL
	uint8_t entryLen      = 0;

	bool    backLongFired = false;       // long-press suppression flag

	void buildCaption();
	void buildContent();

	/** Parse the current entry string into a double; returns 0 on
	 *  empty buffer. Bounded reads only -- never out-of-bounds. */
	double parseEntry() const;

	/** Set the entry buffer to a textual representation of `value`,
	 *  rounded to a whole-number string capped at MaxEntryDigits.
	 *  Used by SWAP to seed the new active side from the previous
	 *  computed amount. Mirrors PhoneCurrencyConverter::seedEntryFromValue()
	 *  exactly so the muscle memory across the two converters is
	 *  identical. */
	void seedEntryFromValue(double value);

	void appendDigit(char c);
	void backspaceEntry();
	void clearEntry();

	void cycleActiveUnit(int8_t delta);
	void cycleCategory(int8_t delta);
	void swapSides();

	void refreshDisplay();

	/** S67: keep the right-softkey caption in sync with the entry
	 *  buffer state. "DEL" while the active entry holds digits, "BACK"
	 *  once it is empty. Mirrors PhoneCurrencyConverter::refreshSoftKeys()
	 *  exactly. */
	void refreshSoftKeys();

	/** Pointer to the active-category record. Never null after
	 *  ctor because catIdx is clamped to a valid index. */
	const Category* activeCategory() const;

	/** Indexed unit pointers within the active category. */
	const Unit* activeUnit() const;
	const Unit* otherUnit()  const;

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEUNITCONVERTER_H
