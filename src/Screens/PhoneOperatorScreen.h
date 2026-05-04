#ifndef MAKERPHONE_PHONEOPERATORSCREEN_H
#define MAKERPHONE_PHONEOPERATORSCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneT9Input;

/**
 * PhoneOperatorScreen - S147
 *
 * SYSTEM-section sub-screen that lets the user edit the "operator
 * banner" painted just under the status bar on PhoneHomeScreen --
 * the literal Sony-Ericsson / Nokia carrier-banner you remember from
 * the homescreen. Reached from PhoneSettingsScreen's "Operator" row,
 * directly below S146's "Power-off msg" inside the SYSTEM group.
 *
 * The screen has two coexisting editor regions, one focused at a time:
 *
 *   TEXT mode -- top region, T9 multi-tap entry into the 15-char
 *                Settings.operatorText slot. PhoneT9Input does all
 *                the heavy lifting (cycle timer, caret, case toggle,
 *                first-letter-upper) so the entry feels exactly like
 *                PhoneOwnerNameScreen / PhonePowerOffMessageScreen.
 *
 *   LOGO mode -- bottom region, a 16x5 cursor-driven pixel toggler
 *                for the Settings.operatorLogo bitmap (five uint16_t
 *                rows). Bit 15 of each row is the leftmost column so
 *                the bitmap reads naturally when written out as
 *                binary literals -- same convention the banner widget
 *                renders against.
 *
 *   View:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |              OPERATOR                  | <- caption (cyan, pixelbasic7)
 *   |  ------------------------------------- |
 *   |  +----------------------------------+ |
 *   |  | MAKERphone|                      | | <- PhoneT9Input (TEXT mode)
 *   |  | abc                          Abc | |
 *   |  +----------------------------------+ |
 *   |  +--------LOGO----16x5--------------+ |
 *   |  | . . . X X X . . X X X . . . . . | |
 *   |  | . . X X . . X X . . X X . . . . | | <- 16x5 grid (LOGO mode)
 *   |  | . . X X X X X X X X X X . . . . | |
 *   |  | . . X X . . . . . . X X . . . . | |
 *   |  | . . X X . . . . . . X X . . . . | |
 *   |  +----------------------------------+ |
 *   |  ------------------------------------- |
 *   |  CLEAR                          DONE   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Mode focus is indicated with a cyan rectangle around the active
 * region and an "EDITING TEXT" / "EDITING LOGO" cyan caption above
 * the soft-keys; the non-active region is dimmed to muted-purple so
 * the user always knows which keypad behaviour is currently live.
 *
 * Persistence
 *   - Backed directly by Settings.operatorText / operatorLogo, written
 *     via the existing SettingsImpl::store() call. No separate NVS
 *     namespace -- both slots are part of the same blob that already
 *     holds soundProfile / wallpaperStyle / themeId / keyTicks /
 *     ownerName / powerOffMessage.
 *   - The buffer is auto-saved on the way out (DONE softkey, BACK
 *     short, BACK long) so an instant edit never loses content. CLEAR
 *     wipes only the focused region (text in TEXT mode, logo bits in
 *     LOGO mode) and persists the change in one shot.
 *
 * Controls
 *   TEXT mode:
 *     - BTN_0..BTN_9                : T9 multi-tap (PhoneT9Input).
 *     - BTN_L bumper                : T9 backspace (forwards '*').
 *     - BTN_R bumper                : T9 case toggle (forwards '#').
 *     - BTN_ENTER                   : commit the in-flight pending letter.
 *     - BTN_LEFT softkey ("CLEAR")  : wipe the operatorText buffer.
 *     - BTN_RIGHT softkey ("DONE")  : persist + pop screen.
 *
 *   LOGO mode:
 *     - BTN_2 / BTN_4 / BTN_6 / BTN_8   : move 16x5 cursor up/left/right/down.
 *     - BTN_5 / BTN_ENTER               : toggle the focused pixel.
 *     - BTN_L bumper                    : invert every bit (inversion
 *                                          is reversible -- press again
 *                                          to flip back).
 *     - BTN_R bumper                    : (no-op, reserved).
 *     - BTN_LEFT softkey ("CLEAR")      : wipe every logo bit.
 *     - BTN_RIGHT softkey ("DONE")      : persist + pop screen.
 *
 *   Either mode:
 *     - BTN_BACK short                  : auto-save and pop screen.
 *     - BTN_BACK long (>=600 ms)        : auto-save and pop screen.
 *     - BTN_0  (LOGO mode override)     : the "0" digit is consumed
 *                                          by the cursor grid (no
 *                                          T9 forwarding) so the
 *                                          numpad maps cleanly to
 *                                          d-pad / select gestures.
 *     - BTN_LEFT bumper                 : (handled per-mode above.)
 *
 * Mode switch:
 *     - On entry the screen starts in TEXT mode (matches the muscle
 *       memory of the existing T9 sub-screens).
 *     - The middle softkey center label reads "L=MODE" on every
 *       repaint so the gesture is discoverable; pressing the BTN_L
 *       *bumper* is reserved for T9 backspace in TEXT mode, so the
 *       mode toggle is wired to BTN_R (long-press) instead. To keep
 *       the discovery copy short we also expose mode switching via
 *       a long-press of BTN_ENTER. BTN_ENTER short-press is reserved
 *       for "commit pending letter" / "toggle pixel" depending on
 *       the active mode, the long-press fires the mode flip.
 *
 * Implementation notes
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar / PhoneT9Input so the screen
 *     reads as part of the MAKERphone family. Data partition cost
 *     stays zero.
 *   - The PhoneT9Input lives for the entire screen lifetime, even
 *     while LOGO mode steals keypad focus, so its caret / commit
 *     timers are paused (hidden) rather than torn down -- mode flips
 *     are then instant rather than walking the LVGL allocator.
 *   - Logo cells are drawn at 6x6 px each so the 16x5 grid spans
 *     96x30 px (centred at (32, 64)). The cursor is a single accent
 *     rectangle that floats over the focused cell, so cursor moves
 *     are one position update rather than a full grid rebuild.
 *   - The 80 cell rectangles are kept alive across edits and recoloured
 *     in place by toggleAt(); no LVGL allocator churn during editing.
 */
