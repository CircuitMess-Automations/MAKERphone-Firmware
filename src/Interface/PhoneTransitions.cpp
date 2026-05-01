#include "PhoneTransitions.h"

/**
 * PhoneTransitions (S66) - implementation.
 *
 * The mapping lives in this single file so future tweaks (motion
 * curve, slightly different duration per gesture, or even a "reduced
 * motion" toggle in Settings) only touch this translator. The
 * underlying `LVScreen::push` / `LVScreen::pop` signatures are not
 * changed - the helper is a strict superset over the existing API.
 */

lv_scr_load_anim_t PhoneTransitions::pushAnim(PhoneTransition t){
	switch(t){
		case PhoneTransition::Drill:
			// New screen enters from the right edge - the dest slides
			// LEFT to take the visible viewport, the previous screen
			// is shoved off to the left. This is the "go one level
			// deeper" gesture (S21 home->menu, every menu->app push).
			return LV_SCR_LOAD_ANIM_MOVE_LEFT;

		case PhoneTransition::Modal:
			// New screen rises from the bottom edge. Matches the
			// legacy default for `LVScreen::push` (kept identical so
			// the visual feel of every modal-style screen that has
			// not been migrated yet does not change).
			return LV_SCR_LOAD_ANIM_MOVE_BOTTOM;

		case PhoneTransition::Fade:
			// Cross-fade in place. Used when there is no spatial
			// relationship between the two screens.
			return LV_SCR_LOAD_ANIM_FADE_ON;

		case PhoneTransition::Instant:
		default:
			return LV_SCR_LOAD_ANIM_NONE;
	}
}

lv_scr_load_anim_t PhoneTransitions::popAnim(PhoneTransition t){
	switch(t){
		case PhoneTransition::Drill:
			// Mirror of the push. Dest slides RIGHT off to the right
			// edge, the parent slides back into place. Together with
			// the Drill push this gives the signature SE-style flick.
			return LV_SCR_LOAD_ANIM_MOVE_RIGHT;

		case PhoneTransition::Modal:
			// Mirror of the modal push - dest drops back down the way
			// it came in. Matches the legacy default for `LVScreen::pop`.
			return LV_SCR_LOAD_ANIM_MOVE_TOP;

		case PhoneTransition::Fade:
			// FADE_ON cross-fades both ways; reuse it on the pop so the
			// inverse looks like a true cross-dissolve rather than a
			// hard cut.
			return LV_SCR_LOAD_ANIM_FADE_ON;

		case PhoneTransition::Instant:
		default:
			return LV_SCR_LOAD_ANIM_NONE;
	}
}

void PhoneTransitions::push(LVScreen* from, LVScreen* to, PhoneTransition t){
	// Defensive against a null caller - matches the call-site contract
	// other helpers in this codebase enforce. The underlying
	// `LVScreen::push` would null-deref so we early-out here instead.
	if(from == nullptr || to == nullptr) return;
	from->push(to, pushAnim(t));
}

void PhoneTransitions::pop(LVScreen* from, PhoneTransition t){
	if(from == nullptr) return;
	from->pop(popAnim(t));
}
