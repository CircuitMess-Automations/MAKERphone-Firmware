#ifndef MAKERPHONE_PHONEDRUMKITSCREEN_H
#define MAKERPHONE_PHONEDRUMKITSCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneDrumKitScreen — S176
 *
 * Phase-S Easter-egg toy. The user opens the dialer and types the
 * service code `*#909#` (a Roland TR-909 nod -- the drum-machine
 * granddaddy of every kick-snare sample anyone has ever heard); the
 * dialer detects the sequence, clears the buffer and pushes this
 * screen. Each digit on the keypad (0-9) becomes a different drum
 * voice played through the Chatter's piezo buzzer, so the device
 * suddenly behaves like a tiny pocket beat-box.
 *
 * Layout (160x128) -- matches the dialer's 3x4 keypad silhouette
 * exactly, so the muscle memory of "I just typed 909 here" carries
 * straight over to "now I tap drums on those same keys":
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |              DRUM KIT                  | <- pixelbasic7 cyan caption
 *   |   PRESS 0-9                            | <- pixelbasic7 dim hint
 *   |     +-----++-----++-----+              |
 *   |     | 1   || 2   || 3   |              |  <- 3x4 pad grid in
 *   |     | KIK || SNR || HHT |              |     dialer-pad shape;
 *   |     +-----++-----++-----+              |     each cell shows the
 *   |     +-----++-----++-----+              |     digit (cyan) over
 *   |     | 4   || 5   || 6   |              |     a 3-letter drum
 *   |     | TML || TMM || TMH |              |     short-name (cream)
 *   |     +-----++-----++-----+              |
 *   |     +-----++-----++-----+              |
 *   |     | 7   || 8   || 9   |              |
 *   |     | CRS || COW || OHH |              |
 *   |     +-----++-----++-----+              |
 *   |              +-----+                   |
 *   |              | 0   |                   |  <- '*' / '#' slots
 *   |              | CLP |                   |     deliberately empty
 *   |              +-----+                   |     (the keypad has no
 *   |                                BACK    |     drum mapping for
 *   +----------------------------------------+     them, which keeps
 *                                                  the toy honest).
 *
 * The pad layout deliberately re-uses the same 3-column / 4-row
 * silhouette PhoneDialerPad ships, so the toy reads as "the dialer
 * keypad, but every key is a drum" rather than as a separate UI.
 * The four corners that would be `*` and `#` are blank: the drum
 * map is only digits.
 *
 * Drum voices (kDrums[]) are short 1-3-frame envelopes played on
 * the piezo buzzer. The envelope timer is a single lv_timer that
 * walks the active drum's frame array, calling Piezo.tone(freq,
 * frameDurMs) on each step and Piezo.noTone() when the envelope
 * runs out. A second press while a drum is still playing cancels
 * the in-flight envelope and starts the new one cleanly, so the
 * user can fire rapid hits without smearing voices into each other.
 *
 * The pad that produced the most-recent hit flashes for ~120 ms
 * (cyan glow on the orange accent rectangle) so even with sound
 * muted the user gets the feedback that the press registered.
 *
 * Behaviour:
 *   - BTN_0 .. BTN_9     -> hitDrum(digit) — plays the matching
 *                           drum voice + flashes the matching pad.
 *                           The pad keypad layout is dialer-style:
 *                             1 2 3
 *                             4 5 6
 *                             7 8 9
 *                               0
 *                           Slots above/below 0 are decorative
 *                           empty cells (no `*` / `#` mapping).
 *   - BTN_BACK / RIGHT   -> stop any in-flight envelope and pop()
 *                           back to the dialer.
 *   - BTN_ENTER          -> repeat the last drum hit (so the user
 *                           can string together hi-hats without
 *                           moving their thumb).
 *
 * Implementation notes:
 *   - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar
 *     / PhoneSoftKeyBar verbatim so the toy lives in the same visual
 *     family as every other Phase-S Easter-egg screen.
 *   - Sound respect: hits stay silent when Settings.get().sound is
 *     false. Visual flash always fires regardless, so the device
 *     reads as responsive even in Mute / Vibrate profile.
 *   - Single-voice (monophonic) by design — the piezo can only do
 *     one frequency at a time, and trying to mix two drum hits
 *     would just produce a buzz. Cancel-and-start is the
 *     drum-machine-correct behaviour anyway (a kick that landed
 *     50 ms ago has already played; cutting it off to land the
 *     snare is what every real machine does).
 *   - drumForDigit() is exposed static so a future test (or future
 *     screen, e.g. an alarm clock that uses the kick voice) can
 *     introspect the drum table without standing the screen up.
 */
class PhoneDrumKitScreen : public LVScreen, private InputListener {
public:
	PhoneDrumKitScreen();
	virtual ~PhoneDrumKitScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of drum voices — one per digit 0..9. */
	static constexpr uint8_t NumDrums = 10;

	/** Largest envelope length we support (frames per drum). */
	static constexpr uint8_t MaxFrames = 3;

	/**
	 * One step of a drum's envelope. `freq` in Hz, `durMs` in
	 * milliseconds. A `freq == 0` step plays silence for that
	 * many ms, which is how the clap voice gets its
	 * tone-silence-tone double-tap shape without a second voice.
	 */
	struct Frame {
		uint16_t freq;
		uint16_t durMs;
	};

	/** A single drum voice — short name (3 letters max), full name,
	 *  and 1..MaxFrames envelope frames. Frames after `frameCount`
	 *  are unused. */
	struct Drum {
		const char* shortName;   // 3-letter pad caption ("KIK", "SNR")
		const char* longName;    // full label for the now-playing strip
		uint8_t     frameCount;  // 1..MaxFrames
		Frame       frames[MaxFrames];
	};

	/**
	 * Drum table. Indexed by digit 0..9. Slot 0 is the CLAP voice
	 * (intentionally placed on the bottom-row keypad cell to mirror
	 * the dialer-pad slot 0 lives in), slots 1..9 are arranged
	 * top-left to bottom-right across the standard 3x3 keypad.
	 */
	static const Drum kDrums[NumDrums];

	/**
	 * Look up the drum voice for the given digit. Returns nullptr
	 * if `digit` is out of range. Static so a host (e.g. a future
	 * alarm clock playback path that wants to re-use the KICK
	 * voice) can introspect without standing up the screen.
	 */
	static const Drum* drumForDigit(uint8_t digit);

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// ---- header (static) labels --------------------------------
	lv_obj_t* captionLabel;        // "DRUM KIT"
	lv_obj_t* hintLabel;           // "PRESS 0-9"
	lv_obj_t* nowPlayingLabel;     // "NOW: KICK" (updates per hit)

	// ---- pad grid ----------------------------------------------
	// Cells are arranged in the 3x4 dialer-pad silhouette:
	//   row 0:  pads[1] pads[2] pads[3]
	//   row 1:  pads[4] pads[5] pads[6]
	//   row 2:  pads[7] pads[8] pads[9]
	//   row 3:  ( empty ) pads[0] ( empty )
	// Index into `pads` matches the digit so hitDrum(digit) can
	// flash pads[digit] directly without a translation table.
	lv_obj_t* pads[NumDrums];

	// ---- envelope playback state ------------------------------
	const Drum* activeDrum    = nullptr;
	uint8_t     activeFrameIx = 0;
	lv_timer_t* envTimer      = nullptr;

	// ---- pad-flash state --------------------------------------
	int8_t      flashingPad   = -1;        // -1 = none
	lv_timer_t* flashTimer    = nullptr;

	void buildHeader();
	void buildPadGrid();
	void buildPad(uint8_t digit, lv_coord_t x, lv_coord_t y);

	void hitDrum(uint8_t digit);
	void startEnvelope(const Drum* drum);
	void stepEnvelope();
	void stopEnvelope();

	void startPadFlash(uint8_t digit);
	void clearPadFlash();

	static void onEnvTimerStatic(lv_timer_t* t);
	static void onFlashTimerStatic(lv_timer_t* t);

	/** Last digit the user fired, so BTN_ENTER can repeat it. -1
	 *  means "nothing yet" (the very first frame after open). */
	int8_t lastDigit = -1;

	void buttonPressed(uint i) override;

	/** Auto-clear duration of the pad-flash highlight. Long enough
	 *  to be visually unmistakable on a 160x128 panel, short enough
	 *  that a fast drumroll (every ~60 ms, the practical limit on
	 *  the piezo) still flashes each hit individually. */
	static constexpr uint32_t FlashMs = 120;
};

#endif // MAKERPHONE_PHONEDRUMKITSCREEN_H
