#include "PhoneMissedCallFlash.h"

// MAKERphone retro palette - kept identical with every other Phone*
// widget (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid, PhoneDialerKey, PhoneDialerPad,
// PhonePixelAvatar, PhoneChatBubble, PhoneSignalIcon, PhoneBatteryIcon,
// PhoneNotificationToast, PhoneConfettiOverlay, PhoneEnvelopeFly,
// PhoneChargeBars, PhoneIdleHint) so the flash reads as a flourish
// that belongs to the same device. Duplicated rather than centralised
// at this small scale - if the palette ever moves out of the widgets,
// every widget moves together.
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream

// One opacity table drives the entire pulse train. The animation runs
// over [0 .. PulseCount * 2 * StepUnits) and the exec callback maps
// the linear input value to PulseCount triangular pulses. We compute
// the per-step duration from the public FlashMs / PulseCount constants
// so a tweak to the constants does not require touching this file.
//
// Triangular pulse model: each pulse rises from 0 -> PeakOpa over the
// first half-step then falls PeakOpa -> 0 over the second half-step.
// Stitching PulseCount of these end-to-end gives the
// 0 -> peak -> 0 -> peak -> 0 -> peak -> 0 train the lock screen
// expects.

namespace {

constexpr int32_t kStepUnits = 1024;     // arbitrary integer resolution per pulse
constexpr int32_t kHalfStep  = kStepUnits / 2;

// Total span of the animation in our integer step domain. The exec
// callback below divides the input value back out into pulses to find
// the per-pulse local progress.
constexpr int32_t kAnimMax   = (int32_t)PhoneMissedCallFlash::PulseCount * kStepUnits;

} // namespace

// ----- ctor / dtor -------------------------------------------------------

PhoneMissedCallFlash::PhoneMissedCallFlash(lv_obj_t* parent) : LVObject(parent) {
	// Full-screen transparent container. Sits on top of every other
	// widget so the pulse fully covers the lock face, status bar,
	// clock, soft-key bar, synthwave wallpaper and the optional
	// charging chip / charge bars without per-widget opacity dances.
	// Hit-testing is disabled so input listeners keep receiving every
	// key press while the flash is on screen - the unlock slide and
	// the soft-keys remain responsive even mid-pulse.
	lv_obj_remove_style_all(obj);
	lv_obj_set_size(obj, OverlayWidth, OverlayHeight);
	lv_obj_set_pos(obj, 0, 0);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Single full-screen sheet. Bg colour is the warm-cream MP_TEXT
	// so on the dark synthwave wallpaper the flash reads as a clear
	// inverted-color frame; bg_opa is animated 0 -> PeakOpa -> 0
	// in three pulses, the only animated channel on this widget.
	sheet = lv_obj_create(obj);
	lv_obj_remove_style_all(sheet);
	lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(sheet, OverlayWidth, OverlayHeight);
	lv_obj_set_pos(sheet, 0, 0);
	lv_obj_set_style_radius(sheet, 0, 0);
	lv_obj_set_style_bg_color(sheet, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(sheet, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(sheet, 0, 0);
	lv_obj_set_style_pad_all(sheet, 0, 0);

	// Hidden until start() - keeps the wallpaper visible on every
	// frame between flashes, and avoids the LVGL render pass paying
	// for an OPA_TRANSP full-screen blit every tick.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

PhoneMissedCallFlash::~PhoneMissedCallFlash() {
	// Yank any in-flight animation so a teardown mid-flash does not
	// fire the exec callback against a freed sheet pointer.
	if(sheet != nullptr){
		lv_anim_del(sheet, opaExec);
	}
}

// ----- public api --------------------------------------------------------

void PhoneMissedCallFlash::start() {
	if(sheet == nullptr) return;

	// Idempotent restart - cancel any in-flight animation, surface
	// the overlay, then relaunch the single opacity channel from
	// frame 0.
	lv_anim_del(sheet, opaExec);

	// Reset the sheet to fully transparent so a re-trigger does not
	// flash the residual peak opacity from the previous pulse train.
	lv_obj_set_style_bg_opa(sheet, LV_OPA_TRANSP, 0);

	lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);

	// Single channel: opacity ramped from 0 -> kAnimMax linearly.
	// The exec callback maps the input value to a triangular pulse
	// inside each kStepUnits-wide window, producing PulseCount
	// peaks evenly spaced across `FlashMs`. Linear path keeps the
	// per-pulse rise + fall symmetric and predictable.
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, sheet);
	lv_anim_set_values(&a, 0, kAnimMax);
	lv_anim_set_time(&a, FlashMs);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_set_exec_cb(&a, opaExec);
	// onAllDone hides the overlay so the next start() has a clean
	// canvas and the LVGL render pass stops paying for the sheet.
	lv_anim_set_ready_cb(&a, onAllDone);
	lv_anim_start(&a);

	active = true;
}

void PhoneMissedCallFlash::stop() {
	if(sheet != nullptr) {
		lv_anim_del(sheet, opaExec);
		// Snap the sheet back to fully transparent so a stop()
		// mid-pulse does not leave a frozen bright frame on screen.
		lv_obj_set_style_bg_opa(sheet, LV_OPA_TRANSP, 0);
	}
	lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	active = false;
}

// ----- animation callback -----------------------------------------------

void PhoneMissedCallFlash::opaExec(void* var, int32_t v) {
	auto s = static_cast<lv_obj_t*>(var);
	if(s == nullptr) return;

	// Map the linear [0..kAnimMax) input to a per-pulse local
	// progress in [0..kStepUnits) via integer modulo. Each pulse
	// rises 0 -> PeakOpa over the first half then falls PeakOpa
	// -> 0 over the second half - a triangle wave with PulseCount
	// peaks across the full flash duration.
	int32_t local = v % kStepUnits;
	int32_t opa;
	if(local < kHalfStep){
		// Rising half: linearly scale local in [0..kHalfStep) to
		// [0..PeakOpa].
		opa = ((int32_t) PeakOpa * local) / kHalfStep;
	}else{
		// Falling half: linearly scale (kStepUnits - local) in
		// (0..kHalfStep] to (0..PeakOpa]. Subtract first to keep
		// the integer math monotonic and clamp-friendly.
		const int32_t falling = kStepUnits - local;
		opa = ((int32_t) PeakOpa * falling) / kHalfStep;
	}

	// Defensive clamp - integer truncation can never overshoot
	// PeakOpa with the math above, but we still pin the range so
	// future tweaks to PeakOpa / kStepUnits cannot leak a value
	// outside lv_opa_t range to the LVGL render pass.
	if(opa < 0) opa = 0;
	if(opa > 255) opa = 255;

	lv_obj_set_style_bg_opa(s, static_cast<lv_opa_t>(opa), 0);
}

void PhoneMissedCallFlash::onAllDone(lv_anim_t* a) {
	// `a->var` is the sheet lv_obj. Its parent is the overlay
	// container - hop one level up and hide the overlay so the
	// next start() call has a clean canvas, and the LVGL render
	// pass stops paying for the sheet between flashes.
	auto sheet = static_cast<lv_obj_t*>(a->var);
	if(sheet == nullptr) return;
	// Snap the sheet back to fully transparent in case the
	// animation engine writes a final non-zero value before
	// firing ready_cb - belt-and-braces against a stuck-bright
	// frame on the LVGL clean-up tick.
	lv_obj_set_style_bg_opa(sheet, LV_OPA_TRANSP, 0);
	lv_obj_t* overlay = lv_obj_get_parent(sheet);
	if(overlay != nullptr){
		lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
	}
}
