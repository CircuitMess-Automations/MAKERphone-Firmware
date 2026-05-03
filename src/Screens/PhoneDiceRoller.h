#ifndef MAKERPHONE_PHONEDICEROLLER_H
#define MAKERPHONE_PHONEDICEROLLER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneDiceRoller — S131
 *
 * Phase-P utility toy: a tabletop dice roller that supports the
 * standard polyhedral set (d4, d6, d8, d10, d12, d20) at either 1
 * die or 2 dice per roll. The user picks a mode, presses ROLL, and
 * watches a brief tumble before the result settles. Slots into the
 * eventual utility-apps grid alongside PhoneCalculator (S60),
 * PhoneAlarmClock (S124), PhoneTimers (S125), PhoneCurrencyConverter
 * (S126), PhoneUnitConverter (S127), PhoneWorldClock (S128),
 * PhoneVirtualPet (S129) and PhoneMagic8Ball (S130). Same
 * Sony-Ericsson silhouette every other Phone* screen wears so a
 * user navigating between them feels at home immediately:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |              DICE ROLLER               | <- pixelbasic7 cyan
 *   |                                        |
 *   |             <  2d20  >                 | <- pixelbasic16 selector
 *   |                                        |
 *   |       +------------------------+       |
 *   |       |          37            |       | <- pixelbasic16 total
 *   |       |        12 + 25         |       | <- pixelbasic7 breakdown
 *   |       +------------------------+       |
 *   |                                        |
 *   |      L/R: TYPE   0: COUNT   5: ROLL    | <- pixelbasic7 hint
 *   |   ROLL                          BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Visual: a single rounded-rect "tray" centered in the screen holds
 * the latest result. The mode selector (e.g. "2d20") sits above the
 * tray flanked by < > chevrons that ghost when the corresponding
 * cycle direction is unavailable (it never is today since we wrap,
 * but the slot is reserved for future modal locks).
 *
 * The 12 supported modes are produced by the cross-product of:
 *   - face counts: 4, 6, 8, 10, 12, 20
 *   - dice counts: 1, 2
 * The user cycles face counts with LEFT/RIGHT and toggles the dice
 * count with BTN_0 (or by holding the same direction key — see
 * Implementation notes). BTN_1..BTN_6 jump straight to a face count
 * (1=d4, 2=d6, 3=d8, 4=d10, 5=d12, 6=d20) keeping the current dice
 * count, so a user who has muscle memory from a tabletop session
 * can pick d20 with a single press.
 *
 * The roll animation is an 8-frame tumble at ~80 ms per frame —
 * matches PhoneMagic8Ball (S130) so the two toys feel like siblings.
 * During tumble both the total and the breakdown render in dim
 * purple; on settle the total snaps to the chosen accent based on
 * how the player did:
 *
 *   - critical low  (rolled all 1s)              -> sunset orange
 *   - critical high (rolled all max)             -> cyan
 *   - everything else                             -> warm cream
 *
 * Implementation notes:
 *   - 100% code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the toy slots into the
 *     family without a visual seam.
 *   - The 6 face counts live as a `static constexpr` table in the
 *     .cpp so a future "exotic dice" pack (d3, d100…) only needs to
 *     extend that table.
 *   - The tumble animation is driven by a single lv_timer that
 *     auto-deletes when the tumble completes. The screen does no
 *     per-frame work in the idle case.
 *   - Random source is rand() seeded from millis() XOR'd with a
 *     screen-specific magic, same pattern PhoneMagic8Ball uses, so
 *     two screens pushed in quick succession do not collapse onto
 *     the same outcome.
 *
 * Controls:
 *   - LEFT / BTN_LEFT  : previous face count (d4 < d6 < ... < d20, wraps)
 *   - RIGHT / BTN_RIGHT: next face count (wraps)
 *   - BTN_0            : toggle 1d / 2d
 *   - BTN_1..BTN_6     : jump to face d4 / d6 / d8 / d10 / d12 / d20
 *   - BTN_5 / BTN_ENTER / BTN_L / left softkey : ROLL
 *   - BTN_R / right softkey / BTN_BACK (short or long) : pop.
 *     (BTN_5 doubles as a roll shortcut even though it overlaps
 *     BTN_5 = "jump to d12" — by design: pressing 5 always rolls,
 *     mirroring the rest of the toy family where the centre digit
 *     is the universal "do the thing" key. BTN_1..BTN_4, BTN_6 do
 *     the face-count jump.)
 *
 * Re-pressing ROLL during an active tumble is a no-op (so a user
 * mashing 5 doesn't extend the animation indefinitely). Cycling
 * face counts mid-tumble is allowed — it changes the *next* roll's
 * mode but does not retroactively affect the in-flight tumble.
 */
class PhoneDiceRoller : public LVScreen, private InputListener {
public:
	PhoneDiceRoller();
	virtual ~PhoneDiceRoller() override;

	void onStart() override;
	void onStop() override;

	/** Number of supported face counts (d4, d6, d8, d10, d12, d20). */
	static constexpr uint8_t FaceCount = 6;

	/** Maximum dice per roll. The roadmap calls for 1d4 → 2d20 so we
	 *  cap at two. Result buffers are sized for this constant — bumping
	 *  it requires widening DiceMax-dependent sites. */
	static constexpr uint8_t DiceMax = 2;

	/** Frame cadence (ms) for the tumble animation. Matches S130. */
	static constexpr uint16_t TumblePeriodMs = 80;

	/** Number of tumble frames before the dice settle. 8 * 80 = 640 ms. */
	static constexpr uint8_t  TumbleFrames   = 8;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs     = 600;

	/** Look up a face count by table index (0..FaceCount-1). Returns
	 *  zero on out-of-range. */
	static uint8_t faceAt(uint8_t idx);

	/** Currently selected face count (4, 6, 8, 10, 12 or 20). Useful
	 *  for tests that want to introspect the screen. */
	uint8_t currentFace() const { return faceAt(faceIdx); }

	/** Currently selected dice count (1 or 2). */
	uint8_t currentCount() const { return diceCount; }

	/** Last settled roll values; valid for indices [0, currentCount()). */
	uint8_t lastRoll(uint8_t which) const {
		if(which >= DiceMax) return 0;
		return rollValues[which];
	}

	/** Sum of the most recent settled roll. Zero before the first roll. */
	uint16_t lastTotal() const { return rollTotal; }

	/** True iff the tumble animation is in progress. */
	bool isRolling() const { return rolling; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Static caption ("DICE ROLLER") and bottom hint.
	lv_obj_t* captionLabel;
	lv_obj_t* hintLabel;

	// Mode selector ("< 2d20 >") and the side chevrons.
	lv_obj_t* modeLabel;
	lv_obj_t* leftChev;
	lv_obj_t* rightChev;

	// Result tray (rounded rect) + total + per-die breakdown.
	lv_obj_t* tray;
	lv_obj_t* totalLabel;
	lv_obj_t* breakdownLabel;

	uint8_t   faceIdx        = 5;     // index into kFaces; default 5 -> d20
	uint8_t   diceCount      = 1;     // 1 or 2
	uint8_t   rollValues[DiceMax];    // last settled values
	uint16_t  rollTotal      = 0;     // sum of last settled values
	bool      hasRolled      = false; // suppresses the breakdown until first roll
	uint8_t   tumbleFrameLeft = 0;
	bool      rolling        = false;

	bool      backLongFired  = false;

	lv_timer_t* tumbleTimer = nullptr;

	void buildHud();

	/** Refresh the mode label ("Nd<F>") plus the chevron states. */
	void renderMode();

	/** Render the tray contents into the result panel. `tumbling`
	 *  paints in dim purple; otherwise the total is colored by tone
	 *  (critical-low / critical-high / normal). */
	void renderResult(bool tumbling);

	/** Pick a fresh roll, paint the first tumble frame, start timer. */
	void beginRoll();

	/** Cycle the face index by `delta` (-1 / +1), wrapping. */
	void cycleFace(int8_t delta);

	/** Toggle between 1d and 2d. */
	void toggleCount();

	/** Jump straight to face index `idx` (0..FaceCount-1). No-op if
	 *  out of range. Keeps current dice count. */
	void jumpToFace(uint8_t idx);

	void startTumbleTimer();
	void stopTumbleTimer();
	static void onTumbleTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEDICEROLLER_H
