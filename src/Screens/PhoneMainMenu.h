#ifndef MAKERPHONE_PHONEMAINMENU_H
#define MAKERPHONE_PHONEMAINMENU_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Elements/PhoneIconTile.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneMenuGrid;

/**
 * PhoneMainMenu
 *
 * The MAKERphone 2.0 phone-style main menu screen (S19). It is the natural
 * Sony-Ericsson successor to the legacy vertical `MainMenu` carousel, and
 * the first screen in Phase C that lets the user reach every flagship app
 * (Phone, Messages, Contacts, Music, Camera, Games, Settings) from a single
 * 4x2 icon grid:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |                                        |
 *   |   [Pho] [Msg] [Cnt] [Mus]              | <- PhoneMenuGrid (4 cols)
 *   |   [Cam] [Gam] [Set]                    |    7 PhoneIconTiles
 *   |                                        |
 *   |    PHONE                               | <- selected-tile caption
 *   |                                        |
 *   | <-SELECT                       BACK->  | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * S19 ships the screen *class*; S20 routes each icon; S21 layers the
 * home<->menu transition (horizontal slide, soft-key press flash).
 *
 * Implementation notes:
 *  - Code-only - all visuals come from the existing Phase-A widgets
 *    (PhoneSynthwaveBg, PhoneStatusBar, PhoneSoftKeyBar, PhoneMenuGrid +
 *    PhoneIconTile). No new SPIFFS assets; data partition cost stays zero.
 *  - The grid is anchored centered horizontally and pinned just below the
 *    status bar so it sits in the visible "main content" rectangle (10 px
 *    status bar at top, 10 px softkey bar at bottom, 108 px between them).
 *    7 tiles in a 4-column layout flow into row1: 4 + row2: 3 - the
 *    underfilled row matches what feature-phones do and PhoneMenuGrid's
 *    wrap navigation already handles the empty 4th cell.
 *  - The screen exposes `setOnSelect` / `setOnBack` plain-pointer hooks
 *    (same pattern as PhoneHomeScreen) so the host (S20) can route SELECT
 *    on a per-icon basis without subclassing. The screen also exposes
 *    `getSelectedIcon()` so the SELECT handler can dispatch.
 *  - The selected-tile caption (e.g. "PHONE", "MESSAGES") is *also* drawn
 *    by PhoneIconTile under the icon, but a feature-phone's main menu
 *    traditionally also shows the focused app's name in larger text in the
 *    free space at the bottom. We render that here as a single pixelbasic7
 *    label that updates whenever the grid's cursor moves.
 *  - BTN_BACK fires the optional back callback; if no callback is set it
 *    falls back to `pop(LV_SCR_LOAD_ANIM_MOVE_RIGHT)` so the unwound slide
 *    visually mirrors the home->menu push (S21).
 *  - Constructor takes no parameters - the icon list is hard-coded to the
 *    seven roadmap apps in the order specified by S19. If a future session
 *    needs a configurable list, lift it to a setter; for now the fixed
 *    layout is what the design calls for.
 */
class PhoneMainMenu : public LVScreen, private InputListener {
public:
	PhoneMainMenu();
	virtual ~PhoneMainMenu();

	void onStart() override;
	void onStop() override;

	using SoftKeyHandler = void (*)(PhoneMainMenu* self);

	/**
	 * Bind a callback to BTN_ENTER (the "SELECT" softkey / center A button).
	 * Pass nullptr to clear. The handler can call `getSelectedIcon()` to
	 * find out which tile the user landed on.
	 */
	void setOnSelect(SoftKeyHandler cb);

	/**
	 * Bind a callback to BTN_BACK (the "BACK" softkey). Pass nullptr to
	 * fall back to the default (`pop()`).
	 */
	void setOnBack(SoftKeyHandler cb);

	/**
	 * S22: Bind a callback to a long-press of BTN_0 from anywhere on the
	 * main menu (the same Sony-Ericsson "hold 0" quick-dial gesture wired
	 * on PhoneHomeScreen). Pass nullptr to clear; with no callback the
	 * gesture is silently ignored and BTN_0 short-press still does its
	 * normal thing (currently a no-op on this screen).
	 */
	void setOnQuickDial(SoftKeyHandler cb);

	/**
	 * S22: Bind a callback to a long-press of BTN_BACK from anywhere on
	 * the main menu (mirrors the home-screen lock gesture). Default
	 * (when nullptr) is to fall back to the screen's normal short-press
	 * BACK behaviour, which is the same screen as the back-tap target -
	 * so an unwired lock gesture is harmless.
	 */
	void setOnLockHold(SoftKeyHandler cb);

	/** Icon enum of the currently focused tile. Useful for SELECT dispatch. */
	PhoneIconTile::Icon getSelectedIcon() const;

	/** Flat index (0..6) of the currently focused tile. */
	uint8_t getSelectedIndex() const;

	/** Replace the visible label of the left softkey (default "SELECT"). */
	void setLeftLabel(const char* label);

	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);

	/**
	 * S21: trigger the press-feedback flash on the left/right softkey.
	 * Already invoked internally on BTN_ENTER / BTN_BACK; exposed publicly
	 * so the host can also flash from outside.
	 */
	void flashLeftSoftKey();
	void flashRightSoftKey();

	static constexpr uint8_t IconCount = 7;
	static constexpr uint8_t GridCols  = 4;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;
	PhoneMenuGrid*    grid;

	lv_obj_t*         caption;   // pixelbasic7 label, big app name under the grid

	SoftKeyHandler selectCb     = nullptr;
	SoftKeyHandler backCb       = nullptr;
	SoftKeyHandler quickDialCb  = nullptr;
	SoftKeyHandler lockHoldCb   = nullptr;

	// S22: prevent the short-press action from also firing on release
	// after a long-press shortcut already triggered.
	bool zeroLongFired = false;
	bool backLongFired = false;

	void buildGrid();
	void buildCaption();
	void refreshCaption();

	/** Pretty name for a roadmap-app icon. Used by refreshCaption(). */
	static const char* iconName(PhoneIconTile::Icon icon);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif //MAKERPHONE_PHONEMAINMENU_H
