#ifndef MAKERPHONE_PHONEBIRTHDAYREMINDERS_H
#define MAKERPHONE_PHONEBIRTHDAYREMINDERS_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Types.hpp"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneBirthdayReminders — S135
 *
 * Phase-P utility screen: a per-contact birthday reminder list.
 * Slots in beside PhoneCalculator (S60), PhoneAlarmClock (S124),
 * PhoneTimers (S125), PhoneCurrencyConverter (S126), PhoneUnitConverter
 * (S127), PhoneWorldClock (S128), PhoneVirtualPet (S129),
 * PhoneMagic8Ball (S130), PhoneDiceRoller (S131), PhoneCoinFlip (S132),
 * PhoneFortuneCookie (S133) and PhoneFlashlight (S134). Same retro
 * silhouette every other Phone* screen wears so a user navigating
 * between them feels at home immediately:
 *
 *   List view (default — at least one contact has a birthday set):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |             BIRTHDAYS                  | <- pixelbasic7 cyan
 *   |              UPCOMING                  | <- pixelbasic7 dim
 *   |   > MARKO         TODAY!     03 MAY  | <- accent row (today)
 *   |     ANA           IN  3 DAYS 06 MAY  | <- cream rows
 *   |     LUKA          IN 12 DAYS 15 MAY  |
 *   |     IVAN          IN 87 DAYS 28 JUL  |
 *   |     PETRA         IN 200 DAYS  17 NOV |
 *   |                                BACK    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 *   Empty view (no contact has the HasBirthday flag set):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |             BIRTHDAYS                  |
 *   |              UPCOMING                  |
 *   |                                        |
 *   |        NO BIRTHDAYS YET.               |
 *   |        SET A BIRTHDAY ON               |
 *   |        A CONTACT TO SEE IT             |
 *   |        APPEAR HERE.                    |
 *   |                                BACK    |
 *   +----------------------------------------+
 *
 * The screen is a *read-only* list — the per-contact birthday-edit UX
 * lands in a follow-up session that extends PhoneContactEdit. The
 * storage helpers (`PhoneContacts::setBirthday` / `clearBirthday` /
 * `birthdayOf`) are wired up in this session so the future edit
 * screen has nothing to invent and so a unit test or REPL session
 * can already exercise the list.
 *
 * Sort order: rising days-until-next-occurrence. "Today!" rows are
 * always at the top (zero days until next occurrence). Ties (two
 * contacts on the same calendar day) break alphabetically by display
 * name, so the list is stable across reloads.
 *
 * Controls:
 *   - BTN_2 / BTN_UP   : cursor up (no-op when empty).
 *   - BTN_8 / BTN_DOWN : cursor down (no-op when empty).
 *   - BTN_BACK / BTN_R / right softkey : pop the screen.
 *
 * Implementation notes:
 *   - 100 % code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen drops into the
 *     family without a visual seam.
 *   - The list pulls from `Storage.PhoneContacts.all()` on every
 *     onStart() so an edit elsewhere is reflected on next open. There
 *     is no live timer — the screen is event-driven by user input.
 *   - The "days until" column is computed against `PhoneClock::now()`
 *     under the same leap-year-free calendar the rest of the firmware
 *     uses, so a Feb-29 birthday simply rolls forward to the next
 *     real date. Two devices with the same wall-clock produce
 *     identical sort orders.
 *   - At most `MaxRows` (5) rows fit on the 128 px display below the
 *     caption strip; the cursor scrolls a virtual list of all
 *     contacts so the user can reach the tail without paging
 *     visually. Off-screen entries simply aren't rendered.
 */
class PhoneBirthdayReminders : public LVScreen, private InputListener {
public:
	PhoneBirthdayReminders();
	virtual ~PhoneBirthdayReminders() override;

	void onStart() override;
	void onStop() override;

	/** Maximum number of contact birthdays the list will surface at
	 *  once. Five rows fit comfortably between the caption strip and
	 *  the soft-key bar at pixelbasic7 with 14 px row height. Any
	 *  contacts past this limit live "below the fold" and become
	 *  visible by scrolling the cursor. */
	static constexpr uint8_t MaxRows      = 5;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs  = 600;

	/** Pixel height of one row in the list view. */
	static constexpr lv_coord_t RowHeight = 14;

	// --- Public computational helpers, exposed for testability. ---

	/**
	 * Days until the next occurrence of (birthMonth, birthDay) given
	 * a "today" of (todayMonth, todayDay). Both inputs are clamped to
	 * 1..12 / 1..31. Uses the leap-year-free 365-day calendar
	 * PhoneClock already uses, so a Feb-29 birthday produces the
	 * "always 1 day after Feb 28" answer (matching the screen's
	 * documented behaviour). Returns 0 when the date matches today,
	 * otherwise a value in [1..364].
	 */
	static uint16_t daysUntil(uint8_t todayMonth, uint8_t todayDay,
	                          uint8_t birthMonth, uint8_t birthDay);

	/**
	 * Day-of-year (1..365) for the supplied (month, day) pair, under
	 * the same leap-year-free calendar. Out-of-range inputs are
	 * clamped to the valid range before the conversion, so the
	 * helper never returns 0 for a valid month >= 1.
	 */
	static uint16_t dayOfYear(uint8_t month, uint8_t day);

private:
	// One precomputed row in the sorted "upcoming" list. Captured at
	// onStart() so the LVGL labels can render without re-walking the
	// storage layer on every refresh.
	struct Entry {
		UID_t    uid;
		char     name[24];   // Mirrors PhoneContact::displayName length.
		uint8_t  month;      // 1..12
		uint8_t  day;        // 1..31
		uint16_t daysUntil;  // 0..364
	};

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;
	lv_obj_t* subCaptionLabel;
	lv_obj_t* emptyLabel;             // Multi-line "no birthdays yet" hint.
	lv_obj_t* rowLabels[MaxRows];     // One pixelbasic7 label per visible row.

	// Snapshot of the sorted "upcoming birthdays" list rebuilt every
	// onStart(). Capped at 32 contacts to keep RAM bounded — well
	// above the realistic phone-book size for a Chatter device.
	static constexpr uint8_t MaxEntries = 32;
	Entry    entries[MaxEntries];
	uint8_t  entryCount = 0;

	// Cursor index into `entries` (0..entryCount-1). The visible
	// window is always anchored so that `cursor` is on screen — the
	// helper updateScroll() recomputes the top entry when the cursor
	// moves out of the current window.
	uint8_t  cursor    = 0;
	uint8_t  scrollTop = 0;

	bool backLongFired = false;

	void buildHeader();
	void buildRows();
	void buildEmptyHint();

	void rebuildList();
	void refresh();
	void updateScroll();

	void cursorUp();
	void cursorDown();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEBIRTHDAYREMINDERS_H
