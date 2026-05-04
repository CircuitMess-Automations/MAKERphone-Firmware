#ifndef MAKERPHONE_PHONESTRESSRELIEVER_H
#define MAKERPHONE_PHONESTRESSRELIEVER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneStressReliever -- S171
 *
 * Phase-S fidget-toy: a single "stress blob" sits centred on the
 * screen and reacts to every keypress. Tap it once and it squishes
 * + shows a calm face. Tap it faster and faster and its mood
 * escalates -- CALM -> RELAXED -> ENERGETIC -> FRANTIC -> DIZZY ->
 * DAZED -- with the blob colour, face glyph, and bottom mood label
 * all shifting in lockstep. Slots in beside PhoneFortuneCookie
 * (S133), PhoneCoinFlip (S132), PhoneMagic8Ball (S130),
 * PhoneDiceRoller (S131), PhoneFlashlight (S134) and PhoneAsciiArt
 * (S170) so the toy reads as an obvious sibling of the rest of
 * the fidget apps.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |          STRESS RELIEVER               | <- pixelbasic7 cyan
 *   |                                        |
 *   |             +----------+               |
 *   |             |          |               |
 *   |             |   ^_^    |    <- pixelbasic16 face glyph
 *   |             |          |       inside a 60x60 rounded blob
 *   |             +----------+               |
 *   |                                        |
 *   |              MOOD: CALM                | <- mood (colour-coded)
 *   |             TAPS: 0   STREAK x1        | <- counter line
 *   |   TAP                            BACK  | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Visual: a rounded-rect "blob" with a thin highlight border. A
 * single pixelbasic16 label sits centred inside it and cycles
 * through a handful of text faces (^_^, :3, O_O, >_<, @_@, X_X)
 * picked from a tone-keyed table. On every tap the blob squishes
 * (height -> 70%, width -> 110% for ~120 ms) then springs back; the
 * squish animation is driven by a single short-lived lv_timer so
 * the screen has zero per-frame cost in the idle case.
 *
 * Mood bands (re-evaluated on every tap; tap-count never decays):
 *   0       : CALM         -> cyan blob,    "^_^"
 *   1..5    : RELAXED      -> cream blob,   ":3"
 *   6..15   : ENERGETIC    -> orange blob,  "O_O"
 *   16..30  : FRANTIC      -> orange blob,  ">_<"
 *   31..60  : DIZZY        -> dim purple,   "@_@"
 *   61+     : DAZED        -> dim purple,   "X_X"
 *
 * The streak multiplier rewards rapid-fire tapping: each tap
 * arriving within StreakWindowMs (700 ms) of the previous one
 * bumps the streak counter up to a hard ceiling of MaxStreak (9).
 * Idle gaps reset the streak to 1. The streak is purely cosmetic
 * -- it shows next to the tap counter so the user can chase a
 * personal best without affecting the underlying mood band.
 *
 * Controls:
 *   - TAP: BTN_5 / BTN_ENTER / BTN_0..BTN_9 / BTN_R / left softkey
 *          Any of them taps the blob. There is no "active tap"
 *          guard -- the squish animation happily restarts mid-flight
 *          so a user mashing keys just gets continuous wobble (which
 *          is exactly the affordance a fidget toy is supposed to
 *          provide).
 *   - BTN_LEFT  : reset tap-count + streak to zero (instant calm).
 *   - BTN_RIGHT : same as TAP (alternate hand position).
 *   - BACK / right softkey / BTN_L / long-press BTN_BACK : pop.
 *
 * Implementation notes:
 *   - 100% code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the toy slots into the
 *     family without a visual seam.
 *   - The face / mood / colour tables live as `static constexpr`
 *     arrays in the .cpp, so a future skin or rebalance only
 *     touches one file.
 *   - The squish animation is a 4-frame lv_timer at 30 ms/frame
 *     (~120 ms total). On the final frame the timer auto-deletes
 *     and the blob returns to its rest size.
 *   - Tap counts are clamped at TapCountMax (999) so the on-screen
 *     counter never overflows the 3-digit field.
 *
 * S168 hook: the shake gesture (L+R held together) registers a
 * single tap, same as pressing TAP would. Forwards to tapBlob() so
 * mood bands / streak / squish all trigger the same way.
 */
class PhoneStressReliever : public LVScreen, private InputListener {
public:
	PhoneStressReliever();
	virtual ~PhoneStressReliever() override;

	void onStart() override;
	void onStop() override;

	// S168 -- shake gesture forwards to tapBlob().
	void onShake() override;

	/** Number of distinct mood bands. */
	static constexpr uint8_t MoodCount = 6;

	/** Hard ceiling on the on-screen tap counter (3 digits). */
	static constexpr uint16_t TapCountMax = 999;

	/** Hard ceiling on the streak multiplier (single digit). */
	static constexpr uint8_t  MaxStreak = 9;

	/** Window within which consecutive taps extend the streak. */
	static constexpr uint16_t StreakWindowMs = 700;

	/** Squish animation cadence + length. */
	static constexpr uint16_t SquishPeriodMs = 30;
	static constexpr uint8_t  SquishFrames   = 4;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs = 600;

	/** Tone enum for the colour-coded mood bands. */
	enum class Mood : uint8_t {
		Calm = 0,
		Relaxed,
		Energetic,
		Frantic,
		Dizzy,
		Dazed,
	};

	/** Compute the mood band for a given tap count. */
	static Mood moodFor(uint16_t tapCount);

	/** Human-readable name for a mood band ("CALM", "RELAXED", ...). */
	static const char* moodLabel(Mood mood);

	/** Pixelbasic16-friendly face glyph for a mood band ("^_^", "O_O", ...). */
	static const char* moodFace(Mood mood);

	/** Snapshot accessors -- handy for tests + future statistics
	 *  rollups (the eventual "highest streak" leaderboard, etc.). */
	uint16_t getTapCount() const { return tapCount; }
	uint8_t  getStreak()   const { return streak;   }
	Mood     getMood()     const { return moodFor(tapCount); }

	/** Register a tap: bump the counter, refresh the mood band, kick
	 *  the squish animation. Public so tests / the shake hook can
	 *  fire one without going through the input subsystem. */
	void tapBlob();

	/** Reset tap count + streak back to zero. Re-renders the idle
	 *  CALM face. */
	void resetState();

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Static caption ("STRESS RELIEVER").
	lv_obj_t* captionLabel;

	// The "blob" body and the face glyph nested inside it.
	lv_obj_t* blob;
	lv_obj_t* faceLabel;

	// Bottom mood line ("MOOD: CALM") and counter line ("TAPS: 42  STREAK x3").
	lv_obj_t* moodLineLabel;
	lv_obj_t* counterLineLabel;

	uint16_t  tapCount     = 0;
	uint8_t   streak       = 1;
	uint32_t  lastTapMs    = 0;

	uint8_t     squishLeft = 0;        // remaining squish frames
	lv_timer_t* squishTimer = nullptr;

	bool      backLongFired = false;

	void buildHud();

	/** Repaint the blob colour, face glyph, mood label, and counter
	 *  line for the current state. */
	void renderState();

	/** Apply a squish offset to the blob for the given frame. Frame
	 *  0 is the rest pose. */
	void applySquish(uint8_t frameIdx);

	void startSquishTimer();
	void stopSquishTimer();
	static void onSquishTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONESTRESSRELIEVER_H
