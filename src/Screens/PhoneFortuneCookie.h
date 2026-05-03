#ifndef MAKERPHONE_PHONEFORTUNECOOKIE_H
#define MAKERPHONE_PHONEFORTUNECOOKIE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneFortuneCookie — S133
 *
 * Phase-P fortune toy: a tiny Sony-Ericsson-style "open one cookie a
 * day" widget. Slots in beside PhoneCalculator (S60), PhoneAlarmClock
 * (S124), PhoneTimers (S125), PhoneCurrencyConverter (S126),
 * PhoneUnitConverter (S127), PhoneWorldClock (S128), PhoneVirtualPet
 * (S129), PhoneMagic8Ball (S130), PhoneDiceRoller (S131) and
 * PhoneCoinFlip (S132). Same retro silhouette every other Phone*
 * screen wears so a user navigating between them feels at home:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |             FORTUNE COOKIE             | <- pixelbasic7 cyan
 *   |               TODAY'S WISDOM           | <- pixelbasic7 dim
 *   |                                        |
 *   |              .---------.               |
 *   |             (    ?      )              | <- closed cookie
 *   |              `---------'               |
 *   |                                        |
 *   |          PRESS CRACK TO OPEN           | <- pixelbasic7 dim
 *   |   CRACK                         BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * After CRACK:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |             FORTUNE COOKIE             |
 *   |               TODAY'S WISDOM           |
 *   |  +----------------------------------+  |
 *   |  | A SMILE TODAY OPENS A DOOR       |  | <- paper strip
 *   |  | TOMORROW.                        |  |   (cream bg, cyan
 *   |  +----------------------------------+  |    border, fortune
 *   |       LUCKY: 04 11 23 35 41            |    text wraps)
 *   |   AGAIN                         BACK   | <- pixelbasic7 cream
 *   +----------------------------------------+
 *
 * Visual: the closed cookie is a single golden-orange rounded rect
 * with a darker cream "fold" line stroked across it and a
 * pixelbasic16 "?" inside. Cracking is a brief ~360 ms wobble (six
 * 60 ms frames) where the cookie shakes ±2 px and the "?" cycles
 * between three glyphs ("?", "!", "*") to fake the contents tumbling.
 * On the final frame the cookie hides and the paper strip fades in
 * with the chosen fortune and lucky numbers.
 *
 * The fortune is **deterministic for the day**: an integer day index
 * derived from `PhoneClock::nowEpoch() / 86400UL` modulo
 * `FortuneCount`. Pressing CRACK on the same wall-clock day always
 * reveals the same wisdom — the toy is a once-a-day ritual, not a
 * randomiser. The lucky numbers are also derived from the day index
 * via a small LCG, so they too are stable for the day. Pressing
 * AGAIN after the cookie is open re-runs the crack animation but
 * lands on the same wisdom (so the user can "watch it open" again
 * without losing the day's message). A future session can layer
 * "advance to tomorrow's preview" on top of this without touching the
 * existing API.
 *
 * Controls:
 *   - BTN_5 / BTN_ENTER / BTN_L / left softkey ("CRACK"/"AGAIN") :
 *       opens the cookie or replays the crack animation.
 *   - BTN_0 : closes the cookie back to the idle "?" face. Useful
 *       for a clean re-entry without leaving the screen.
 *   - BTN_R / right softkey / BTN_BACK (short or long) : pop.
 *
 * Re-pressing CRACK during an active animation is a no-op so a user
 * mashing 5 cannot extend the tumble.
 *
 * Implementation notes:
 *   - 100% code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the toy slots into the
 *     family without a visual seam.
 *   - The 32 fortunes live as a `static constexpr` string table in
 *     the .cpp, original phrasings (no quoted material from the
 *     classic-cookie canon).
 *   - The crack animation is driven by a single lv_timer that
 *     auto-deletes when the wobble completes. The screen does no
 *     per-frame work in the idle case.
 *   - Random source for the lucky-number LCG is seeded purely from
 *     the day index, so two devices set to the same date produce
 *     the same lucky numbers — the screen is a toy, not a draw.
 */
class PhoneFortuneCookie : public LVScreen, private InputListener {
public:
	PhoneFortuneCookie();
	virtual ~PhoneFortuneCookie() override;

	void onStart() override;
	void onStop() override;

	/** Number of fortunes in the inline table. */
	static constexpr uint8_t  FortuneCount     = 32;

	/** Frame cadence (ms) for the crack animation. ~60 ms keeps the
	 *  cookie wobble snappy without breaking frame budget. */
	static constexpr uint16_t CrackPeriodMs   = 60;

	/** Number of crack frames before the cookie settles open.
	 *  6 frames at 60 ms = ~360 ms — enough to feel like a real
	 *  crack, short enough that the user can re-replay quickly. */
	static constexpr uint8_t  CrackFrames     = 6;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs      = 600;

	/** How many "lucky numbers" the paper carries. */
	static constexpr uint8_t  LuckyNumberCount = 5;

	/** Maximum lucky number value (numbers run 1..LuckyNumberMax). */
	static constexpr uint8_t  LuckyNumberMax   = 49;

	/** Look up a fortune by its index. Returns nullptr if out of range. */
	static const char* fortuneAt(uint8_t idx);

	/** Pick the fortune-of-the-day for a given day index. Pure
	 *  function, factored out so tests can introspect the rotation. */
	static uint8_t fortuneOfDay(uint32_t dayIndex);

	/** Generate the day's lucky numbers into out[]. The numbers are
	 *  unique within the array and stable for the same dayIndex.
	 *  out[] is sorted ascending so the printed line reads naturally. */
	static void luckyNumbersForDay(uint32_t dayIndex,
	                               uint8_t out[LuckyNumberCount]);

	/** True iff the cookie is fully cracked open and the paper is
	 *  showing. Idle (closed) and cracking-in-progress both return
	 *  false. */
	bool isOpen()      const { return open; }

	/** True iff the wobble animation is currently running. */
	bool isCracking()  const { return cracking; }

	/** Index of the fortune currently shown. Equals fortuneOfDay()
	 *  for the day the screen was opened. */
	uint8_t currentFortuneIdx() const { return currentIdx; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Static caption ("FORTUNE COOKIE") and the date sub-line below it.
	lv_obj_t* captionLabel;
	lv_obj_t* dateLabel;
	lv_obj_t* hintLabel;

	// Closed-cookie body, fold line, and the "?" glyph inside.
	lv_obj_t* cookieBody;
	lv_obj_t* cookieFold;
	lv_obj_t* cookieMark;

	// Open-state paper strip + the wrapped fortune text + lucky line.
	lv_obj_t* paperStrip;
	lv_obj_t* fortuneLabel;
	lv_obj_t* luckyLabel;

	uint8_t   currentIdx     = 0;     // fortune index currently shown
	uint32_t  currentDayIdx  = 0;     // day index used to derive the above
	bool      open           = false; // paper visible?
	bool      cracking       = false; // wobble running?
	uint8_t   crackFrameLeft = 0;     // wobble frames remaining

	bool      backLongFired  = false;

	lv_timer_t* crackTimer = nullptr;

	void buildHud();

	/** Snapshot the current PhoneClock wall time and compute the
	 *  day index + fortune index + lucky numbers. Updates the date
	 *  label in place. Safe to call multiple times during the
	 *  screen's lifetime. */
	void refreshFromClock();

	/** Render the closed-cookie state ("?" face, paper hidden). */
	void renderClosed();

	/** Render the fully-open state (paper visible with today's
	 *  fortune + lucky numbers, cookie hidden). */
	void renderOpen();

	/** Paint a single wobble frame of the crack animation. The
	 *  caller picks which decoy glyph to show inside the cookie. */
	void renderCrackFrame(uint8_t frameLeft);

	/** Begin a fresh crack. No-op if a crack is already running. */
	void beginCrack();

	/** Force the screen back to the closed-cookie state. */
	void closeCookie();

	void startCrackTimer();
	void stopCrackTimer();
	static void onCrackTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEFORTUNECOOKIE_H
