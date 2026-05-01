#ifndef MAKERPHONE_PHONECALCULATOR_H
#define MAKERPHONE_PHONECALCULATOR_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneCalculator
 *
 * Phase-L kickoff (S60): the first MAKERphone utility app -- a basic
 * four-function calculator (add / subtract / multiply / divide) styled
 * with dialer-pad-style buttons so it visually slots in next to the
 * S23 PhoneDialerScreen and the rest of the Phone* family. Same retro
 * Sony-Ericsson silhouette, same MP_* palette, same code-only build:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |   12 X                                 | <- history (small, dim)
 *   |                                  84    | <- display (big, cream)
 *   |                                        |
 *   |   +----++----++----++----+             |
 *   |   | 7  || 8  || 9  | | / |             | <- 4x4 button grid
 *   |   +----++----++----++----+
 *   |   +----++----++----++----+             |
 *   |   | 4  || 5  || 6  | | x |             |
 *   |   +----++----++----++----+
 *   |   +----++----++----++----+             |
 *   |   | 1  || 2  || 3  | | - |             |
 *   |   +----++----++----++----+
 *   |   +----++----++----++----+             |
 *   |   | C  || 0  || =  | | + |             |
 *   |   +----++----++----++----+             |
 *   |   OP                            BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * The grid is purely visual (no on-screen cursor) -- input comes from
 * the physical keys, with each key press flashing the matching tile so
 * the user gets the same tactile cue PhoneDialerScreen gives:
 *
 *   - BTN_0..BTN_9       : type a digit into the current entry.
 *   - BTN_L              : staged-operator cycle "+ -> - -> x -> / -> +".
 *                          Mirrors the visual softkey on the left
 *                          ("OP") so two keys (the L bumper and the
 *                          left softkey) reach the same action.
 *   - BTN_R              : equals (=).
 *   - BTN_LEFT (softkey) : "OP" -- same as BTN_L (cycle operator).
 *   - BTN_RIGHT(softkey) : "BACK" -- same short/long behaviour as the
 *                          back button: short-press = backspace one
 *                          digit from the entry; long-press = exit.
 *   - BTN_ENTER          : equals (=) -- friendlier alias for BTN_R.
 *   - BTN_BACK           : short-press = backspace; long-press = clear
 *                          all (AC) -- clears the accumulator AND the
 *                          entry; a second long-press exits.
 *
 * Math semantics are deliberately classic-feature-phone simple:
 *   acc = acc <op> entry  every time the user picks a new operator OR
 *   presses '='. There is no operator precedence -- "2 + 3 x 4" computes
 *   left-to-right ((2+3)*4 = 20), matching every Sony-Ericsson basic-calc
 *   shipped on real hardware.
 *
 * Edge cases:
 *   - Division by zero shows "ERROR" in the display and freezes the
 *     accumulator until the user hits clear-all (long BACK).
 *   - The entry buffer caps at MaxEntryDigits (10) so a 64-bit double
 *     does not overflow the 11-char display column.
 *   - The result is rendered with %g (no trailing zeros) and truncated
 *     to MaxDisplayChars so a giant number falls back to "ERROR" rather
 *     than overflowing the label width.
 *
 * Implementation notes:
 *   - 100% code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually
 *     part of the MAKERphone family. Data partition cost stays zero.
 *   - The 16 buttons are rendered in-place as plain lv_obj rectangles
 *     (one per cell, 32x16 with a 1 px MP_DIM border) carrying a single
 *     pixelbasic7 label each. We DO NOT reuse PhoneDialerKey because it
 *     is fixed at 36x20 -- a 4x4 grid of those does not fit under the
 *     display in the 160x128 budget. The mini-key style still reads as
 *     "dialer-pad-style" because the border / radius / palette are
 *     identical to PhoneDialerKey's idle look.
 *   - flashCell(idx) ping-pongs the matching cell's border to
 *     MP_ACCENT_BRIGHT for ~90 ms (the same FlashDuration PhoneDialerKey
 *     uses), so direct-key input gives the same visual cue arrow-key
 *     navigation would have given on a cursor-driven pad. No timer is
 *     allocated until the user actually presses something.
 *   - The accumulator and entry are kept on the screen instance (no
 *     heap), and the formatters all bound their output to small stack
 *     buffers so the screen has no per-press allocation traffic.
 */
class PhoneCalculator : public LVScreen, private InputListener {
public:
	PhoneCalculator();
	virtual ~PhoneCalculator() override;

	void onStart() override;
	void onStop() override;

	/** Cap on how many digits the user can type for one operand. */
	static constexpr uint8_t MaxEntryDigits = 10;
	/** Cap on how many chars the display label can render before ERROR. */
	static constexpr uint8_t MaxDisplayChars = 11;

	/** Pulse duration (ms) for the press-flash on a button cell. */
	static constexpr uint16_t FlashDurationMs = 90;
	/** Long-press threshold for BTN_BACK (ms). */
	static constexpr uint16_t BackHoldMs = 600;

	/**
	 * Total number of button cells (4 cols x 4 rows). Public so the
	 * file-scope kCellLayout table in PhoneCalculator.cpp can size
	 * itself off the same constant the class uses internally without
	 * a friend declaration.
	 */
	static constexpr uint8_t CellCount = 16;

	/**
	 * Apply two operands with an operator. Static + side-effect-free so
	 * a host (or a test) can sanity-check the math without standing up
	 * the screen. `op` is one of '+', '-', 'x', '/'. On division by zero
	 * `outErr` is set to true and the return value is undefined.
	 */
	static double applyOp(double a, double b, char op, bool& outErr);

	/**
	 * Format a double as a fixed-width display string. Uses %g so a
	 * round number like 42.0 prints as "42" (not "42.000000"); strips
	 * trailing zeros from a fractional result; falls back to "ERROR"
	 * when the formatted text would not fit in MaxDisplayChars. Static
	 * for the same testability reason as applyOp().
	 */
	static void formatDisplay(double value, char* out, size_t outLen);

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Display strip (history line + big result line).
	lv_obj_t* historyLabel;
	lv_obj_t* resultLabel;

	// 16 button cells (kept in row-major order: 0..15).
	// Layout:  7 8 9 /
	//          4 5 6 x
	//          1 2 3 -
	//          C 0 = +  (CellCount defined in the public block above so
	// the file-scope kCellLayout table in PhoneCalculator.cpp can size
	// itself off the same constant.)
	lv_obj_t* cells[CellCount]      = { nullptr };
	lv_obj_t* cellLabels[CellCount] = { nullptr };

	lv_timer_t* flashTimer = nullptr;
	int8_t      flashCellIndex = -1;

	// Calculator state.
	char    entry[MaxEntryDigits + 2];   // typed digits + optional minus + NUL
	uint8_t entryLen     = 0;
	double  accumulator  = 0.0;
	bool    accValid     = false;        // becomes true once a digit + op are committed
	char    pendingOp    = 0;            // '+', '-', 'x', '/' or 0
	bool    justComputed = false;        // true after '=' so next digit clears
	bool    errorState   = false;        // div-by-zero / overflow latch

	bool    backLongFired = false;       // long-press suppression flag

	void buildDisplayStrip();
	void buildKeypad();

	// Glyph for a given cell index (matches the visual layout above).
	static char cellGlyph(uint8_t index);
	// Index of the cell carrying glyph `c`, or -1 if absent.
	int8_t findCellIndex(char c) const;

	void appendDigit(char c);
	void backspaceEntry();
	void clearAll();
	void cycleOperator();
	void chooseOperator(char op);   // commit any pending op, stage `op`
	void computeEquals();           // commit pending op, finalise result

	double parseEntry() const;      // string -> double (entry might be empty)
	void   showError();             // latch errorState + paint "ERROR"

	void   refreshDisplay();

	/**
	 * S67: keep the right-softkey caption in sync with the entry
	 * buffer state. Empty entry / no-op short-press = "BACK". Buffer
	 * holds digits = "DEL" (a short BACK press will pop one digit).
	 * Errored / cleared state collapses back to "BACK". Called from
	 * refreshDisplay() so every state-changing path drives both at
	 * once - no extra wiring at each call site.
	 */
	void   refreshSoftKeys();
	void   flashCell(int8_t index);
	void   restoreCellBorder(int8_t index);
	static void onFlashTimer(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONECALCULATOR_H
