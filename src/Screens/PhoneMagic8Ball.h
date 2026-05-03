#ifndef MAKERPHONE_PHONEMAGIC8BALL_H
#define MAKERPHONE_PHONEMAGIC8BALL_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneMagic8Ball — S130
 *
 * Phase-P fortune toy: classic Magic 8-Ball you "shake" by pressing
 * SHAKE / ENTER / any digit, then watch the answer triangle tumble
 * through the canon 20 answers before settling on one. Slots in
 * beside PhoneCalculator (S60), PhoneAlarmClock (S124), PhoneTimers
 * (S125), PhoneCurrencyConverter (S126), PhoneUnitConverter (S127),
 * PhoneWorldClock (S128) and PhoneVirtualPet (S129) inside the
 * eventual utility-apps grid. Same Sony-Ericsson silhouette every
 * other Phone* screen wears so a user navigating between them feels
 * at home immediately:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |             MAGIC 8 BALL               | <- pixelbasic7 cyan
 *   |          ___                           |
 *   |         /   \                          |
 *   |        |  8  |   <- pixelbasic16 "8"   |
 *   |        |     |   center triangle area  |
 *   |         \___/    holds the answer text |
 *   |                                        |
 *   |          ASK A QUESTION                | <- pixelbasic7 hint
 *   |   SHAKE                         BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Visual: the ball is two concentric LVGL circles -- a black outer
 * disc with a thin cyan border (the "8-ball"), and a smaller dim
 * purple inner disc (the "answer triangle window"). When the screen
 * is idle the inner disc shows a stylised pixelbasic16 "8" so the
 * toy reads as recognisably an 8-ball; when the user "shakes" it,
 * the inner disc cycles answers at ~80 ms per frame and the outer
 * ball wobbles ±2 px to fake the toss.
 *
 * The 20 answers are the classic Magic 8-Ball canon, split three
 * ways for colour-coding:
 *
 *   - 10 affirmative   (e.g. "IT IS CERTAIN")              -> cyan
 *   -  5 non-committal (e.g. "ASK AGAIN LATER")            -> cream
 *   -  5 negative      (e.g. "DON'T COUNT ON IT")          -> sunset orange
 *
 * The Magic 8-Ball's answers themselves are factual public domain
 * trivia, but they are surfaced verbatim so the toy reads exactly
 * like the original Mattel item. The colour-coding is a
 * MAKERphone addition -- 1971 plastic triangles obviously didn't do
 * that.
 *
 * Controls:
 *   - SHAKE: BTN_5 / BTN_ENTER / BTN_0..BTN_9 / BTN_L / left softkey
 *            Any of them triggers a fresh tumble. Re-pressing during
 *            an active shake is a no-op (so a user mashing 5 doesn't
 *            extend the animation indefinitely).
 *   - BACK / right softkey / BTN_R / long-press BTN_BACK : pop.
 *
 * Implementation notes:
 *   - 100% code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the toy slots into the
 *     family without a visual seam.
 *   - The 20 answers live as a `static constexpr` string table in
 *     the .cpp, so a future session can add the "extended pack" the
 *     1990s relaunches did without touching this header.
 *   - The shake animation is driven by a single lv_timer that
 *     auto-deletes when the tumble completes. The screen does no
 *     per-frame work in the idle case.
 *   - Random source is rand() seeded from millis() XOR'd with a
 *     screen-specific magic, same pattern PhoneSimon (S97) uses to
 *     keep two screens pushed in quick succession from collapsing
 *     onto the same outcome.
 */
class PhoneMagic8Ball : public LVScreen, private InputListener {
public:
	PhoneMagic8Ball();
	virtual ~PhoneMagic8Ball() override;

	void onStart() override;
	void onStop() override;

	/** Number of canon answers in the table. */
	static constexpr uint8_t AnswerCount = 20;

	/** Affirmative answers occupy [0, AffirmativeCount). */
	static constexpr uint8_t AffirmativeCount  = 10;
	/** Non-committal answers occupy [AffirmativeCount, NonCommittalEnd). */
	static constexpr uint8_t NonCommittalCount = 5;
	/** Negative answers occupy [NonCommittalEnd, AnswerCount). */
	static constexpr uint8_t NegativeCount     = 5;

	/** Frame cadence (ms) for the tumble animation. ~80 ms gives the
	 *  user a perceptible "blur" of answers without making a single
	 *  shake cost much frame budget. */
	static constexpr uint16_t ShakePeriodMs = 80;

	/** Number of tumble frames before the answer settles. 8 frames
	 *  at 80 ms = ~640 ms — long enough to feel like a real toss,
	 *  short enough that the user can re-shake quickly. */
	static constexpr uint8_t  ShakeFrames   = 8;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs    = 600;

	/** Categorical answer kind, derived from the answer index. */
	enum class Tone : uint8_t {
		Affirmative = 0,
		NonCommittal,
		Negative,
	};

	/** Look up an answer by its index. Returns nullptr if out of range. */
	static const char* answerAt(uint8_t idx);

	/** Look up the tone of an answer index. Defaults to NonCommittal
	 *  on out-of-range so a future caller cannot accidentally crash. */
	static Tone toneOf(uint8_t idx);

	/** Currently displayed (or last settled) answer index, useful for
	 *  tests that want to introspect the screen. */
	uint8_t getCurrentAnswer() const { return currentIdx; }

	/** True iff the tumble animation is in progress. */
	bool isShaking() const { return shaking; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Static caption ("MAGIC 8 BALL") and bottom hint ("ASK A QUESTION").
	lv_obj_t* captionLabel;
	lv_obj_t* hintLabel;

	// Two-disc 8-ball: outer black ball + inner purple "window".
	lv_obj_t* outerBall;
	lv_obj_t* innerWindow;

	// pixelbasic16 label inside the window. Holds either "8" while
	// idle or the current answer text while shaking / settled.
	lv_obj_t* answerLabel;

	uint8_t   currentIdx     = 0;     // last picked answer (0..AnswerCount-1)
	uint8_t   finalAnswerIdx = 0;     // the answer the current tumble will land on
	uint8_t   shakeFrameLeft = 0;     // tumble frames remaining
	bool      shaking        = false; // tumble in progress

	bool      backLongFired  = false;

	lv_timer_t* shakeTimer = nullptr;

	void buildHud();

	/** Render the current state into the inner window: either the idle
	 *  "8" (in cyan), the cycling tumble face (in dim purple), or a
	 *  settled answer (coloured by Tone). */
	void renderAnswer(uint8_t idx, bool tumbling);

	/** Reset the screen back to the idle "8" face. */
	void renderIdle();

	/** Begin a fresh tumble. No-op if a tumble is already in progress. */
	void beginShake();

	/** Wobble the outer ball by ±2 px on x and ±1 px on y, picked from
	 *  a tiny LUT keyed off the remaining frame count so the wobble
	 *  reads as deterministic-but-jittery rather than random. */
	void wobbleBall(uint8_t frameLeft);

	void startShakeTimer();
	void stopShakeTimer();
	static void onShakeTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEMAGIC8BALL_H
