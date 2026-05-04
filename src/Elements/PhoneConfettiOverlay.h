#ifndef MAKERPHONE_PHONECONFETTIOVERLAY_H
#define MAKERPHONE_PHONECONFETTIOVERLAY_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneConfettiOverlay - S152
 *
 * Reusable retro feature-phone "celebration" overlay. The widget
 * renders a fixed pool of 14 small (3x3) coloured pieces that fall
 * from above the 160x128 display, each on its own infinite y-axis
 * animation with a per-piece duration / start delay so the field
 * reads as natural confetti rather than a synchronised curtain.
 *
 *      .  *  .       .
 *         .         *      *      .       <- fast pieces
 *      *     *  .       *       .
 *           *    .         .  *
 *      *  .       *  .         *  .       <- slower pieces, deeper hue
 *
 * Implementation notes:
 *  - 100 % code-only (no SPIFFS asset cost) and reuses the shared
 *    MAKERphone palette (MP_ACCENT, MP_HIGHLIGHT, MP_TEXT,
 *    MP_LABEL_DIM) so the celebration matches the rest of the
 *    PhoneSynthwaveBg / PhoneStatusBar / PhoneSoftKeyBar visual
 *    family. Each piece is a 3x3 lv_obj with a solid fill.
 *  - The overlay is a 160x128 transparent container that sits on
 *    top of every other widget on the host screen. It anchors with
 *    LV_OBJ_FLAG_IGNORE_LAYOUT so a flex / grid host layout is
 *    untouched. The container also disables hit-testing so the
 *    confetti never steals focus from the host screen's input
 *    listeners.
 *  - Animations are owned by the piece objects themselves, so when
 *    the overlay's parent is destroyed LVGL frees them recursively.
 *    stop() cancels every in-flight animation so a second start()
 *    resets cleanly even mid-fall.
 *  - The widget is constructed *inactive*: every piece is built but
 *    no animation is started until the first start() call. That
 *    way a screen that wants the overlay always available (so the
 *    user can re-trigger from a long-press, say) only pays the
 *    builder cost once.
 *  - start() is idempotent - calling it while already active is
 *    treated as a "restart from frame 0" so a screen that's
 *    re-entered (e.g. the user tabs back to home on the same day)
 *    gets a fresh confetti volley without a half-finished one
 *    lingering on screen.
 *  - Pieces use four shared palette tints stamped down the pool in
 *    sequence so every visible chunk of the pool reads as varied.
 *    The same deterministic seeding strategy PhonePixelAvatar uses
 *    keeps the animation cheap and free of random() calls.
 *
 * Caller pattern:
 *
 *   if(PhoneBirthdayReminders::firstBirthdayToday().hasMatch){
 *       confettiOverlay = new PhoneConfettiOverlay(obj);
 *       confettiOverlay->start();
 *   }
 *
 * Cleanup is automatic - when the host screen is destroyed, the
 * overlay (and its animations) tears down with it.
 */
class PhoneConfettiOverlay : public LVObject {
public:
	/** Number of confetti pieces in the pool. 14 is a sweet spot -
	 *  enough to fill the 160 px width without a synchronised curtain,
	 *  cheap enough that 14 lv_anim instances do not strain the
	 *  Chatter heap. */
	static constexpr uint8_t  PieceCount    = 14;

	/** 160x128 - covers the full screen so pieces can be placed at
	 *  any horizontal slot. */
	static constexpr lv_coord_t OverlayWidth  = 160;
	static constexpr lv_coord_t OverlayHeight = 128;

	/** 3x3 px coloured square - visible against the synthwave
	 *  wallpaper without competing with the clock face glyphs. */
	static constexpr lv_coord_t PieceSize     = 3;

	/** Slowest fall duration (ms) - assigned to the first piece in
	 *  the pool. Each subsequent piece shaves a constant amount off
	 *  this so the slowest and fastest pieces are visually
	 *  distinguishable. Range is ~[1300..2700] ms. */
	static constexpr uint32_t  FallSlowMs     = 2700;
	static constexpr uint32_t  FallFastMs     = 1300;

	/** Maximum stagger delay (ms) on the first cycle so pieces
	 *  enter the screen at different times rather than as a
	 *  synchronised curtain. Per-piece delay = (idx * Stride) % Max. */
	static constexpr uint32_t  StaggerMaxMs   = 2000;

	PhoneConfettiOverlay(lv_obj_t* parent);
	virtual ~PhoneConfettiOverlay();

	/**
	 * Begin the falling animation. Idempotent - a second call
	 * cancels any in-flight animation and restarts every piece
	 * from frame 0. The overlay is hidden when constructed and
	 * becomes visible on the first start() call.
	 */
	void start();

	/**
	 * Cancel every in-flight animation and hide the overlay.
	 * Pieces are left where they are in the LVGL tree so a future
	 * start() can re-launch without a rebuild.
	 */
	void stop();

	/** True between start() and the next stop() (or destruction). */
	bool isActive() const { return active; }

private:
	lv_obj_t* pieces[PieceCount];
	bool      active = false;

	void buildPieces();
	void launchPiece(uint8_t idx);

	// Animation exec callback - drives the y position of a piece.
	// ABI-compatible with lv_anim_exec_xcb_t.
	static void fallExec(void* var, int32_t v);
};

#endif // MAKERPHONE_PHONECONFETTIOVERLAY_H
