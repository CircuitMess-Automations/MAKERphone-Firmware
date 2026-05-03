#ifndef MAKERPHONE_PHONEMOODLOG_H
#define MAKERPHONE_PHONEMOODLOG_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneMoodLog - S139
 *
 * Phase-Q "Pocket Organiser" one-tap-per-day mood journal. Sits next to
 * PhoneTodo (S136), PhoneHabits (S137) and PhonePomodoro (S138) inside
 * the eventual organiser-apps grid. The user records exactly one mood
 * per day out of five preset levels; the screen renders the last 30
 * days as a coloured strip plus a stats summary so trends are visible
 * without leaving the screen.
 *
 *   View:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             MOOD LOG                   | <- caption (cyan, pixelbasic7)
 *   |  ------------------------------------- |
 *   |  ##|##|..|##|##|##|##|##|..|##|##|##  | <- 30-day strip
 *   |  TODAY WED 03 MAY                      | <- cursor day caption
 *   |  [###]   GREAT                         | <- 16x14 swatch + label
 *   |                                        |
 *   |  AVG 3.4/5      LOGGED 21/30           | <- stats line
 *   |  STREAK 5d                             | <- streak line
 *   |  1=AWFUL 2=BAD 3=OK 4=GOOD 5=GREAT     | <- key hint
 *   |  ------------------------------------- |
 *   |  TODAY                          CLEAR  | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Mood scale (1..5, 0 == "not logged"):
 *   1 -> Awful (deep red)
 *   2 -> Bad   (sunset orange)
 *   3 -> Okay  (muted purple, MP_LABEL_DIM)
 *   4 -> Good  (cyan)
 *   5 -> Great (soft yellow)
 *
 * 30-day strip:
 *   30 cells, leftmost = 29 days ago, rightmost = today. Each cell is
 *   4x8 px on a 5 px stride, totalling 30 * 5 = 150 px wide so the
 *   strip sits centred under the caption with ~5 px of side padding.
 *   The currently-browsed cell wears a 1 px MP_HIGHLIGHT outline ring
 *   so the eye can track it across the strip.
 *
 * Day rollover:
 *   On every load() and on every screen entry the screen shifts the
 *   30-byte history left by (todayDay - lastSyncDay). entries[i] is
 *   (HistoryDays - 1 - i) days ago, so entries[HistoryDays - 1] is
 *   "today" and entries[0] is "29 days ago". A clock-set-backward
 *   leaves history alone (delta <= 0). The day index is
 *   PhoneClock::nowEpoch() / 86400, exactly as PhoneHabits (S137)
 *   does it, so a habit that ticks today and a mood that logs today
 *   share one definition of "day".
 *
 * Stats:
 *   - Average: 10 * sum(logged) / count(logged) so 3.4/5 prints with
 *     one decimal. Empty -> "AVG --/5".
 *   - Logged: number of non-zero entries out of HistoryDays.
 *   - Streak: consecutive non-zero entries from today (rightmost)
 *     backwards. Breaks on the first 0.
 *
 * Persistence:
 *   One 38-byte NVS blob in namespace "mpmood" / key "m". Layout:
 *
 *     [0]    magic 'M'
 *     [1]    magic 'P'
 *     [2]    version (1)
 *     [3]    reserved (0)
 *     [4..7] lastSyncDay (uint32 LE)
 *     [8..37] 30 bytes of moods (0 = none, 1..5)
 *
 *   Read failure (missing blob, bad magic, NVS error) leaves the
 *   in-memory history all-zero and the screen runs RAM-only. Writes
 *   are best-effort and fail-soft, the same way PhoneHabits /
 *   PhoneVirtualPet / PhoneAlarmService degrade.
 *
 * Controls:
 *   - BTN_1..BTN_5      : set the cursor day's mood to that level
 *                         (1=AWFUL, 2=BAD, 3=OKAY, 4=GOOD, 5=GREAT).
 *                         The cursor stays put so the user can scan
 *                         their entries.
 *   - BTN_L             : cursor left in the 30-day strip (older).
 *   - BTN_R             : cursor right in the 30-day strip (newer).
 *   - BTN_LEFT softkey  : "TODAY"  -> jump cursor to today.
 *   - BTN_RIGHT softkey : "CLEAR"  -> clear the cursor day's entry.
 *   - BTN_ENTER         : alias for "TODAY" (jump cursor to today).
 *   - BTN_BACK          : pop the screen.
 *
 * Implementation notes:
 *   - 100 % code-only - no SPIFFS asset growth. Reuses PhoneSynthwaveBg
 *     / PhoneStatusBar / PhoneSoftKeyBar so the screen reads as part of
 *     the MAKERphone family.
 *   - 30 cell rectangles + a cursor-ring outline are pre-allocated at
 *     construction; refreshAll() repaints colours in place so the
 *     cursor-step path has zero allocation traffic.
 *   - The screen does not own a per-frame timer - paint cost is paid
 *     only on user input and on entry.
 */
class PhoneMoodLog : public LVScreen, private InputListener {
public:
	PhoneMoodLog();
	virtual ~PhoneMoodLog() override;

	void onStart() override;
	void onStop() override;

	/** Number of days kept in the rolling history. 30 == one month, the
	 *  size of the visible strip and the size of the on-disk array. */
	static constexpr uint8_t HistoryDays = 30;

	/** Number of mood levels (1..5). Level 0 means "not logged". */
	static constexpr uint8_t MoodLevels  = 5;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs = 600;

	/** Mood scale, 0 == not logged. Public so a host / test can
	 *  introspect state without standing up the screen. */
	enum class Mood : uint8_t {
		None  = 0,
		Awful = 1,
		Bad   = 2,
		Okay  = 3,
		Good  = 4,
		Great = 5,
	};

	/** Read-only accessors for tests / future hosts. */
	uint8_t getCursor()      const { return cursor; }
	Mood    getMoodForDay(uint8_t daysAgo) const;
	Mood    getTodayMood()   const { return getMoodForDay(0); }
	uint8_t getLoggedCount() const;
	uint8_t getStreak()      const;
	/** Average * 10, e.g. 34 == "3.4". 0 if no entries logged. */
	uint16_t getAverageX10() const;

	/** All-uppercase short name for a mood ("AWFUL", "BAD", etc.).
	 *  Returns "--" for Mood::None. Static + side-effect-free so a
	 *  host (or a test) can format without standing up the screen. */
	static const char* moodName(Mood m);

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	lv_obj_t* captionLabel  = nullptr;
	lv_obj_t* topDivider    = nullptr;
	lv_obj_t* bottomDivider = nullptr;

	lv_obj_t* cells[HistoryDays] = { nullptr };
	lv_obj_t* cursorRing         = nullptr;

	lv_obj_t* dayLabel    = nullptr;   // "TODAY WED 03 MAY"
	lv_obj_t* swatch      = nullptr;   // 16x14 cursor-day mood block
	lv_obj_t* moodLabel   = nullptr;   // "GREAT" / "--"
	lv_obj_t* statsLabel  = nullptr;   // "AVG 3.4/5  LOGGED 21/30"
	lv_obj_t* streakLabel = nullptr;   // "STREAK 5d"
	lv_obj_t* hintLabel   = nullptr;   // "1=AWFUL ... 5=GREAT"

	uint8_t  entries[HistoryDays] = {};
	uint8_t  cursor               = HistoryDays - 1;   // start on today
	uint32_t lastSyncDay          = 0;
	bool     backLongFired        = false;

	// builders / repainters
	void buildView();
	void refreshAll();
	void refreshStrip();
	void refreshDayCaption();
	void refreshSwatch();
	void refreshStats();
	void refreshSoftKeys();

	// model helpers
	void setCursorMood(Mood m);
	void clearCursor();
	void jumpToToday();
	void moveCursor(int8_t delta);

	// day rollover
	void syncToToday();
	static uint32_t todayIndex();

	// persistence
	void load();
	void save();

	// input
	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEMOODLOG_H
