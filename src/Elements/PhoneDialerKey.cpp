#include "PhoneDialerKey.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept consistent with the other phone widgets
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid). One source of truth duplicated by
// every widget keeps things straightforward at this small scale - if the
// palette ever moves out of the widgets, every widget switches together.
#define MP_BG_DARK        lv_color_make(20, 12, 36)     // deep purple key background
#define MP_ACCENT         lv_color_make(255, 140, 30)   // sunset orange (selected border + halo)
#define MP_ACCENT_BRIGHT  lv_color_make(255, 200, 80)   // bright sunset yellow (press flash)
#define MP_HIGHLIGHT      lv_color_make(122, 232, 255)  // cyan (letters caption when selected)
#define MP_DIM            lv_color_make(70, 56, 100)    // muted purple (idle border)
#define MP_TEXT           lv_color_make(255, 220, 180)  // warm cream digit glyph
#define MP_LABEL_DIM      lv_color_make(170, 140, 200)  // dim caption when not selected

PhoneDialerKey::PhoneDialerKey(lv_obj_t* parent, char glyph, const char* lettersIn)
		: LVObject(parent), glyph(glyph), halo(nullptr),
		  digitLabel(nullptr), lettersLabel(nullptr){

	if(lettersIn != nullptr && lettersIn[0] != '\0'){
		letters = String(lettersIn);
	}

	// Fixed-size widget that flows naturally inside a flex / grid parent
	// (no IGNORE_LAYOUT flag). The future PhoneDialerPad will use a 3-col
	// flex-row-wrap container so 12 keys arrange themselves into the
	// classic 3x4 dialer.
	lv_obj_set_size(obj, KeyWidth, KeyHeight);

	buildBackground();
	buildHalo();
	buildDigitLabel();
	buildLettersLabel(letters.c_str());
	refreshSelection();
}

PhoneDialerKey::~PhoneDialerKey(){
	// Cancel any pending press-flash timer so it can't fire on a freed
	// `this` after we go away. The halo's pulse animation is parented
	// on the lv_obj and tears itself down via LVGL's normal child cleanup
	// when the base LVObject destroys its obj - no manual cleanup needed
	// for the animation here.
	if(flashTimer != nullptr){
		lv_timer_del(flashTimer);
		flashTimer = nullptr;
	}
}

// ----- builders -----

void PhoneDialerKey::buildBackground(){
	// Dark purple slab with a thin idle-purple border. The border color
	// switches to MP_ACCENT when the key is selected, and momentarily to
	// MP_ACCENT_BRIGHT during a pressFlash(). 2 px corner radius matches
	// PhoneIconTile so mixed grids share a visual language.
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, 2, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_color(obj, MP_DIM, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
}

void PhoneDialerKey::buildHalo(){
	// Outer glow ring - same approach as PhoneIconTile: an over-sized
	// transparent-fill child whose border color matches MP_ACCENT and
	// whose border opacity ping-pongs while selected. Sent to the back
	// so only its border peeks past the key's body.
	halo = lv_obj_create(obj);
	lv_obj_remove_style_all(halo);
	lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(halo, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(halo, KeyWidth + 4, KeyHeight + 4);
	lv_obj_set_align(halo, LV_ALIGN_CENTER);
	lv_obj_set_style_radius(halo, 4, 0);
	lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(halo, MP_ACCENT, 0);
	lv_obj_set_style_border_width(halo, 2, 0);
	lv_obj_set_style_border_opa(halo, LV_OPA_TRANSP, 0);
	lv_obj_move_background(halo);
}

void PhoneDialerKey::buildDigitLabel(){
	// Big glyph using pixelbasic16 - the same font PhoneClockFace uses
	// for the homescreen time, so the dialer feels like part of the same
	// hardware. Anchored top-mid with a small top inset so the letters
	// caption has clean room beneath it.
	digitLabel = lv_label_create(obj);
	lv_obj_add_flag(digitLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(digitLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(digitLabel, MP_TEXT, 0);

	// Build a 2-char NUL-terminated string for lv_label_set_text. Using
	// a stack buffer to avoid keeping a String alive just for a single char.
	char buf[2] = { glyph, '\0' };
	lv_label_set_text(digitLabel, buf);

	if(letters.length() > 0){
		// Sit slightly higher so the caption fits underneath comfortably.
		lv_obj_set_align(digitLabel, LV_ALIGN_TOP_MID);
		lv_obj_set_y(digitLabel, -1);
	}else{
		// No caption - center the glyph vertically for a clean key.
		lv_obj_set_align(digitLabel, LV_ALIGN_CENTER);
		lv_obj_set_y(digitLabel, 0);
	}
}

void PhoneDialerKey::buildLettersLabel(const char* lettersIn){
	if(lettersIn == nullptr || lettersIn[0] == '\0'){
		lettersLabel = nullptr;
		return;
	}
	lettersLabel = lv_label_create(obj);
	lv_obj_add_flag(lettersLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(lettersLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(lettersLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(lettersLabel, lettersIn);
	lv_obj_set_align(lettersLabel, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(lettersLabel, -1);
}

// ----- public API -----

void PhoneDialerKey::setSelected(bool sel){
	if(selected == sel) return;
	selected = sel;
	refreshSelection();
}

void PhoneDialerKey::pressFlash(){
	// Bright accent border for a moment, then back to the prior look.
	// We restart the timer if a flash is already in progress so rapid
	// presses always show the latest flash for a clean FlashDuration.
	lv_obj_set_style_border_color(obj, MP_ACCENT_BRIGHT, 0);

	if(flashTimer != nullptr){
		lv_timer_del(flashTimer);
		flashTimer = nullptr;
	}
	flashTimer = lv_timer_create(flashEndCb, FlashDuration, this);
	lv_timer_set_repeat_count(flashTimer, 1);
}

// ----- internal -----

void PhoneDialerKey::refreshSelection(){
	// Idle look: muted purple border, dim caption, transparent halo,
	//            no animation running.
	// Selected look: orange border, cyan caption, halo border opacity
	//                pulses between LV_OPA_30 and LV_OPA_80 forever.
	if(selected){
		lv_obj_set_style_border_color(obj, MP_ACCENT, 0);
		if(lettersLabel != nullptr){
			lv_obj_set_style_text_color(lettersLabel, MP_HIGHLIGHT, 0);
		}

		lv_anim_t a;
		lv_anim_init(&a);
		lv_anim_set_var(&a, halo);
		lv_anim_set_values(&a, LV_OPA_30, LV_OPA_80);
		lv_anim_set_time(&a, HaloPulsePeriod);
		lv_anim_set_playback_time(&a, HaloPulsePeriod);
		lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
		lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
		lv_anim_set_exec_cb(&a, haloPulseExec);
		lv_anim_start(&a);
	}else{
		// Cancel any running pulse and snap halo back to invisible.
		lv_anim_del(halo, haloPulseExec);
		lv_obj_set_style_border_opa(halo, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_color(obj, MP_DIM, 0);
		if(lettersLabel != nullptr){
			lv_obj_set_style_text_color(lettersLabel, MP_LABEL_DIM, 0);
		}
	}
}

void PhoneDialerKey::haloPulseExec(void* var, int32_t v){
	auto target = static_cast<lv_obj_t*>(var);
	lv_obj_set_style_border_opa(target, (lv_opa_t) v, 0);
}

void PhoneDialerKey::flashEndCb(lv_timer_t* timer){
	auto* self = static_cast<PhoneDialerKey*>(timer->user_data);
	// Restore the border to whatever the current selection look wants -
	// MP_ACCENT when the key is the cursor, MP_DIM otherwise. We do NOT
	// call refreshSelection() here because that would also restart the
	// halo pulse animation from scratch on every keystroke, which would
	// visibly interrupt the breath rhythm. The pulse itself is untouched
	// by pressFlash() (it only writes to the border), so it keeps ticking
	// happily in the background and we just snap the border colour back.
	lv_obj_set_style_border_color(self->obj,
	                              self->selected ? MP_ACCENT : MP_DIM, 0);
	self->flashTimer = nullptr;
}
