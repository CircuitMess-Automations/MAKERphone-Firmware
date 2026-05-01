#ifndef MAKERPHONE_PHONEPOWERDOWN_H
#define MAKERPHONE_PHONEPOWERDOWN_H

#include <Arduino.h>
#include "../Interface/LVScreen.h"

/**
 * PhonePowerDown (S57)
 *
 * The "powering off" overlay that ships the MAKERphone's signature CRT
 * shrink animation + descending piezo tone. Modeled on the way an old
 * cathode-ray TV collapses to a thin horizontal line and then a single
 * dim afterglow pixel when you yank the mains. Code-only - no SPIFFS
 * assets, no canvas blits, just a couple of lv_obj rectangles whose
 * geometry we walk every animation tick.
 *
 *   t=0.00 s : full screen filled with a warm-cream "phosphor" plate
 *   t=0.50 s : plate has collapsed to a 2 px tall band across the centre
 *   t=0.75 s : band has collapsed to a 4 px x 4 px centre dot
 *   t=1.30 s : centre dot fades to black; piezo silenced
 *
 * Audio:
 *   - The piezo descends exponentially from ~1500 Hz to ~180 Hz over
 *     the full ~1.3 s, with a final 30 ms low click on the very last
 *     frame for a tactile "and then it's off" cue. The tone updates
 *     every animation tick (~30 ms) by recomputing the frequency from
 *     the current normalized progress.
 *   - Piezo.setMute() state is honoured: if the user has globally
 *     muted sound (PhoneSoundScreen / Settings.sound == false) the
 *     animation still plays visually but the piezo is left silent.
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

	/** Total animation run length, in milliseconds. */
	static constexpr uint32_t DefaultDurationMs = 1300;

	/** Animation tick cadence, in milliseconds (~33 Hz). */
	static constexpr uint32_t AnimTickMs = 30;

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

private:
	DismissHandler dismissCb;
	uint32_t       durationMs;
	uint32_t       elapsedMs;
	bool           firedAlready;

	lv_obj_t*   plate;       // shrinking warm-cream phosphor rectangle
	lv_obj_t*   dot;         // centre afterglow dot, fades at the end
	lv_timer_t* tickTimer;

	// Phase boundaries as fractions of total progress. Phase 0 collapses
	// the plate vertically, phase 1 horizontally, phase 2 fades the dot.
	static constexpr float PhaseVerticalEnd   = 0.50f;
	static constexpr float PhaseHorizontalEnd = 0.62f;

	void buildPlate();
	void buildDot();

	void startTicker();
	void stopTicker();

	void applyFrame(float progress);
	void applyTone(float progress);

	void fireComplete();

	static void onTickStatic(lv_timer_t* timer);
};

#endif // MAKERPHONE_PHONEPOWERDOWN_H
