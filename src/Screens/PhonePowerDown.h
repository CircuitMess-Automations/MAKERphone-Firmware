#ifndef MAKERPHONE_PHONEPOWERDOWN_H
#define MAKERPHONE_PHONEPOWERDOWN_H

#include <Arduino.h>
#include "../Interface/LVScreen.h"

/**
 * PhonePowerDown (S57 / S146 / S149)
 *
 * The "powering off" overlay that ships the MAKERphone's signature CRT
 * shrink animation + descending piezo tone. Modeled on the way an old
 * cathode-ray TV collapses to a thin horizontal line and then a single
 * dim afterglow pixel when you yank the mains. Code-only - no SPIFFS
 * assets, no canvas blits, just a couple of lv_obj rectangles whose
 * geometry we walk every animation tick.
 *
 * S146 layers a "custom power-off message" preamble on top of the
 * S57 baseline. When Settings.powerOffMessage is non-empty (set via
 * PhonePowerOffMessageScreen, reachable from the SYSTEM section of
 * PhoneSettingsScreen) the screen prepends a ~700 ms hold-frame in
 * which the phosphor plate stays at full brightness while the
 * persisted message is centred on it in deep-purple pixelbasic16
 * -- the classic Sony-Ericsson "Bye!" flourish. Once the preamble
 * elapses the existing CRT shrink fires unchanged. An empty
 * powerOffMessage slot (factory default) skips the preamble
 * entirely so the original S57 timeline is preserved exactly.
 *
 *   t=0.00 s : full screen filled with a warm-cream "phosphor" plate
 *   t=0.50 s : plate has collapsed to a 2 px tall band across the centre
 *   t=0.75 s : band has collapsed to a 4 px x 4 px centre dot
 *   t=1.30 s : centre dot fades to black; piezo silenced
 *
 * Audio (S149):
 *   - A descending G-major arpeggio (G6 - D6 - B5 - G5) plays once
 *     through the global PhoneRingtoneEngine the moment the CRT
 *     shrink begins. The first three notes strike in 110 ms each
 *     and the final G5 is held for 320 ms, mirroring the rising
 *     S148 boot chime in reverse so power-on and power-off bracket
 *     the session as a matched pair. Total melody length is ~740 ms,
 *     comfortably inside the 1.3 s shrink so the dot fade is heard
 *     in silence -- the closing breath after the chime resolves.
 *   - Replaces the S57 placeholder exponential frequency sweep,
 *     which read as a continuous siren rather than a deliberate
 *     "goodbye" gesture. The arpeggio is a real four-note phrase.
 *   - Settings.sound is honoured: PhoneRingtoneEngine emitTone()
 *     skips the piezo when the user has globally muted, so the
 *     animation plays visually with no audio under mute. We also
 *     short-circuit before kicking the engine to avoid stealing
 *     the playhead from any concurrent buzzer-service beep.
 *   - The S146 message preamble (when active) holds the chime
 *     until the preamble window elapses; the user reads the
 *     goodbye in silence, then the arpeggio fires at the
 *     boundary into the CRT shrink.
 *
 *   +----------------------------------------+
 *   |                                        |  <- pure black canvas
 *   |                                        |
 *   |          [warm-cream phosphor]         |  <- shrinks to centre
 *   |                                        |
 *   |                                        |
 *   +----------------------------------------+
 *
 * Implementation notes:
 *  - Animation is driven by a single lv_timer at AnimTickMs (30 ms).
 *    Every tick advances `elapsedMs` and recomputes the phosphor plate's
 *    width / height + the centre dot's opacity. Total run length is
 *    DefaultDurationMs (~1300 ms) so the animation reads as deliberate
 *    rather than abrupt.
 *  - The plate uses a vertical 2-stop gradient (warm cream -> cyan)
 *    that re-renders cleanly even at 2 px tall - the eye still reads
 *    "still-bright phosphor band" when the plate is mid-collapse. A
 *    soft cyan outline gives it a CRT scanline halo without canvas.
 *  - The afterglow dot is a separate small lv_obj kept hidden until
 *    Phase 2 (after the horizontal collapse). It fades from full warm
 *    cream to black via lv_obj_set_style_bg_opa, so the final frames
 *    feel like phosphor cooling down rather than a hard cut.
 *  - Three phases tracked by `progress` (0.0 -> 1.0 across the whole
 *    run). The breakpoints (PhaseVerticalEnd, PhaseHorizontalEnd) are
 *    floating-point so a host that overrides durationMs (tests) still
 *    gets the same proportions.
 *  - At completion the screen invokes the host-supplied DismissHandler
 *    exactly once. With no callback wired the screen pop()s itself, so
 *    a future PhoneSettingsScreen "Power off" row can push us, the
 *    animation runs, and the user lands back on the previous screen
 *    by default. A host that wants the actual hardware shutdown
 *    (Sleep.turnOff()) wires the callback - the screen never touches
 *    the power rail itself, so you can demo the animation safely from
 *    any context.
 *  - Hardware key input is intentionally NOT wired here: a power-down
 *    animation that you can interrupt by leaning on a button feels
 *    flimsy. The whole run is ~1.3 s; we eat the keys via a no-op
 *    rather than skipping ahead.
 *  - Guarded against double-fire on completion: the timer + a possible
 *    future "skip" path collapse into a single dispatch via the
 *    `firedAlready` flag, mirroring the pattern PhoneCallEnded /
 *    PhoneBootSplash already use for their auto-dismiss.
 */
