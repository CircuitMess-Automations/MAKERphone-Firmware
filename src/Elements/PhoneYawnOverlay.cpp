#include "PhoneYawnOverlay.h"
#include "../MakerphoneTheme.h"
#include <Loop/LoopManager.h>
#include <Input/Input.h>

// Palette: theme-aware so the eyes flip to the active skin (Synthwave
// cream-on-purple by default, Nokia 3310 dark olive on pale olive,
// etc.). The eye-whites draw in MP_TEXT and the pupils in MP_BG_DARK
// so each pair reads as a clean light-on-dark cartoon eye on every
// theme that ships through MakerphoneTheme.
#define MP_TEXT     (MakerphoneTheme::text())
#define MP_BG_DARK  (MakerphoneTheme::bgDark())

PhoneYawnOverlay::PhoneYawnOverlay(lv_obj_t* parent) : LVObject(parent) {
	// Full-screen transparent container so both eyes can be positioned
	// in absolute coordinates without disturbing the host's flex
	// layout. Hit-testing disabled so the host's input listeners keep
	// receiving every key press while the eyes are visible (the
	// overlay should never steal focus from CALL / MENU).
	lv_obj_remove_style_all(obj);
	lv_obj_set_size(obj, 160, 128);
	lv_obj_set_pos(obj, 0, 0);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_outline_width(obj, 0, 0);
	// Boot fully transparent - the very first loop() tick will at most
	// flip nothing. The fade-in only starts after IdleMs of stillness.
	lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);

	// Two eyes side-by-side, centered horizontally on the 160 px display.
	// Total width of the pair = EyeWidth + EyeGap + EyeWidth.
	const lv_coord_t pairW   = EyeWidth + EyeGap + EyeWidth;
	const lv_coord_t leftX   = (160 - pairW) / 2;
	const lv_coord_t rightX  = leftX + EyeWidth + EyeGap;
	const lv_coord_t y       = EyeYOffset;

	// Eye-white left.
	eyeL = lv_obj_create(obj);
	lv_obj_remove_style_all(eyeL);
	lv_obj_clear_flag(eyeL, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(eyeL, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(eyeL, EyeWidth, EyeHeight);
	lv_obj_set_pos(eyeL, leftX, y);
	lv_obj_set_style_radius(eyeL, EyeHeight / 2, 0);
	lv_obj_set_style_bg_opa(eyeL, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(eyeL, MP_TEXT, 0);
	lv_obj_set_style_border_width(eyeL, 0, 0);
	lv_obj_set_style_pad_all(eyeL, 0, 0);

	// Eye-white right.
	eyeR = lv_obj_create(obj);
	lv_obj_remove_style_all(eyeR);
	lv_obj_clear_flag(eyeR, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(eyeR, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(eyeR, EyeWidth, EyeHeight);
	lv_obj_set_pos(eyeR, rightX, y);
	lv_obj_set_style_radius(eyeR, EyeHeight / 2, 0);
	lv_obj_set_style_bg_opa(eyeR, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(eyeR, MP_TEXT, 0);
	lv_obj_set_style_border_width(eyeR, 0, 0);
	lv_obj_set_style_pad_all(eyeR, 0, 0);

	// Pupils: small dark squares centered inside each eye-white.
	const lv_coord_t pupilDx = (EyeWidth  - PupilSize) / 2;
	const lv_coord_t pupilDy = (EyeHeight - PupilSize) / 2;

	pupilL = lv_obj_create(obj);
	lv_obj_remove_style_all(pupilL);
	lv_obj_clear_flag(pupilL, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(pupilL, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(pupilL, PupilSize, PupilSize);
	lv_obj_set_pos(pupilL, leftX + pupilDx, y + pupilDy);
	lv_obj_set_style_radius(pupilL, 0, 0);
	lv_obj_set_style_bg_opa(pupilL, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(pupilL, MP_BG_DARK, 0);
	lv_obj_set_style_border_width(pupilL, 0, 0);
	lv_obj_set_style_pad_all(pupilL, 0, 0);

	pupilR = lv_obj_create(obj);
	lv_obj_remove_style_all(pupilR);
	lv_obj_clear_flag(pupilR, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(pupilR, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(pupilR, PupilSize, PupilSize);
	lv_obj_set_pos(pupilR, rightX + pupilDx, y + pupilDy);
	lv_obj_set_style_radius(pupilR, 0, 0);
	lv_obj_set_style_bg_opa(pupilR, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(pupilR, MP_BG_DARK, 0);
	lv_obj_set_style_border_width(pupilR, 0, 0);
	lv_obj_set_style_pad_all(pupilR, 0, 0);

	lastActivityMs = millis();
	appliedOpa     = 0;
	shownAtMs      = 0;
	eyesClosed     = false;
	blinkCount     = 0;

	LoopManager::addListener(this);
	Input::getInstance()->addListener(this);
}

PhoneYawnOverlay::~PhoneYawnOverlay() {
	// Order matters: drop the listener registrations BEFORE the LVObject
	// base destructor frees `obj`, so a frame in flight does not see a
	// dangling eye/pupil pointer.
	LoopManager::removeListener(this);
	Input::getInstance()->removeListener(this);
}

void PhoneYawnOverlay::setActive(bool a) {
	active = a;
	if(!a) {
		applyOpa(0);
		setEyesClosed(false);
		shownAtMs      = 0;
		blinkCount     = 0;
		lastActivityMs = millis();
	} else {
		// Re-arming after a gate close - the user has not just touched
		// a key, so don't reset the clock. Let the host call
		// resetActivity() if it wants the idle window to start fresh.
	}
}

void PhoneYawnOverlay::resetActivity() {
	lastActivityMs = millis();
	if(appliedOpa != 0) {
		applyOpa(0);
	}
	if(eyesClosed) {
		setEyesClosed(false);
	}
	shownAtMs  = 0;
	blinkCount = 0;
}

void PhoneYawnOverlay::loop(uint /*micros*/) {
	if(!active) {
		if(appliedOpa != 0) {
			applyOpa(0);
		}
		if(eyesClosed) {
			setEyesClosed(false);
		}
		return;
	}

	const uint32_t now     = millis();
	const uint32_t idleFor = now - lastActivityMs;
	if(idleFor < IdleMs) {
		// Still inside the "user is around" window. Keep the eyes
		// hidden and the shownAt clock cleared so the next idle
		// cycle starts cleanly from a 0-opa baseline.
		if(appliedOpa != 0) {
			applyOpa(0);
		}
		if(eyesClosed) {
			setEyesClosed(false);
		}
		shownAtMs  = 0;
		blinkCount = 0;
		return;
	}

	if(shownAtMs == 0) {
		shownAtMs  = now;
		blinkCount = 0;
	}
	const uint32_t shownFor = now - shownAtMs;

	// 1) Fade-in ramp. While we're inside FadeMs, eyes stay open and
	//    we just drive the opacity up. The blink cycle starts after
	//    the fade-in completes so the first thing the user sees is a
	//    pair of cleanly-rendered eyes, not a half-faded blink.
	if(shownFor < FadeMs) {
		const uint16_t opa = (uint16_t) (((uint32_t) PeakOpa * shownFor) / FadeMs);
		applyOpa((uint8_t) (opa > 255u ? 255u : opa));
		if(eyesClosed) {
			setEyesClosed(false);
		}
		return;
	}

	// 2) Pinned at PeakOpa once fade-in is complete.
	if(appliedOpa != PeakOpa) {
		applyOpa(PeakOpa);
	}

	// 3) Blink cycle. Compute (cycleIdx, phase) from the time elapsed
	//    since the fade-in finished. Inside the cycle, the first
	//    `closeMs` ms have eyes shut; the rest are eyes open. Every
	//    `YawnEvery`-th cycle uses YawnDownMs instead of BlinkDownMs
	//    so the loop reads as a slow "yawn" once in a while rather
	//    than a metronome of identical blinks.
	const uint32_t sinceFade = shownFor - FadeMs;
	const uint32_t cycleIdx  = sinceFade / BlinkPeriodMs;
	const uint32_t phase     = sinceFade % BlinkPeriodMs;
	const bool     isYawn    = (YawnEvery > 0u) && ((cycleIdx % YawnEvery) == (YawnEvery - 1u));
	const uint32_t closeMs   = isYawn ? YawnDownMs : BlinkDownMs;
	const bool     wantClose = (phase < closeMs);

	if(wantClose != eyesClosed) {
		setEyesClosed(wantClose);
	}

	// Track full cycles for parity (currently used only by isYawn but
	// kept exposed for future variants - e.g. an eye-roll on every
	// 10th cycle).
	if(cycleIdx > blinkCount) {
		blinkCount = (uint16_t) cycleIdx;
	}
}

void PhoneYawnOverlay::anyKeyPressed() {
	// Any button counts as activity - exactly the contract every other
	// idle widget on the home screen uses. The idle clock resets, the
	// eyes snap invisible, and the next idle cycle starts from a
	// clean 0-opa baseline so the user never sees a half-faded ghost
	// on the very next press.
	lastActivityMs = millis();
	if(appliedOpa != 0) {
		applyOpa(0);
	}
	if(eyesClosed) {
		setEyesClosed(false);
	}
	shownAtMs  = 0;
	blinkCount = 0;
}

void PhoneYawnOverlay::applyOpa(uint8_t opa) {
	if(opa == appliedOpa) {
		return;
	}
	lv_obj_set_style_opa(obj, opa, 0);
	appliedOpa = opa;
}

void PhoneYawnOverlay::setEyesClosed(bool closed) {
	if(closed == eyesClosed) {
		return;
	}
	eyesClosed = closed;

	// "Closed" state: squash each eye-white to a 1 px line and hide
	// the pupils. We adjust both the height and the y-offset so the
	// line stays vertically centered on the original eye-white.
	const lv_coord_t pairW   = EyeWidth + EyeGap + EyeWidth;
	const lv_coord_t leftX   = (160 - pairW) / 2;
	const lv_coord_t rightX  = leftX + EyeWidth + EyeGap;

	if(closed) {
		const lv_coord_t lineY = EyeYOffset + (EyeHeight / 2);
		if(eyeL) {
			lv_obj_set_size(eyeL, EyeWidth, 1);
			lv_obj_set_pos (eyeL, leftX, lineY);
		}
		if(eyeR) {
			lv_obj_set_size(eyeR, EyeWidth, 1);
			lv_obj_set_pos (eyeR, rightX, lineY);
		}
		if(pupilL) lv_obj_add_flag(pupilL, LV_OBJ_FLAG_HIDDEN);
		if(pupilR) lv_obj_add_flag(pupilR, LV_OBJ_FLAG_HIDDEN);
	} else {
		if(eyeL) {
			lv_obj_set_size(eyeL, EyeWidth, EyeHeight);
			lv_obj_set_pos (eyeL, leftX, EyeYOffset);
		}
		if(eyeR) {
			lv_obj_set_size(eyeR, EyeWidth, EyeHeight);
			lv_obj_set_pos (eyeR, rightX, EyeYOffset);
		}
		if(pupilL) lv_obj_clear_flag(pupilL, LV_OBJ_FLAG_HIDDEN);
		if(pupilR) lv_obj_clear_flag(pupilR, LV_OBJ_FLAG_HIDDEN);
	}
}
