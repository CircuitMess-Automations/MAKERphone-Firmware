#include "PhoneFlashlight.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Chatter.h>
#include <Settings.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneIdleDim.h"

// MAKERphone retro palette — kept identical to every other Phone*
// widget so the flashlight's OFF state slots into the family beside
// PhoneCalculator (S60), PhoneAlarmClock (S124), PhoneTimers (S125),
// PhoneCurrencyConverter (S126), PhoneUnitConverter (S127),
// PhoneWorldClock (S128), PhoneVirtualPet (S129), PhoneMagic8Ball
// (S130), PhoneDiceRoller (S131), PhoneCoinFlip (S132) and
// PhoneFortuneCookie (S133) without a visual seam. Same
// inline-#define convention every other Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple

// Flashlight-specific shades. The white panel is pure white so the
// LCD pumps every sub-pixel; the hint text on top of it is a dim
// grey so it reads without breaking the "this whole screen is a
// light" mental model.
#define MP_FLASH_WHITE     lv_color_make(255, 255, 255)
#define MP_FLASH_HINT      lv_color_make( 80,  80,  80)

// ---------- geometry --------------------------------------------------
//
// 160x128 budget (OFF state):
//   y=0..10    PhoneStatusBar
//   y=14..20   "FLASHLIGHT" caption (pixelbasic7, cyan)
//
//   y=34..86   preview box 64x52 centred at x=48..112 (cream bg,
//              cyan border, pixelbasic7 "TORCH OFF" text)
//
//   y=100..106 hint ("PRESS LIGHT TO TURN ON")
//   y=118..128 PhoneSoftKeyBar
//
// ON state hides the chrome and the wallpaper behind a full-screen
// white panel; the soft-key bar is re-asserted on top so the user
// can find OFF / BACK without losing the lit area. The status bar
// is hidden while ON because its synthwave-tinted graphics would
// bleed colour into the otherwise-pure white.

static constexpr lv_coord_t kCaptionY        = 14;

static constexpr lv_coord_t kPreviewW        = 64;
static constexpr lv_coord_t kPreviewH        = 52;
static constexpr lv_coord_t kPreviewX        = (160 - kPreviewW) / 2;
static constexpr lv_coord_t kPreviewY        = 34;

static constexpr lv_coord_t kHintY           = 100;

// White panel covers everything below the soft-key strip (which
// lives at y=118..128). Reaching the top edge so the lit area is
// the maximum we can squeeze out of the 160x128 panel without
// overlapping the soft-key bar.
static constexpr lv_coord_t kPanelW          = 160;
static constexpr lv_coord_t kPanelH          = 118;
static constexpr lv_coord_t kPanelX          = 0;
static constexpr lv_coord_t kPanelY          = 0;

// ---------- ctor / dtor -----------------------------------------------

PhoneFlashlight::PhoneFlashlight()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  previewBox(nullptr),
		  previewLabel(nullptr),
		  hintLabel(nullptr),
		  whitePanel(nullptr) {

	// Snapshot the brightness the screen opened with. Every transition
	// back to OFF restores this exact value so the flashlight visit
	// cannot accidentally rewrite the user's preferred brightness.
	initialBrightness = Settings.get().screenBrightness;

	// Full-screen container, no scrollbars, no padding — same blank
	// canvas pattern every other Phone* utility screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, 160);
	lv_obj_set_pos(captionLabel, 0, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "FLASHLIGHT");

	buildHud();
	buildWhitePanel();

	// Bottom soft-key bar: left flips between LIGHT / OFF, right is
	// always BACK. The bar is created last so it sits on top of the
	// white panel when ON; we reuse its press-flash feedback so a
	// user mashing 5 sees the same tactile click feel as the rest
	// of the shell.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("LIGHT");
	softKeys->setRight("BACK");

	// Final paint: render the OFF state. brightness is *not* changed
	// here because we already match the user's setting.
	renderOff();
}

PhoneFlashlight::~PhoneFlashlight() {
	stopKeepAliveTimer();
	// Restore the user's brightness on the way out, even if the
	// screen was destroyed while the panel was still lit (e.g. a
	// notification popped over us, or a higher-level screen yanked
	// us off the stack).
	Chatter.setBrightness(initialBrightness);
}

// ---------- build helpers ---------------------------------------------

