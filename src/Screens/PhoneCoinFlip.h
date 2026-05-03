#ifndef MAKERPHONE_PHONECOINFLIP_H
#define MAKERPHONE_PHONECOINFLIP_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneCoinFlip — S132
 *
 * Phase-P utility toy: a single-coin flipper that tosses a coin into
 * the air, lets it tumble for a beat, and settles on heads or tails.
 * Slots into the eventual utility-apps grid alongside PhoneCalculator
 * (S60), PhoneAlarmClock (S124), PhoneTimers (S125),
 * PhoneCurrencyConverter (S126), PhoneUnitConverter (S127),
 * PhoneWorldClock (S128), PhoneVirtualPet (S129), PhoneMagic8Ball
 * (S130) and PhoneDiceRoller (S131). Same Sony-Ericsson silhouette
 * every other Phone* screen wears so a user navigating between them
 * feels at home immediately:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |               COIN FLIP                | <- pixelbasic7 cyan
 *   |                                        |
 *   |                  __                    |
 *   |                 ( H )                  | <- coin in mid-tumble
 *   |                  --                    |
 *   |              =========                 | <- baseline / shadow
 *   |                                        |
 *   |                HEADS                   | <- pixelbasic16 result
 *   |          H T H H T H T T H H           | <- last-10 history
 *   |        FLIPS:42  H:23  STREAK:3        | <- pixelbasic7 stats
 *   |   FLIP                          BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Visual: a small "coin" (rounded rect drawn as an oval by squashing
 * its width during rotation) bounces in a parabolic arc between a
 * starting baseline and a peak roughly two-thirds up the toss area.
 * As the coin moves it also "rotates" — implemented by oscillating
 * its rendered width with |cos(rot*t)| while the height stays put,
 * so the coin appears to spin edge-on through the air. The face
 * label ("H" / "T") follows the rotation: it shows on the wide
 * (cos > 0) frames and hides on the narrow (cos near 0) frames so
 * the eye reads it as a 3D tumble.
 *
 * The toss takes 14 frames at 60 ms (~840 ms total). During the toss
 * the result label below the coin reads "—" in dim purple; on
 * settle it snaps to "HEADS" / "TAILS" in the warm cream / cyan
 * the rest of the toy family uses.
 *
 * Below the result a tiny last-10 history strip remembers the
 * recent flips so a user playing best-of-three can see what they
 * just got. Below that, a stats line tracks the lifetime totals
 * (this session): total flips, heads count, and the current
 * consecutive-same streak. None of this persists to storage today —
 * S132 keeps the toy purely in-RAM.
 *
 * Implementation notes:
 *   - 100% code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the toy slots into the
 *     family without a visual seam.
 *   - The coin is a single LVGL object whose x / y / width are
 *     tweaked per frame from a single lv_timer. The label inside
 *     the coin is hidden when the rotation is edge-on (width below
 *     a small threshold) so the user reads it as a real spin.
 *   - The toss animation uses a single lv_timer that auto-deletes
 *     when the toss completes. The screen does no per-frame work
 *     in the idle case.
 *   - Random source is rand() seeded from millis() XOR'd with a
 *     screen-specific magic, same pattern PhoneMagic8Ball and
 *     PhoneDiceRoller use, so two toy screens pushed in quick
 *     succession do not collapse onto the same outcome.
 *
 * Controls:
 *   - BTN_5 / BTN_ENTER / BTN_L / left softkey : FLIP
 *   - BTN_0   : clear history + stats
 *   - BTN_R / right softkey / BTN_BACK (short or long) : pop.
 *
 * Re-pressing FLIP during an active toss is a no-op (so a user
 * mashing 5 doesn't extend the animation). Clearing history during
 * an active toss is allowed — it wipes prior counters but doesn't
 * abort the in-flight flip; that flip's result will be the first
 * entry of the freshly-cleared history.
 */
class PhoneCoinFlip : public LVScreen, private InputListener {
public:
	PhoneCoinFlip();
	virtual ~PhoneCoinFlip() override;

	void onStart() override;
	void onStop() override;

	enum Face : uint8_t {
		FaceHeads = 0,
		FaceTails = 1,
	};

	/** Number of flips remembered for the on-screen history strip. */
	static constexpr uint8_t HistorySize    = 10;

	/** Frame cadence (ms) for the toss animation. Slightly tighter
	 *  than the dice tumble because a coin flip is a snappier toy. */
	static constexpr uint16_t TossPeriodMs  = 60;

	/** Number of toss frames before the coin settles. 14 * 60 = 840 ms. */
	static constexpr uint8_t  TossFrames    = 14;

	/** Number of half-rotations the coin makes during the toss. The
	 *  rendered width oscillates as |cos(Rotations * pi * t)|; with
	 *  Rotations = 4 the coin completes two full rotations during
	 *  the air-time and lands face-on at settle, which reads as a
	 *  believable real-world flip. */
	static constexpr uint8_t  Rotations     = 4;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs    = 600;

	// --- introspection helpers (handy for future eval/tests) ---------

	/** Currently displayed face (last settled outcome). FaceHeads
	 *  before the first flip. */
	Face currentFace() const { return settledFace; }

	/** True iff a toss animation is in progress. */
	bool isFlipping() const { return tossing; }

	/** True once the user has flipped the coin at least once in the
	 *  current screen lifetime. */
	bool hasAnyFlip() const { return totalFlips > 0; }

	/** Total flips since the screen was opened (or last cleared). */
	uint16_t totalFlipCount() const { return totalFlips; }

	/** Total heads since the screen was opened (or last cleared). */
	uint16_t headsCount() const { return headsTotal; }

	/** Current consecutive-same streak length. The streak resets to 1
	 *  on the first flip after the screen opens, and to 1 each time
	 *  the outcome differs from the previous outcome. */
	uint8_t streakLength() const { return streak; }

	/** Read a slot of the history ring. `which` 0 is the most recent
	 *  entry, `HistorySize-1` is the oldest. Returns 0xFF for
	 *  unfilled slots (i.e. before the user has flipped that many
	 *  times this session). */
	uint8_t historyAt(uint8_t which) const;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Static caption ("COIN FLIP") and bottom hint.
	lv_obj_t* captionLabel;
	lv_obj_t* hintLabel;

	// The coin: an outer rounded-rect ("body") with a single-letter
	// label inside that flips between H and T as the coin rotates.
	lv_obj_t* coinBody;
	lv_obj_t* coinLabel;
	lv_obj_t* shadow;       // little ground-line oval that scales with airtime

	// Result + history + stats lines below the toss area.
	lv_obj_t* resultLabel;
	lv_obj_t* historyLabel;
	lv_obj_t* statsLabel;

	Face       settledFace  = FaceHeads;  // last *settled* outcome
	bool       tossing      = false;
	uint8_t    tossFrameLeft = 0;
	uint8_t    pendingFace  = FaceHeads;  // the outcome the in-flight toss will settle on

	// History ring -- newest at index 0. Slots beyond historyFill are
	// invalid. We never grow past HistorySize; a fresh flip rotates
	// the ring.
	Face       history[HistorySize];
	uint8_t    historyFill   = 0;

	// Lifetime stats (since screen opened or last clear).
	uint16_t   totalFlips    = 0;
	uint16_t   headsTotal    = 0;
	uint8_t    streak        = 0;
	bool       prevValid     = false;
	Face       prevFace      = FaceHeads;

	bool       backLongFired = false;

	lv_timer_t* tossTimer    = nullptr;

	void buildHud();

	/** Refresh the static result line below the coin. */
	void renderResult();

	/** Refresh the last-10 history strip. */
	void renderHistory();

	/** Refresh the lifetime stats line. */
	void renderStats();

	/** Paint a single toss frame at progress `t` in [0..1]. The
	 *  caller decides which face to display while the coin is
	 *  edge-on (very small width). */
	void renderTossFrame(uint8_t face, int yPx, int widthPx, bool edgeOn);

	/** Paint the resting coin (full width, baseline y, settled face). */
	void renderRestingCoin();

	/** Pick a fresh outcome and start the toss timer. */
	void beginToss();

	/** Wipe history + stats. Idle-only; safe at any time. */
	void clearHistory();

	void startTossTimer();
	void stopTossTimer();
	static void onTossTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONECOINFLIP_H