class PhonePowerDown : public LVScreen {
public:
	using DismissHandler = void (*)();

	/** Total animation run length for the post-preamble CRT shrink,
	 *  in milliseconds. The S146 message preamble (when active) is
	 *  ADDED on top of this duration so that the CRT shrink keeps
	 *  the same on-screen timing every host has been measuring
	 *  against since S57. */
	static constexpr uint32_t DefaultDurationMs = 1300;

	/** Animation tick cadence, in milliseconds (~33 Hz). */
	static constexpr uint32_t AnimTickMs = 30;

	/** S146 - default message-preamble hold time when
	 *  Settings.powerOffMessage is non-empty. Long enough for the
	 *  user to read a classic 5-10 character feature-phone goodbye
	 *  ("Bye!", "See ya!") at glance speed, short enough that the
	 *  whole power-off ceremony still wraps in ~2 s end-to-end. The
	 *  preamble ticks through `applyTone(0.0f)` so the descending
	 *  piezo sweep starts at the top of its envelope and stays
	 *  audibly steady on the highest note for the duration of the
	 *  message hold -- "the screen is bright, the device is humming,
	 *  goodbye". */
	static constexpr uint32_t DefaultMessagePreambleMs = 700;

	/**
	 * Build the power-down overlay. The DismissHandler fires exactly
	 * once after the animation completes; pass nullptr (the default)
	 * to fall through to a plain pop() back to the screen that pushed
	 * us. `durationMs` lets tests run a faster animation; production
	 * callers should leave it at the default.
	 */
	explicit PhonePowerDown(DismissHandler onComplete = nullptr,
							uint32_t       durationMs = DefaultDurationMs);

	~PhonePowerDown() override;

	void onStart() override;
	void onStop() override;

	/** Replace the on-completion handler. Has no effect once fired. */
	void setOnComplete(DismissHandler cb) { dismissCb = cb; }

	/**
	 * S146 - replace the message painted over the phosphor plate
	 * during the preamble phase. By default the screen reads the
	 * value from Settings.powerOffMessage on construction; hosts
	 * (and tests) can override it after the fact. Passing an
	 * empty / null string drops the preamble back to zero so the
	 * shrink fires immediately on onStart() -- the original S57
	 * behaviour. Has no effect once the timer has fired through
	 * the preamble window.
	 */
	void setMessage(const char* text);

	/** S146 - read the active message buffer. Useful for tests
	 *  and future hosts. */
	const char* getMessage() const { return message; }

	/** S146 - duration of the message preamble in milliseconds. Zero
	 *  when no message is active (factory default or post-CLEAR). */
	uint32_t getMessagePreambleMs() const { return messagePreambleMs; }

private:
	DismissHandler dismissCb;
	uint32_t       durationMs;
	uint32_t       elapsedMs;
	bool           firedAlready;

	// S149 - guards against re-firing the descending arpeggio. Set
	// once when applyTone() crosses out of the S146 preamble window
	// (or at t=0 when no message is set), so subsequent ticks let
	// PhoneRingtoneEngine drive the melody to completion without
	// our timer interrupting it. Reset in onStart() so a re-used
	// screen instance plays the chime each run.
	bool           meloFired;

	lv_obj_t*   plate;       // shrinking warm-cream phosphor rectangle
	lv_obj_t*   dot;         // centre afterglow dot, fades at the end
	lv_timer_t* tickTimer;

	// S146 - custom message preamble. The label is built unconditionally
	// (LVGL teardown stays the same in both branches) but only made
	// visible while `messagePreambleMs > 0` and `elapsedMs` is inside
	// the preamble window. The buffer caps at 23 chars + nul to match
	// the Settings.powerOffMessage slot. Pixelbasic16 paints the
	// message in deep purple over the warm-cream plate so it reads as
	// a hard-stamped CRT phosphor caption rather than a translucent
	// overlay.
	static constexpr uint16_t MessageMaxLen = 23;
	char       message[MessageMaxLen + 1];
	uint32_t   messagePreambleMs;
	lv_obj_t*  messageLabel;

	// Phase boundaries as fractions of total progress. Phase 0 collapses
	// the plate vertically, phase 1 horizontally, phase 2 fades the dot.
	static constexpr float PhaseVerticalEnd   = 0.50f;
	static constexpr float PhaseHorizontalEnd = 0.62f;

	void buildPlate();
	void buildDot();
	// S146 - build the centred message label. Always called from
	// the ctor so a host that flips the message after construction
	// (via setMessage) does not have to re-create the label.
	void buildMessageLabel();
	// S146 - apply the preamble visibility for the current
	// elapsedMs (called every tick before the existing applyFrame).
	// Returns true when the preamble is active and the tick should
	// SKIP the existing CRT-shrink frame so the plate stays at full
	// size while the message reads.
	bool applyPreamble();

	void startTicker();
	void stopTicker();

	void applyFrame(float progress);
	void applyTone(float progress);

	void fireComplete();

	static void onTickStatic(lv_timer_t* timer);
};

#endif // MAKERPHONE_PHONEPOWERDOWN_H