void PhoneFlashlight::buildHud() {
	// Preview rectangle that mimics the lit-state look in miniature.
	// Cream background + cyan border, with "TORCH OFF" centred in
	// pixelbasic7. It also doubles as a target the user's eye can
	// rest on while looking for the toggle.
	previewBox = lv_obj_create(obj);
	lv_obj_remove_style_all(previewBox);
	lv_obj_set_size(previewBox, kPreviewW, kPreviewH);
	lv_obj_set_pos(previewBox, kPreviewX, kPreviewY);
	lv_obj_set_style_radius(previewBox, 3, 0);
	lv_obj_set_style_bg_opa(previewBox, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(previewBox, MP_TEXT, 0);
	lv_obj_set_style_border_width(previewBox, 1, 0);
	lv_obj_set_style_border_color(previewBox, MP_HIGHLIGHT, 0);
	lv_obj_set_style_pad_all(previewBox, 0, 0);
	lv_obj_clear_flag(previewBox, LV_OBJ_FLAG_SCROLLABLE);

	previewLabel = lv_label_create(previewBox);
	lv_obj_set_style_text_font(previewLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(previewLabel, MP_BG_DARK, 0);
	lv_obj_set_width(previewLabel, kPreviewW - 4);
	lv_obj_align(previewLabel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_align(previewLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(previewLabel, LV_LABEL_LONG_WRAP);
	lv_label_set_text(previewLabel, "TORCH\nOFF");

	// Hint reads "PRESS LIGHT TO TURN ON" in dim purple while OFF
	// and "PRESS OFF TO TURN OFF" while ON. The text gets rewritten
	// on every render*().
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(hintLabel, 160);
	lv_obj_set_pos(hintLabel, 0, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "PRESS LIGHT TO TURN ON");
}

void PhoneFlashlight::buildWhitePanel() {
	// Built once in the ctor; toggled hidden/visible by render*().
	// Sits above the wallpaper / status bar / preview but BELOW the
	// soft-key bar (which is created after this) so the user can
	// always find OFF / BACK while the panel is lit.
	whitePanel = lv_obj_create(obj);
	lv_obj_remove_style_all(whitePanel);
	lv_obj_set_size(whitePanel, kPanelW, kPanelH);
	lv_obj_set_pos(whitePanel, kPanelX, kPanelY);
	lv_obj_set_style_radius(whitePanel, 0, 0);
	lv_obj_set_style_bg_opa(whitePanel, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(whitePanel, MP_FLASH_WHITE, 0);
	lv_obj_set_style_border_width(whitePanel, 0, 0);
	lv_obj_set_style_pad_all(whitePanel, 0, 0);
	lv_obj_clear_flag(whitePanel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(whitePanel, LV_OBJ_FLAG_HIDDEN);
}

// ---------- lifecycle -------------------------------------------------

void PhoneFlashlight::onStart() {
	Input::getInstance()->addListener(this);
	// Re-snapshot in case the brightness changed while the screen
	// was on the navigation stack (e.g. PhoneBrightnessScreen above
	// us tweaked it). The OFF state already matches the user's
	// setting, so no panel re-paint is needed here.
	initialBrightness = Settings.get().screenBrightness;
}

void PhoneFlashlight::onStop() {
	Input::getInstance()->removeListener(this);
	stopKeepAliveTimer();
	// Snapshot the lit-state out: if we were ON, the destructor /
	// next renderOff() will restore brightness; explicitly do it
	// here too in case the screen is paused without being destroyed
	// (e.g. a modal dialog covers us). Cheap idempotent write.
	if(on) {
		Chatter.setBrightness(initialBrightness);
	}
}

// ---------- render ----------------------------------------------------

void PhoneFlashlight::renderOff() {
	on = false;
	stopKeepAliveTimer();

	// Hide the lit panel.
	if(whitePanel != nullptr) {
		lv_obj_add_flag(whitePanel, LV_OBJ_FLAG_HIDDEN);
	}

	// Bring the OFF-state chrome back up.
	if(wallpaper != nullptr) {
		lv_obj_clear_flag(wallpaper->getLvObj(), LV_OBJ_FLAG_HIDDEN);
	}
	if(statusBar != nullptr) {
		lv_obj_clear_flag(statusBar->getLvObj(), LV_OBJ_FLAG_HIDDEN);
	}
	if(captionLabel != nullptr) {
		lv_obj_clear_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
	}
	if(previewBox != nullptr) {
		lv_obj_clear_flag(previewBox, LV_OBJ_FLAG_HIDDEN);
	}
	if(previewLabel != nullptr) {
		lv_label_set_text(previewLabel, "TORCH\nOFF");
	}
	if(hintLabel != nullptr) {
		lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(hintLabel, "PRESS LIGHT TO TURN ON");
	}
	if(softKeys != nullptr) {
		softKeys->setLeft("LIGHT");
	}

	// Restore the user's preferred brightness. Idempotent — the LEDC
	// driver short-circuits redundant writes.
	Chatter.setBrightness(initialBrightness);
}

void PhoneFlashlight::renderOn() {
	on = true;

	// Hide the OFF-state chrome so its purple-tinted pixels can't
	// bleed into the otherwise-pure white panel.
	if(wallpaper != nullptr) {
		lv_obj_add_flag(wallpaper->getLvObj(), LV_OBJ_FLAG_HIDDEN);
	}
	if(statusBar != nullptr) {
		lv_obj_add_flag(statusBar->getLvObj(), LV_OBJ_FLAG_HIDDEN);
	}
	if(captionLabel != nullptr) {
		lv_obj_add_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
	}
	if(previewBox != nullptr) {
		lv_obj_add_flag(previewBox, LV_OBJ_FLAG_HIDDEN);
	}

	// Reveal the white panel.
	if(whitePanel != nullptr) {
		lv_obj_clear_flag(whitePanel, LV_OBJ_FLAG_HIDDEN);
	}

	// Re-anchor the hint label at the bottom of the lit area, in a
	// faint grey so it reads without breaking the "this is a light"
	// effect, and reword it for the lit state. The label is
	// reparented to the white panel (via raise + restyle) — actually
	// we just restyle it: lv_obj is a sibling of the panel and gets
	// drawn after it because LVGL renders children in creation
	// order, and the soft-key bar (created last) draws above both.
	if(hintLabel != nullptr) {
		lv_obj_set_style_text_color(hintLabel, MP_FLASH_HINT, 0);
		lv_label_set_text(hintLabel, "PRESS OFF TO TURN OFF");
		lv_obj_clear_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
		// Move to the foreground so it sits above the white panel
		// rather than being painted under it.
		lv_obj_move_foreground(hintLabel);
	}

	if(softKeys != nullptr) {
		softKeys->setLeft("OFF");
		// Soft-keys must always render on top of the white panel so
		// the user can find OFF / BACK while blinded.
		lv_obj_move_foreground(softKeys->getLvObj());
	}

	// Crank the LEDC duty up to its brightest stop. This bypasses
	// Settings deliberately so the user's preferred brightness
	// survives the visit.
	Chatter.setBrightness(MaxBrightness);

	// Tell the idle-dim service we're "active" so it can't ramp the
	// panel down to its 30% dim state mid-emergency.
	IdleDim.resetActivity();

	// Schedule periodic keep-alives — once per second is well inside
	// PhoneIdleDim::IDLE_DIM_MS (30 000 ms) and costs a single
	// timestamp compare per tick.
	startKeepAliveTimer();
}

void PhoneFlashlight::toggle() {
	if(on) {
		renderOff();
	} else {
		renderOn();
	}
}

// ---------- keep-alive timer -----------------------------------------

void PhoneFlashlight::startKeepAliveTimer() {
	if(keepAliveTimer != nullptr) return;
	keepAliveTimer = lv_timer_create(&PhoneFlashlight::onKeepAliveTickStatic,
	                                 KeepAliveMs, this);
}

void PhoneFlashlight::stopKeepAliveTimer() {
	if(keepAliveTimer == nullptr) return;
	lv_timer_del(keepAliveTimer);
	keepAliveTimer = nullptr;
}

void PhoneFlashlight::onKeepAliveTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneFlashlight*>(timer->user_data);
	if(self == nullptr) return;

	// While the panel is lit, keep prodding IdleDim AND re-write
	// the max-brightness duty. The latter is defensive: any other
	// service that touches LEDC (SleepService fade-in, a future
	// notification overlay, etc.) might drag the duty down between
	// our turn-on and now; this loop snaps it back so the panel
	// stays at the brightest stop.
	if(!self->on) return;
	IdleDim.resetActivity();
	Chatter.setBrightness(MaxBrightness);
}

// ---------- input -----------------------------------------------------

void PhoneFlashlight::buttonPressed(uint i) {
	switch(i) {
		// Numpad keys all double as "toggle". The roadmap calls
		// for "full-white screen + brightness max" so any keypad
		// press should land on that experience as quickly as
		// possible — no need to remember which digit.
		case BTN_1:
		case BTN_2:
		case BTN_3:
		case BTN_4:
		case BTN_5:
		case BTN_6:
		case BTN_7:
		case BTN_8:
		case BTN_9:
		case BTN_ENTER:
			if(softKeys) softKeys->flashLeft();
			toggle();
			break;

		case BTN_L:
			// Left bumper mirrors the left softkey.
			if(softKeys) softKeys->flashLeft();
			toggle();
			break;

		case BTN_0:
			// Panic exit: kill the lit panel AND leave the screen
			// in one keystroke. Useful if the user pulls the phone
			// out of a pocket and just wants the light off NOW.
			if(on) renderOff();
			pop();
			break;

		case BTN_R:
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		case BTN_LEFT:
		case BTN_RIGHT:
			// No horizontal cursor today; absorb the keys so they
			// don't fall through to anything else.
			break;

		default:
			break;
	}
}

void PhoneFlashlight::buttonReleased(uint i) {
	switch(i) {
		case BTN_BACK:
			if(!backLongFired) {
				pop();
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneFlashlight::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK = short tap = exit. The flag suppresses
			// the matching short-press fire-back on release.
			backLongFired = true;
			pop();
			break;

		default:
			break;
	}
}
