#ifndef MAKERPHONE_PHONECURRENCYCONVERTER_H
#define MAKERPHONE_PHONECURRENCYCONVERTER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneCurrencyConverter - S126
 *
 * Phase-P utility screen: an offline two-column currency converter that
 * sits next to PhoneCalculator (S60) / PhoneAlarmClock (S124) /
 * PhoneTimers (S125) inside the eventual utility-apps grid. Every
 * Phone* screen wears the same Sony-Ericsson silhouette so a user
 * navigating between them feels at home immediately:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             CURRENCY                   | <- pixelbasic7 cyan
 *   |   FROM           >                     | <- active-side marker
 *   |   USD                  100             | <- pixelbasic16 row
 *   |   TO                                   |
 *   |   EUR                  92              | <- pixelbasic16 row
 *   |   <- L/R chg ccy   ENT swap ->         | <- pixelbasic7 dim hint
 *   |   SWAP                        BACK     | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Two columns share a single typed-amount on the *active* side; the
 * other side is computed live from the offline rate table. Switching
 * the active side just pivots which column is "input" vs "output" --
 * the rate table is shared. No SPIFFS assets.
 *
 * Controls:
 *   - BTN_0..BTN_9 : append a digit to the active side's amount.
 *                    Caps at MaxEntryDigits so a long mash does not
 *                    overflow the display column.
 *   - BTN_LEFT / BTN_L : cycle the active side's currency one step
 *                        backward through the inline rate table. Wraps.
 *   - BTN_RIGHT/ BTN_R : cycle the active side's currency one step
 *                        forward. Wraps.
 *   - BTN_ENTER        : SWAP active side (FROM <-> TO). The other
 *                        side's previous displayed amount becomes the
 *                        new active entry, so the user keeps a sensible
 *                        starting value for the just-flipped flow.
 *                        Also wired to the LEFT softkey ("SWAP") so
 *                        either approach reaches the same action.
 *   - BTN_BACK         : short-press = backspace one digit from the
 *                        active entry; long-press = clear the entry to
 *                        zero; second long-press from a pristine state
 *                        pops the screen. Same gesture muscle-memory as
 *                        PhoneCalculator (S60).
 *   - BTN_RIGHT softkey alias: "BACK" / "DEL" - mirrors BTN_BACK so a
 *                        thumb on the soft-key row reaches the same
 *                        action without crossing the keypad.
 *
 * Math:
 *   convert(amount, srcIdx, dstIdx) = amount * rate[dst] / rate[src]
 *
 * Rates are inlined in PhoneCurrencyConverter.cpp as a static
 * `Currency` table keyed off "units of THIS currency per 1 USD". The
 * table is small (~12 entries), plausible mid-2020s mid-rates, and
 * clearly marked as offline / approximate -- the firmware has no
 * network path so a live FX feed is not on the table. A user who needs
 * a precise rate can update the table in source.
 *
 * Implementation notes:
 *   - 100% code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually
 *     part of the MAKERphone family. Data partition cost stays zero.
 *   - The active side is marked with a small ">" glyph in the side
 *     caption row, and the active currency code is highlighted in
 *     MP_HIGHLIGHT cyan; the inactive side stays in MP_TEXT cream so
 *     the contrast tells the user where keypad input is going.
 *   - The amount string is kept in a small stack buffer; we never
 *     allocate per keypress. parseEntry() bounds the parse at
 *     MaxEntryDigits so overflow into double-precision exponent
 *     territory is impossible from typed input alone.
 *   - The result column uses %g formatting then trims trailing
 *     zeros so a round number like 100.00 prints as "100" rather
 *     than "100.000000", matching every Sony-Ericsson basic calc
 *     and the PhoneCalculator (S60) display formatter.
 *   - convert() is static + side-effect-free so a host (or a
 *     unit test) can sanity-check the math without standing up
 *     the screen, the same way PhoneCalculator::applyOp() works.
 */
