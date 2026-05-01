#ifndef MAKERPHONE_PHONETRANSITIONS_H
#define MAKERPHONE_PHONETRANSITIONS_H

#include <lvgl.h>
#include "LVScreen.h"

/**
 * PhoneTransitions (S66)
 *
 * Centralised page-transition helper for MAKERphone 2.0. Every screen
 * in the firmware navigates through `LVScreen::push` / `LVScreen::pop`,
 * which take a raw `lv_scr_load_anim_t`. Until S66 each call site
 * either picked its own LV_SCR_LOAD_ANIM_* constant ad-hoc (S21
 * home<->menu, S65 games launcher, etc.) or quietly inherited the
 * default vertical slide. The result was a UI that felt slightly
 * inconsistent: a "drill into a sub-page" sometimes slid horizontally
 * and sometimes vertically, depending on which screen owned the push.
 *
 * This helper introduces a small, semantic vocabulary - Drill / Modal
 * / Fade / Instant - that maps to the *correct pair* of LVGL
 * animations for the push and the pop direction. A caller writes
 *
 *   PhoneTransitions::push(this, new PhoneDialerScreen(),
 *                          PhoneTransition::Drill);
 *
 * and the destination slides in from the right; when that destination
 * later calls
 *
 *   PhoneTransitions::pop(this, PhoneTransition::Drill);
 *
 * the matching slide-back-to-the-right unwinds it - so the user sees
 * a consistent "drill in, drill out" gesture across every screen that
 * adopts the helper. Future polish (motion-curve, duration, etc.) can
 * be tuned in this single file without chasing every call site.
 *
 * The helper is intentionally non-stateful: it is just a thin
 * translator from PhoneTransition -> lv_scr_load_anim_t plus two
 * forwarding wrappers around `LVScreen::push` / `LVScreen::pop`. Every
 * existing call site that passes a raw `LV_SCR_LOAD_ANIM_*` keeps
 * working unchanged (the underlying LVScreen API is untouched), so
 * adopting this helper is opt-in and incremental.
 *
 * Semantic vocabulary (S66):
 *
 *   Drill   - "go one level deeper into the menu tree"
 *             push: enter from right (MOVE_LEFT), pop: exit to right
 *             (MOVE_RIGHT). The Sony-Ericsson signature gesture and
 *             the one we already use for home->menu (S21) and
 *             menu->app (S20 sites).
 *
 *   Modal   - "open something on top of the current screen"
 *             push: rise from bottom (MOVE_BOTTOM), pop: drop back
 *             down (MOVE_TOP). The legacy default for `LVScreen::push`
 *             before S66; used for incoming-call overlay (S24), the
 *             T9 input surface, the lock screen drop-in, etc.
 *
 *   Fade    - "swap the surface in place"
 *             push and pop both LV_SCR_LOAD_ANIM_FADE_ON. Reserved for
 *             cases where there is no spatial relationship between the
 *             two screens (e.g. boot-splash -> homescreen, power-down
 *             -> blank).
 *
 *   Instant - "no animation, swap immediately"
 *             passes LV_SCR_LOAD_ANIM_NONE through for both push and
 *             pop. Matches the legacy `start(false)` path. Useful when
 *             the destination is itself going to play a heavy entrance
 *             animation (boot splash, CRT-shrink power-down).
 *
 * The enum is `enum class` so callers must qualify the value
 * (PhoneTransition::Drill) - that prevents accidental collisions with
 * the LV_SCR_LOAD_ANIM_* macros which live in the global namespace.
 */
enum class PhoneTransition : uint8_t {
	Drill   = 0,
	Modal   = 1,
	Fade    = 2,
	Instant = 3,
};

class PhoneTransitions {
public:
	/** Map a PhoneTransition to the lv_scr_load_anim_t to use on push. */
	static lv_scr_load_anim_t pushAnim(PhoneTransition t);

	/** Map a PhoneTransition to the lv_scr_load_anim_t to use on pop. */
	static lv_scr_load_anim_t popAnim(PhoneTransition t);

	/**
	 * Push `to` on top of `from`, animating with the gesture's push
	 * direction. Equivalent to
	 *   from->push(to, PhoneTransitions::pushAnim(t));
	 * but reads as a single intent at the call site. `from` and `to`
	 * must both be non-null - the helper does not allocate.
	 */
	static void push(LVScreen* from, LVScreen* to, PhoneTransition t);

	/**
	 * Pop `from` back to its parent, animating with the gesture's pop
	 * direction. Equivalent to
	 *   from->pop(PhoneTransitions::popAnim(t));
	 * but uses the same vocabulary as the matching push so call sites
	 * can keep "drill in" and "drill out" symmetric. No-op if `from`
	 * has no parent (mirroring `LVScreen::pop`'s own guard).
	 */
	static void pop(LVScreen* from, PhoneTransition t);
};

#endif // MAKERPHONE_PHONETRANSITIONS_H
