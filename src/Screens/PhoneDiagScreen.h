#ifndef MAKERPHONE_PHONE_DIAG_SCREEN_H
#define MAKERPHONE_PHONE_DIAG_SCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * S198 - PhoneDiagScreen
 *
 * Hidden battery-life / LVGL-cost diagnostics page surfaced by a long
 * press on BTN_R from PhoneAboutScreen. The Sony-Ericsson era had a
 * "service menu" that was deliberately hidden from the customer but
 * trivial to reach if you knew the gesture; this is the MAKERphone
 * equivalent.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |               DIAG                     | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  LOOP CADENCE                          | <- pixelbasic7 dim label
 *   |  60 fps  avg 16678 us                  | <- cream value
 *   |  PEAK / TOTAL                          |
 *   |  peak 23410 us   1234567 ticks         |
 *   |  IDLE DIM                              |
 *   |  BRIGHT  bl 192   idle 00:00:04        |
 *   |  HEAP                                  |
 *   |  142 KB free                           |
 *   |                                        |
 *   |                                 BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The page is intentionally text-only -- nothing here is editable -
 * BACK / ENTER are the only meaningful actions. A 1 Hz lv_timer drives
 * the live readouts so a developer triaging a battery-life regression
 * can park the device on this screen and watch the numbers move.
 *
 * Behaviour:
 *  - BTN_BACK / BTN_ENTER pop back to the page that pushed us
 *    (PhoneAboutScreen by default).
 *  - The 1 Hz lv_timer refreshes every value in place. The labels
 *    themselves are static.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Same wallpaper / status bar / softkey
 *    chrome as PhoneAboutScreen so the surface feels like part of
 *    the same family of read-only diag pages.
 *  - All values are read from the existing services (PhoneLvglCost,
 *    PhoneIdleDim, ESP runtime). No new state lives in this screen.
 *  - 160x128 budget: 10 px status bar + 10 px caption + four 20 px
 *    label/value groups starting at y=24 = 80 px = y=24..104, with
 *    the soft-key bar at y=118.
 */
class PhoneDiagScreen : public LVScreen, private InputListener {
public:
	PhoneDiagScreen();
	virtual ~PhoneDiagScreen() override;

	void onStart() override;
	void onStop() override;

	/** Live-update tick period (ms) for the rolling readouts. */
	static constexpr uint32_t kRefreshPeriodMs = 1000;

	/**
	 * Format the current PhoneIdleDim stage into a 9-char-or-shorter
	 * uppercase token ("BRIGHT", "DIM", "DEEP DIM"). Static so unit
	 * tests / hosts can introspect the formatting without standing
	 * up the screen.
	 */
	static const char* formatStage(uint8_t stageU8);

	/**
	 * Format a milliseconds-since-activity value into a fixed-width
	 * HH:MM:SS string. Cap is 99:59:59. Same out/outLen contract as
	 * PhoneAboutScreen::formatUptime.
	 */
	static void formatIdleClock(uint32_t ms, char* out, size_t outLen);

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;

	// Group 1: LOOP CADENCE - "60 fps  avg 16678 us"
	lv_obj_t* cadenceLabel;
	lv_obj_t* cadenceValue;

	// Group 2: PEAK / TOTAL - "peak 23410 us   1234567 ticks"
	lv_obj_t* peakLabel;
	lv_obj_t* peakValue;

	// Group 3: IDLE DIM - "BRIGHT  bl 192   idle 00:00:04"
	lv_obj_t* dimLabel;
	lv_obj_t* dimValue;

	// Group 4: HEAP - "142 KB free"
	lv_obj_t* heapLabel;
	lv_obj_t* heapValue;

	// S245 - single-line chime catalogue footer ("5 chimes  220-1568Hz")
	// anchored just above the soft-key bar. The catalogue does not
	// change at runtime so the line is computed ONCE on screen entry
	// rather than re-derived on every refresh tick.
	lv_obj_t* chimesSummary;

	lv_timer_t* tickTimer;

	void buildCaption();
	void buildBody();
	void buildChimesSummary();
	void refreshLiveFields();

	static void onTickTimer(lv_timer_t* timer);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONE_DIAG_SCREEN_H
