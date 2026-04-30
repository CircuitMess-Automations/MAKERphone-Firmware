#ifndef MAKERPHONE_PHONEDIALERKEY_H
#define MAKERPHONE_PHONEDIALERKEY_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneDialerKey
 *
 * Reusable retro feature-phone numpad key (36x20) for MAKERphone 2.0.
 * It is the foundational atom of the future Phase-4 PhoneDialer screen
 * and Phase-5 T9 multi-tap text input - a single Sony-Ericsson-style
 * numpad button rendering one big glyph (a digit or '*' / '#') plus an
 * optional letters caption beneath the iconic "press 2 for ABC" hint.
 *
 *      +-------------+
 *      |     2       |     <- pixelbasic16 glyph (top, MP_TEXT)
 *      |    ABC      |     <- pixelbasic7 caption (bottom, MP_LABEL_DIM)
 *      +-------------+
 *
 * It mirrors the architecture pattern already established by
 * PhoneIconTile -> PhoneMenuGrid: a single, immutable key first, with
 * the composing PhoneDialerPad (3x4 grid: 1-9, *, 0, #) to follow in a
 * later run. By landing the atom in isolation we keep this commit
 * compile-clean and let the host arrange keys with whatever layout
 * suits it (3x4 dialer, 5x3 SMS quickbar, etc.).
 *
 * Implementation notes:
 *  - 100% code-only - just two lv_label children plus a halo ring drawn
 *    as a sibling lv_obj. No SPIFFS assets, no canvas backing buffers,
 *    zero data partition cost - matching every other Phone* widget.
 *  - The key uses the existing MAKERphone palette (kept in a single
 *    block at the top of the .cpp, identical to PhoneIconTile so the
 *    two widgets always agree visually).
 *  - Selection is purely visual: setSelected(true) brightens the border
 *    to MP_ACCENT and fades the same halo ring PhoneIconTile uses, so
 *    composing the two widgets in mixed grids reads as one coherent
 *    style language.
 *  - Press flash is a brief, one-shot border flash to MP_ACCENT_BRIGHT
 *    that auto-restores after FlashDuration ms. This is what the future
 *    dialer / T9 screens will trigger on every keystroke to give the
 *    user instant feedback - decoupled from setSelected() so a key can
 *    flash while ALSO being the cursor without breaking the cursor look.
 *  - Labels are nullable: passing nullptr / empty for letters yields a
 *    single big-glyph key (used for '1', '0', '*', '#').
 *  - Tiles flow naturally inside a flex / grid parent - no
 *    LV_OBJ_FLAG_IGNORE_LAYOUT (same choice as PhoneIconTile so the
 *    future PhoneDialerPad can lay keys out automatically).
 */
class PhoneDialerKey : public LVObject {
public:
	/**
	 * Build a single dialer key.
	 *
	 * @param parent  LVGL parent.
	 * @param glyph   Big character to render (typically '0'..'9', '*', '#').
	 *                Stored as a 2-char NUL-terminated string for the label.
	 * @param letters Optional small caption (e.g. "ABC", "DEF"). Pass
	 *                nullptr or "" for keys that have none ('1', '0', '*', '#').
	 */
	PhoneDialerKey(lv_obj_t* parent, char glyph, const char* letters = nullptr);
	virtual ~PhoneDialerKey();

	/** Toggle the highlighted/focused look (pulsing halo + orange border). */
	void setSelected(bool selected);
	bool isSelected() const { return selected; }

	/**
	 * Trigger a brief border-flash to give the user instant tactile-feedback
	 * on a keystroke. Auto-restores to the prior selection look after
	 * FlashDuration milliseconds. Calling repeatedly retriggers the timer.
	 */
	void pressFlash();

	/** The big glyph this key renders (as a printable char). */
	char getGlyph() const { return glyph; }

	/** The letters caption ("ABC", "DEF", ...) or empty string when absent. */
	const char* getLetters() const { return letters.c_str(); }

	/** Whether this key was constructed with a non-empty letters caption. */
	bool hasLetters() const { return letters.length() > 0; }

	static constexpr uint16_t KeyWidth        = 36;
	static constexpr uint16_t KeyHeight       = 20;
	// Halo "breath" period (ms). Full ping-pong cycle = 2 * HaloPulsePeriod.
	// Matches PhoneIconTile so mixed grids share one visual rhythm.
	static constexpr uint16_t HaloPulsePeriod = 900;
	// Press flash duration (ms). Long enough to register as a flash, short
	// enough not to obscure rapid-fire dialing or T9 multi-tap.
	static constexpr uint16_t FlashDuration   = 90;

private:
	char   glyph;
	String letters;
	bool   selected = false;

	lv_obj_t* halo;          // outer glow ring (visible only when selected)
	lv_obj_t* digitLabel;    // pixelbasic16 big glyph (always present)
	lv_obj_t* lettersLabel;  // pixelbasic7 caption beneath glyph (nullable)

	lv_timer_t* flashTimer = nullptr;

	void buildBackground();
	void buildHalo();
	void buildDigitLabel();
	void buildLettersLabel(const char* letters);
	void refreshSelection();

	static void haloPulseExec(void* var, int32_t v);
	static void flashEndCb(lv_timer_t* timer);
};

#endif //MAKERPHONE_PHONEDIALERKEY_H