class PhoneCurrencyConverter : public LVScreen, private InputListener {
public:
	PhoneCurrencyConverter();
	virtual ~PhoneCurrencyConverter() override;

	void onStart() override;
	void onStop() override;

	/** Cap on how many digits the user can type for the active amount.
	 *  Bounds the parse at 9,999,999 so a typed value comfortably fits
	 *  in the 8-char display column even after the rate-multiplied
	 *  result has up to 2 decimal places of precision. */
	static constexpr uint8_t  MaxEntryDigits   = 7;

	/** Cap on how many chars the right-aligned amount label can render
	 *  before the formatter falls back to a clipped result. */
	static constexpr uint8_t  MaxDisplayChars  = 10;

	/** Long-press threshold for BTN_BACK / BTN_RIGHT (ms). Same value
	 *  the rest of the MAKERphone shell uses so the gesture feels
	 *  identical from any screen. */
	static constexpr uint16_t BackHoldMs       = 600;

	/** A single currency-table entry. Public for the file-scope
	 *  rate-table definition in PhoneCurrencyConverter.cpp. */
	struct Currency {
		const char* code;     // 3-letter ISO-4217 code, e.g. "USD"
		double      perUSD;   // units of THIS currency per 1 USD
	};

	/**
	 * Convert `amount` from currency `srcIdx` to currency `dstIdx`,
	 * using the inline rate table. Static and side-effect-free for
	 * the same testability reason as PhoneCalculator::applyOp().
	 *
	 * If either index is out of range or src has perUSD == 0, returns
	 * 0.0. Otherwise:
	 *   amount * (perUSD[dst] / perUSD[src])
	 */
	static double convert(double amount, uint8_t srcIdx, uint8_t dstIdx);

	/**
	 * Format `value` as a fixed-width display string. Uses %g (no
	 * trailing zeros), strips the decimal point if the result is
	 * whole, and falls back to a "----" placeholder when the value
	 * does not fit in MaxDisplayChars. Static for testability.
	 */
	static void formatAmount(double value, char* out, size_t outLen);

	/** How many entries the inline currency table exposes. Public so
	 *  cycle-currency tests / call sites do not have to duplicate the
	 *  modulo. */
	static uint8_t currencyCount();

	/** Read-only access to the rate table. Returns nullptr if `idx`
	 *  is out of range. */
	static const Currency* currencyAt(uint8_t idx);

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
	lv_obj_t* captionLabel;     // "CURRENCY"
	lv_obj_t* fromCaption;      // "FROM" / "FROM >" when active
	lv_obj_t* fromCode;         // pixelbasic16 currency code (e.g. "USD")
	lv_obj_t* fromAmount;       // pixelbasic16 amount string
	lv_obj_t* toCaption;        // "TO"   / "TO >"   when active
	lv_obj_t* toCode;           // pixelbasic16 currency code
	lv_obj_t* toAmount;         // pixelbasic16 amount string
	lv_obj_t* hintLabel;        // small dim hint line

	// Currency picks for each side.
	uint8_t fromIdx = 0;       // defaults to "USD"
	uint8_t toIdx   = 1;       // defaults to "EUR"

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
	 *  computed amount. */
	void seedEntryFromValue(double value);

	void appendDigit(char c);
	void backspaceEntry();
	void clearEntry();

	void cycleActiveCurrency(int8_t delta);
	void swapSides();

	void refreshDisplay();
	/**
	 * S67: keep the right-softkey caption in sync with the entry
	 * buffer state. "DEL" while the active entry holds digits, "BACK"
	 * once it is empty. Mirrors PhoneCalculator's refreshSoftKeys()
	 * exactly so muscle memory carries.
	 */
	void refreshSoftKeys();

	/** Index of the currently-active currency. */
	uint8_t activeIdx() const { return active == Side::From ? fromIdx : toIdx; }
	/** Index of the inactive (computed) currency. */
	uint8_t otherIdx()  const { return active == Side::From ? toIdx   : fromIdx; }

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONECURRENCYCONVERTER_H
