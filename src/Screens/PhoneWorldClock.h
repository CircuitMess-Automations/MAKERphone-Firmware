#ifndef MAKERPHONE_PHONEWORLDCLOCK_H
#define MAKERPHONE_PHONEWORLDCLOCK_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneWorldClock - S128
 *
 * Phase-P utility screen: a six-zone world clock grid. Slots in next
 * to PhoneCalculator (S60), PhoneAlarmClock (S124), PhoneTimers (S125),
 * PhoneCurrencyConverter (S126) and PhoneUnitConverter (S127) inside
 * the eventual utility-apps grid. Same Sony-Ericsson silhouette every
 * other Phone* screen wears so a user navigating through them feels at
 * home immediately:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |             WORLD CLOCK                | <- pixelbasic7 cyan
 *   |   LON HOME       NYC -5                | <- pixelbasic7 captions
 *   |   12:34          07:34                 | <- pixelbasic16 readouts
 *   |   LAX -8         TYO +9                |
 *   |   04:34          21:34                 |
 *   |   SYD+10         DXB +4                |
 *   |   22:34          16:34                 |
 *   |   HOME                          BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Six fixed zones (2 cols x 3 rows). One of the six is the HOME zone,
 * which renders the device's PhoneClock::now() as-is; the other five
 * are derived by adding (zoneOffset - homeOffset) seconds to the
 * device's time-of-day. There is no notion of a "real" UTC anchor on
 * Chatter (no RTC, no network), so the world-clock pretends the
 * device's wall clock is the local time of whichever zone the user
 * has tagged HOME, and renders the others relative to that. This is
 * the same simplification a 2003 feature phone would use when the
 * user's GSM provider hasn't pushed a TZ broadcast yet.
 *
 * Controls:
 *   - BTN_2 / BTN_8       : move cursor up / down (wraps within column).
 *   - BTN_4 / BTN_LEFT    : move cursor left (wraps within row).
 *   - BTN_6 / BTN_RIGHT   : move cursor right (wraps within row).
 *   - BTN_5 / BTN_ENTER / BTN_L (left softkey "HOME") : promote the
 *                           highlighted cell to HOME. The captions on
 *                           every other cell update on the next frame
 *                           so the offsets read relative to the new
 *                           home instantly.
 *   - BTN_R / BTN_BACK    : pop the screen.
 *
 * Implementation notes:
 *   - 100% code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen drops into the
 *     family without a visual seam.
 *   - The six zone slots are a `static constexpr` table in the .cpp,
 *     so a future session can grow / reorder the list without touching
 *     this header.
 *   - Time recompute runs from a 1 s lv_timer that simply rebuilds the
 *     six readout labels. We never reach for the LVGL animation system
 *     here so the screen is cheap to keep open in the background.
 *   - The HOME selection lives only in the screen's RAM; popping the
 *     screen forgets it. A future session can persist it via
 *     ProfileService if the feature graduates to a permanent setting.
 */
class PhoneWorldClock : public LVScreen, private InputListener {
public:
	PhoneWorldClock();
	virtual ~PhoneWorldClock() override;

	void onStart() override;
	void onStop() override;

	/** A single world-clock zone descriptor. Public so the file-scope
	 *  zone-table definition in PhoneWorldClock.cpp can use it. */
	struct Zone {
		const char* code;        // 3-letter city tag, e.g. "LON" / "TYO"
		int16_t     offsetMin;   // signed offset from the home zone's
		                         // base (minutes). All defaults are
		                         // whole hours but the field is in
		                         // minutes so future zones (e.g. IST
		                         // at +5:30, NPT at +5:45) drop in
		                         // without changing this header.
	};

	/** The fixed grid layout: 2 cols x 3 rows = 6 cells. */
	static constexpr uint8_t Cols      = 2;
	static constexpr uint8_t Rows      = 3;
	static constexpr uint8_t ZoneCount = Cols * Rows;

	/** Tick cadence (ms) for the time-recompute timer. 1 s is enough
	 *  to keep the HH:MM readouts fresh without burning frame budget. */
	static constexpr uint16_t TickPeriodMs = 1000;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs   = 600;

	/** Read-only access to a zone descriptor. Returns nullptr if the
	 *  index is out of range. */
	static const Zone* zoneAt(uint8_t idx);

	/**
	 * Decompose a base epoch second + an offset (minutes) into a
	 * time-of-day HH/MM pair. Static and side-effect-free for the
	 * same testability reason PhoneCalculator::applyOp() and the
	 * other Phone* statics are factored that way.
	 *
	 *   - `epoch`   : base epoch second (PhoneClock::nowEpoch())
	 *   - `delta`   : signed offset in minutes to apply BEFORE the
	 *                 modulo. Use (zone.offsetMin - home.offsetMin)
	 *                 to render any zone relative to a chosen home.
	 *   - `hourOut` : 0..23
	 *   - `minOut`  : 0..59
	 *
	 * Negative deltas are handled correctly via a modulo-with-bias so
	 * the time-of-day always lands in [0, 86400).
	 */
	static void timeOfDay(uint32_t epoch, int32_t delta,
	                      uint8_t& hourOut, uint8_t& minOut);

	/** Currently highlighted cell (0..ZoneCount-1). Useful for tests
	 *  / hosts that introspect the screen state. */
	uint8_t getCursor() const { return cursor; }

	/** Currently selected HOME zone (0..ZoneCount-1). Defaults to 0
	 *  (LON) at construction. */
	uint8_t getHome() const { return homeIdx; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Top caption ("WORLD CLOCK") + per-cell label pairs.
	lv_obj_t* captionLabel;
	lv_obj_t* cellCaption[ZoneCount];   // pixelbasic7 "LON +0" / "LON HOME"
	lv_obj_t* cellTime   [ZoneCount];   // pixelbasic16 "12:34"

	uint8_t   cursor  = 0;     // cell index 0..ZoneCount-1
	uint8_t   homeIdx = 0;     // cell index 0..ZoneCount-1

	bool      backLongFired = false;

	lv_timer_t* tickTimer = nullptr;

	void buildCells();
	void refreshAll();
	void refreshCells();
	void refreshSoftKeys();

	/** Move the cursor in row/col space, wrapping within the grid. */
	void moveCursor(int8_t deltaCol, int8_t deltaRow);

	/** Promote the cursor cell to HOME. No-op if it is already HOME. */
	void promoteHome();

	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEWORLDCLOCK_H
