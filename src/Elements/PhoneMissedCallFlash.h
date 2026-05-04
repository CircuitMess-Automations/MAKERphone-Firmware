#ifndef MAKERPHONE_PHONEMISSEDCALLFLASH_H
#define MAKERPHONE_PHONEMISSEDCALLFLASH_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneMissedCallFlash - S158
 *
 * Reusable retro feature-phone "you missed a call" beat for
 * MAKERphone 2.0. Renders a 160x128 full-screen overlay that pulses
 * the entire display three times in quick succession the moment the
 * lock screen redraws after a wake event. The pulse colour is the
 * shared MAKERphone warm-cream MP_TEXT, which on the dark synthwave
 * wallpaper reads as the classic Sony-Ericsson / early-Nokia
 * "inverted-color flash" - a sudden bright frame that snaps your
 * eye to the screen, then settles back to the normal lock face with
 * the missed-call line already populated by the existing
 * `PhoneNotificationPreview` strip (S49).
 *
 * Why a dedicated overlay and not a per-screen tint?
 *  - The flash is a system-level cue that has to fire over the
 *    finished lock screen (status bar, clock, preview, soft-keys,
 *    synthwave background, charging chip, charge bars) regardless
 *    of which sub-element happens to own the largest area on the
 *    frame the user is looking at. A full-screen overlay is the
 *    only widget that guarantees coverage without per-element
 *    plumbing.
 *  - The lock screen already owns the wake-redraw lifecycle
 *    (`onStarting()` is the canonical "next frame after wake" hook),
 *    so a single overlay built once and re-triggered on demand is
 *    cheaper than a transient build-and-tear-down per arrival.
 *
 * Implementation notes:
 *  - 100 % code-only (no SPIFFS asset cost) and reuses the shared
 *    MAKERphone palette (MP_TEXT cream as the flash colour).
 *  - One opaque rectangle covering the whole display, opacity
 *    animated through three pulses (0 -> 200 -> 0 -> 200 -> 0
 *    -> 200 -> 0) over `FlashMs` (~750 ms total). The eye reads
 *    each pulse as a distinct "blip" against the dark wallpaper.
 *  - The overlay disables hit-testing (`LV_OBJ_FLAG_CLICKABLE`
 *    cleared on construction) so input still flows to the host
 *    screen's listeners during the flash. The unlock slide and
 *    soft-key bar remain interactive even mid-pulse.
 *  - `start()` is idempotent - calling it while a pulse train is
 *    in flight cancels the in-flight animation and relaunches
 *    from frame 0. Useful for the rare case where a second
 *    missed call lands while the first flash is still playing.
 *  - Hidden by default. The widget surfaces itself on `start()`
 *    and re-hides itself when the final pulse completes via the
 *    animation's `ready_cb`. The host never has to manage
 *    visibility manually.
 *  - Anchored with `LV_OBJ_FLAG_IGNORE_LAYOUT` so a flex / grid
 *    parent layout is untouched - same contract as
 *    `PhoneEnvelopeFly` and `PhoneConfettiOverlay`.
 *
 * Caller pattern:
 *
 *   // LockScreen ctor
 *   missedFlash = new PhoneMissedCallFlash(obj);
 *
 *   // LockScreen::onStarting(), after the rest of the layout pass
 *   if(MissedCallLog::instance().consumePendingFlash()){
 *       missedFlash->start();
 *   }
 *
 * Cleanup is automatic - when the host screen is destroyed, the
 * overlay (and its animation) tears down with it.
 */
class PhoneMissedCallFlash : public LVObject {
public:
	/** 160x128 - covers the full screen so every pixel under the
	 *  overlay (status bar, clock, soft-key bar, synthwave
	 *  wallpaper, charging chip, charge bars) participates in the
	 *  pulse without per-widget plumbing. */
	static constexpr lv_coord_t OverlayWidth   = 160;
	static constexpr lv_coord_t OverlayHeight  = 128;

	/** Total flash duration in ms. Three ~250 ms pulses keep the
	 *  beat short enough to read as a distinct "alert" cue without
	 *  feeling like a stutter. */
	static constexpr uint32_t   FlashMs        = 750;

	/** Number of bright pulses inside `FlashMs`. Three matches the
	 *  Sony-Ericsson "blink-blink-blink" muscle memory; tweakable
	 *  here without re-deriving the per-frame opacity table. */
	static constexpr uint8_t    PulseCount     = 3;

	/** Peak opacity of each pulse. 200/255 (~78%) is loud enough to
	 *  visibly invert the dark synthwave wallpaper while still
	 *  letting the underlying clock + status bar bleed through, so
	 *  the screen reads as "flashing" rather than "blanked-out". */
	static constexpr lv_opa_t   PeakOpa        = 200;

	PhoneMissedCallFlash(lv_obj_t* parent);
	virtual ~PhoneMissedCallFlash();

	/**
	 * Begin the three-pulse flash. Idempotent - a second call
	 * cancels every in-flight animation and restarts from frame 0.
	 * The overlay is hidden when constructed and becomes visible
	 * on the first `start()` call.
	 */
	void start();

	/**
	 * Cancel every in-flight animation and hide the overlay
	 * immediately. The lv_obj is left in the LVGL tree so a
	 * future `start()` can re-launch without a rebuild.
	 */
	void stop();

	/** True between start() and the natural end-of-flash (or a
	 *  caller-driven stop()). */
	bool isActive() const { return active; }

private:
	// Single full-screen rectangle; opacity is the only animated
	// channel. Held as a member so stop() / start() can target the
	// same lv_obj across calls without scanning the child list.
	lv_obj_t* sheet = nullptr;

	bool active = false;

	// Animation exec callback - drives the sheet's bg opacity.
	// ABI-compatible with lv_anim_exec_xcb_t.
	static void opaExec(void* var, int32_t v);

	// One-shot ready callback that hides the overlay after the
	// final pulse, so the widget self-cleans.
	static void onAllDone(lv_anim_t* a);
};

#endif // MAKERPHONE_PHONEMISSEDCALLFLASH_H
