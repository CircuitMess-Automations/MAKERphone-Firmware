#ifndef MAKERPHONE_PHONEENVELOPEFLY_H
#define MAKERPHONE_PHONEENVELOPEFLY_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneEnvelopeFly - S156
 *
 * Reusable retro feature-phone "SMS sent" flourish for MAKERphone 2.0.
 * The widget renders a tiny code-only envelope sprite that flies from
 * the bottom-left of the 160x128 display up and across to the
 * top-right corner, then dissolves with a brief sparkle. Whenever
 * `ConvoScreen::sendMessage()` posts a new outbound message, it
 * fires `start()` and the animation plays once. This is the
 * MAKERphone 2.0 equivalent of the Sony-Ericsson "letter-being-
 * mailed" beat that every retro phone fan remembers.
 *
 *      ............................     ___
 *      .             .--.        ..    /\__\.       . sparkle
 *      .            /__|         ..   .   ___       .
 *      .           [____]        ..    .  \  \      .
 *      .                         ..              <- envelope arcs
 *      .                         ..                  bottom-left ->
 *      ............................                  top-right
 *
 * Implementation notes:
 *  - 100 % code-only (no SPIFFS asset cost) and reuses the shared
 *    MAKERphone palette (MP_TEXT cream body, MP_ACCENT orange flap
 *    seam, MP_DIM dim purple shadow, MP_HIGHLIGHT cyan sparkle).
 *  - The envelope sprite is six small lv_obj rectangles parented to
 *    one carrier `lv_obj` so the whole thing translates as a single
 *    unit. The carrier is animated, not the individual rectangles -
 *    one animation drives x, one drives y, one drives the dissolve
 *    opacity, one drives the sparkle radial expansion. lv_anim is
 *    cheaper than four lv_timers and the LVGL engine handles
 *    cleanup if the parent dies mid-flight.
 *  - The arc is two cubic ease segments stitched: ease-out on the
 *    horizontal sweep so the envelope leaves quickly, ease-in on
 *    the vertical climb so it appears to "lift off" before
 *    accelerating up. Visually the result is a parabolic arc
 *    without a costly per-frame trig pass.
 *  - The whole flight lasts FlyMs (~700 ms) plus a SparkleMs
 *    (~250 ms) puff at the end - short enough that the user gets
 *    immediate feedback after BTN_ENTER and long enough to read
 *    as a deliberate "sent" beat. The widget hides itself when the
 *    sparkle fades, so a screen that wants the overlay always
 *    available only pays the builder cost once.
 *  - The overlay is a 160x128 transparent container that sits on
 *    top of every other widget on the host screen and disables
 *    hit-testing so input still flows to the host's listeners. It
 *    anchors with LV_OBJ_FLAG_IGNORE_LAYOUT so a flex / grid host
 *    layout is untouched.
 *  - start() is idempotent - calling it while a flight is in flight
 *    cancels the in-flight animation and relaunches from frame 0.
 *  - The widget keeps no persistent dynamic state between flights
 *    beyond an `active` flag, mirroring the PhoneConfettiOverlay
 *    contract callers already know.
 *
 * Caller pattern:
 *
 *   // ConvoScreen ctor
 *   envelopeFly = new PhoneEnvelopeFly(obj);
 *
 *   // ConvoScreen::sendMessage(), after Messages.sendText() succeeds
 *   envelopeFly->start();
 *
 * Cleanup is automatic - when the host screen is destroyed, the
 * overlay (and its animations) tears down with it.
 */
class PhoneEnvelopeFly : public LVObject {
public:
	/** 160x128 - covers the full screen so the envelope can be placed
	 *  anywhere along the arc without a parent re-anchor. */
	static constexpr lv_coord_t OverlayWidth   = 160;
	static constexpr lv_coord_t OverlayHeight  = 128;

	/** Envelope sprite footprint - 18x12 reads as a clear retro
	 *  envelope at 160x128 without obscuring the message that just
	 *  flew. The flap triangle is two thin rectangles inside this
	 *  bounding box. */
	static constexpr lv_coord_t SpriteWidth    = 18;
	static constexpr lv_coord_t SpriteHeight   = 12;

	/** Flight duration in ms. ~700 ms is a beat that reads as
	 *  deliberate "sent" feedback on the 160x128 display - shorter
	 *  feels twitchy, longer interrupts the user's typing rhythm. */
	static constexpr uint32_t   FlyMs          = 700;

	/** Sparkle dissolve duration in ms after the envelope reaches
	 *  the top-right corner. Short and sharp so the message list
	 *  re-takes focus quickly. */
	static constexpr uint32_t   SparkleMs      = 250;

	/** Sparkle ring max radius in px - reads as a small "delivered"
	 *  pop without competing with the status bar glyphs. */
	static constexpr lv_coord_t SparkleMaxR    = 8;

	PhoneEnvelopeFly(lv_obj_t* parent);
	virtual ~PhoneEnvelopeFly();

	/**
	 * Begin the envelope flight + sparkle puff. Idempotent - a
	 * second call cancels every in-flight animation and restarts
	 * from frame 0. The overlay is hidden when constructed and
	 * becomes visible on the first start() call.
	 */
	void start();

	/**
	 * Cancel every in-flight animation and hide the overlay
	 * immediately. Pieces are left in the LVGL tree so a future
	 * start() can re-launch without a rebuild.
	 */
	void stop();

	/** True between start() and the natural end-of-sparkle (or a
	 *  caller-driven stop()). */
	bool isActive() const { return active; }

private:
	// Carrier holds the six envelope rectangles; we animate the
	// carrier's x/y so the whole sprite translates as one piece.
	lv_obj_t* carrier  = nullptr;
	lv_obj_t* body     = nullptr;
	lv_obj_t* flapL    = nullptr;
	lv_obj_t* flapR    = nullptr;
	lv_obj_t* seam     = nullptr;
	lv_obj_t* shadow   = nullptr;
	lv_obj_t* sparkle  = nullptr;

	bool active = false;

	void buildSprite();

	// Animation exec callbacks. Each drives one property on the
	// carrier or the sparkle. ABI-compatible with lv_anim_exec_xcb_t.
	static void flyXExec(void* var, int32_t v);
	static void flyYExec(void* var, int32_t v);
	static void flyOpaExec(void* var, int32_t v);
	static void sparkleSizeExec(void* var, int32_t v);
	static void sparkleOpaExec(void* var, int32_t v);

	// One-shot ready callback that hides the overlay after the
	// sparkle finishes, so the widget self-cleans.
	static void onAllDone(lv_anim_t* a);
};

#endif // MAKERPHONE_PHONEENVELOPEFLY_H