class PhoneOperatorScreen : public LVScreen, private InputListener {
public:
	PhoneOperatorScreen();
	virtual ~PhoneOperatorScreen() override;

	void onStart() override;
	void onStop() override;

	/** Hard cap on the editable text buffer (matches the 16-byte
	 *  Settings.operatorText field minus the trailing nul). */
	static constexpr uint16_t MaxTextLen = 15;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs  = 600;
	static constexpr uint16_t ModeHoldMs  = 600;

	/** Logical 16x5 pixel grid dimensions. */
	static constexpr uint8_t  LogoCols    = 16;
	static constexpr uint8_t  LogoRows    = 5;

	/** Cell size for the on-screen editor (the homescreen banner
	 *  paints at 1 px / cell; we render bigger here so the bitmap
	 *  is comfortable to edit). */
	static constexpr lv_coord_t EditorCellPx = 6;
	static constexpr lv_coord_t EditorWidth  = LogoCols * EditorCellPx; // 96
	static constexpr lv_coord_t EditorHeight = LogoRows * EditorCellPx; // 30

	enum class Mode : uint8_t { Text = 0, Logo = 1 };

	/** Read-only accessors useful for tests and future hosts. */
	uint16_t getTextLength() const;
	const char* getText() const;
	uint16_t getLogoRow(uint8_t r) const;
	Mode getMode() const { return mode; }
	uint8_t getCursorX() const { return cx; }
	uint8_t getCursorY() const { return cy; }
	bool    isDirty()    const { return dirty; }

	/**
	 * Force-flush every live edit into Settings + persist. Public so a
	 * future shell (e.g. low-battery shutdown hook) can persist the
	 * in-flight banner without ripping the user out of the screen.
	 */
	void persist();

	/** Wipe the focused region (text in TEXT mode, every logo bit in
	 *  LOGO mode) and persist. Public for the same reason as
	 *  persist() -- a host or test can drive it without synthesising
	 *  a softkey press. */
	void clearFocused();

	/** Flip every logo bit. No-op outside LOGO mode. */
	void invertLogo();

	/** Toggle the focused pixel (LOGO mode only). */
	void togglePixel();

	/** Switch between TEXT and LOGO mode. */
	void switchMode();

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// View widgets.
	lv_obj_t*     captionLabel = nullptr;     // "OPERATOR"
	lv_obj_t*     topDivider   = nullptr;
	lv_obj_t*     bottomDivider= nullptr;
	lv_obj_t*     modeHint     = nullptr;     // "EDITING TEXT" / "EDITING LOGO"

	// TEXT region.
	PhoneT9Input* t9Input      = nullptr;
	lv_obj_t*     textBorder   = nullptr;     // focus rectangle around T9

	// LOGO region.
	lv_obj_t*     logoHost     = nullptr;     // grid host
	lv_obj_t*     logoBorder   = nullptr;     // focus rectangle around grid
	lv_obj_t*     logoCells[LogoRows][LogoCols] = { { nullptr } };
	lv_obj_t*     cursor       = nullptr;     // accent rectangle over focused cell

	// State -- mirrors the persisted Settings until persist() flushes.
	char          textBuf[MaxTextLen + 1] = "";
	uint16_t      logoBuf[LogoRows]       = { 0, 0, 0, 0, 0 };
	Mode          mode                    = Mode::Text;
	uint8_t       cx                      = 0;
	uint8_t       cy                      = 0;
	bool          dirty                   = false;
	bool          backLongFired           = false;
	bool          enterLongFired          = false;

	// ---- builders ----
	void buildCaptionAndDividers();
	void buildTextRegion();
	void buildLogoRegion();
	void buildModeHint();

	// ---- repainters ----
	void refreshSoftKeys();
	void refreshModeHint();
	void refreshFocus();
	void refreshLogoCell(uint8_t r, uint8_t c);
	void refreshCursor();
	void refreshAllLogoCells();

	// ---- helpers ----
	void onClearPressed();
	void onDonePressed();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEOPERATORSCREEN_H
