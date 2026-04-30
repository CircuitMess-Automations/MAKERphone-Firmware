#include "PhoneNotificationToast.h"
#include "../Fonts/font.h"
#include <string.h>

// MAKERphone retro palette - kept identical with every other Phone* widget
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid, PhoneDialerKey, PhoneDialerPad,
// PhonePixelAvatar, PhoneChatBubble, PhoneSignalIcon, PhoneBatteryIcon)
// so a screen that mixes any of them reads as one coherent device UI.
// Duplicated rather than centralised at this small scale - if the
// palette ever moves out of the widgets, every widget moves together.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple background
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (badge / rule)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (badge for info / SMS)
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (slab background)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream (title / glyph)
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim caption (preview)

// ----- ctor / dtor -----

PhoneNotificationToast::PhoneNotificationToast(lv_obj_t* parent) : LVObject(parent){
	// The toast docks at fixed coordinates regardless of parent layout.
	// IGNORE_LAYOUT lets it sit on top of a flex / grid screen content
	// area without disturbing whatever else the parent is laying out.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);

	buildSlab();
	buildIcon();
	buildLabels();

	// Start fully hidden above the screen so the first frame the user
	// ever sees is the slide-in - never a "naked" toast pop.
	lv_obj_set_pos(obj, ToastX, -((int16_t) ToastHeight));
	lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	visible = false;
}

PhoneNotificationToast::~PhoneNotificationToast(){
	// Cancel pending callbacks before children get torn down so neither
	// the dismiss / hide timers nor any in-flight slide animation can
	// fire on a freed `this`.
	cancelTimers();
	lv_anim_del(obj, slideExec);
}

// ----- builders -----

void PhoneNotificationToast::buildSlab(){
	lv_obj_set_size(obj, ToastWidth, ToastHeight);
	lv_obj_set_style_bg_color(obj, MP_DIM, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, 2, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	// Thin sunset-orange rule along the bottom edge - mirrors the
	// PhoneStatusBar / PhoneSoftKeyBar rule so the toast reads as a
	// "second status bar" briefly visiting the screen.
	lv_obj_set_style_border_color(obj, MP_ACCENT, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_outline_width(obj, 0, 0);
}

void PhoneNotificationToast::buildIcon(){
	// 14x14 badge on the left, 3 px in from the slab edge, vertically
	// centered. Fixed size and absolute position so the layout never
	// fights with the labels next to it.
	iconBox = lv_obj_create(obj);
	lv_obj_remove_style_all(iconBox);
	lv_obj_clear_flag(iconBox, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(iconBox, 14, 14);
	lv_obj_set_pos(iconBox, 3, (ToastHeight - 14) / 2);
	lv_obj_set_style_radius(iconBox, 1, 0);
	lv_obj_set_style_bg_opa(iconBox, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(iconBox, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(iconBox, 0, 0);
	lv_obj_set_style_pad_all(iconBox, 0, 0);
}

void PhoneNotificationToast::buildLabels(){
	// Title row (top half). Pixelbasic7 keeps the line height compact
	// enough to fit two rows + 1 px breathing room into 28 px.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_TEXT, 0);
	lv_label_set_text(titleLabel, "");
	lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
	// 14 px badge + 3 px gutter on either side of it = labels start at
	// x = 3 + 14 + 3 = 20. Reserve 3 px on the right edge for slack.
	lv_obj_set_size(titleLabel, ToastWidth - 20 - 3, 11);
	lv_obj_set_pos(titleLabel, 20, 4);

	// Preview row (bottom half). Slightly dimmer so the title visually
	// dominates - the same hierarchy every Sony-Ericsson notification
	// banner used (bold sender, lighter preview).
	msgLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(msgLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(msgLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(msgLabel, "");
	lv_label_set_long_mode(msgLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_size(msgLabel, ToastWidth - 20 - 3, 11);
	lv_obj_set_pos(msgLabel, 20, 14);
}

// ----- icon glyph -----

void PhoneNotificationToast::clearIconGlyph(){
	// Drop every child of the badge background (the glyph "pixels").
	// The badge `iconBox` itself stays - only its children are
	// recomposed when the variant changes.
	if(iconBox == nullptr) return;
	uint32_t n = lv_obj_get_child_cnt(iconBox);
	for(uint32_t i = 0; i < n; i++){
		// Always pop child 0 because the count shrinks as we delete.
		lv_obj_t* child = lv_obj_get_child(iconBox, 0);
		if(child == nullptr) break;
		lv_obj_del(child);
	}
}

lv_obj_t* PhoneNotificationToast::px(int16_t x, int16_t y, uint16_t w, uint16_t h, lv_color_t color){
	lv_obj_t* p = lv_obj_create(iconBox);
	lv_obj_remove_style_all(p);
	lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(p, w, h);
	lv_obj_set_pos(p, x, y);
	lv_obj_set_style_radius(p, 0, 0);
	lv_obj_set_style_bg_color(p, color, 0);
	lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(p, 0, 0);
	lv_obj_set_style_pad_all(p, 0, 0);
	return p;
}

void PhoneNotificationToast::redrawIcon(Variant v){
	if(iconBox == nullptr) return;
	clearIconGlyph();

	// Pick the badge tint per variant. Cyan reads as "passive info"
	// (generic / SMS), orange reads as "attention" (call / battery).
	lv_color_t badge = MP_HIGHLIGHT;
	switch(v){
		case Variant::Call:
		case Variant::Battery:
			badge = MP_ACCENT;
			break;
		case Variant::Generic:
		case Variant::SMS:
		default:
			badge = MP_HIGHLIGHT;
			break;
	}
	lv_obj_set_style_bg_color(iconBox, badge, 0);

	// Glyph color stays the deep-purple background tone so the symbol
	// punches against the bright badge tint.
	const lv_color_t g = MP_BG_DARK;

	switch(v){
		case Variant::Call: {
			// Stylised handset glyph - two short ear/mouth caps joined
			// by a diagonal "cord" of three 2x2 cells. Compact enough
			// to read at 14x14 on a 160x128 panel.
			px(2,  3, 3, 2, g);             // ear pad
			px(9,  9, 3, 2, g);             // mouth pad
			px(4,  4, 2, 2, g);
			px(5,  6, 2, 2, g);
			px(7,  7, 2, 2, g);
			px(8,  8, 2, 2, g);
			break;
		}
		case Variant::SMS: {
			// Envelope: outer rectangle + V flap. Outer rect is 10x6
			// drawn as 4 thin sides so the glyph stays "outline-only"
			// and lets the badge tint show through the middle.
			px(2,  4, 10, 1, g);            // top
			px(2, 10, 10, 1, g);            // bottom
			px(2,  4,  1, 7, g);            // left
			px(11, 4,  1, 7, g);            // right
			// V flap - two diagonal pixel runs meeting in the middle.
			px(3,  5, 1, 1, g);
			px(4,  6, 1, 1, g);
			px(5,  7, 1, 1, g);
			px(6,  8, 2, 1, g);             // valley
			px(8,  7, 1, 1, g);
			px(9,  6, 1, 1, g);
			px(10, 5, 1, 1, g);
			break;
		}
		case Variant::Battery: {
			// Tiny battery - 10x6 outline + 2x4 tip + a 2-cell fill
			// so the glyph reads even at 14x14.
			px(2,  4, 10, 1, g);            // top
			px(2,  9, 10, 1, g);            // bottom
			px(2,  5, 1, 4, g);             // left
			px(11, 5, 1, 4, g);             // right
			px(12, 6, 1, 2, g);             // tip
			// Fill - 2 cells of "low" charge to telegraph the variant.
			px(3,  6, 2, 2, g);
			break;
		}
		case Variant::Generic:
		default: {
			// Exclamation in a 6x10 frame - top stem + bottom dot.
			px(6,  3, 2, 5, g);             // stem
			px(6, 9, 2, 2, g);              // dot
			break;
		}
	}
}

// ----- public API -----

void PhoneNotificationToast::show(Variant variant, const char* title,
								  const char* message, uint32_t durationMs){
	currentVariant = variant;

	// Refresh content first so the slide-in already shows the right
	// glyph / text on its very first frame.
	redrawIcon(variant);
	if(titleLabel != nullptr){
		lv_label_set_text(titleLabel, title == nullptr ? "" : title);
	}
	const bool hasMsg = (message != nullptr) && (message[0] != '\0');
	if(msgLabel != nullptr){
		lv_label_set_text(msgLabel, hasMsg ? message : "");
		// Vertically centre the title when there is no preview line,
		// else keep the two-line layout.
		if(hasMsg){
			lv_obj_set_pos(titleLabel, 20, 4);
		}else{
			lv_obj_set_pos(titleLabel, 20, (ToastHeight - 11) / 2);
		}
	}

	// If a previous show() is still in flight cancel its slide / timers
	// and slide-in from the current y, so two rapid show()s feel
	// "merged" rather than "stacked".
	cancelTimers();
	lv_anim_del(obj, slideExec);

	lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	visible = true;

	// Bring to front in case the host screen built the toast before
	// adding other top-level widgets - we want the notification on top
	// of any background bg / status bar / softkey bar.
	lv_obj_move_foreground(obj);

	slideTo((int16_t) StatusBarHeight);

	// Schedule auto-dismiss. duration == 0 means "stay until hide()".
	if(durationMs > 0){
		dismissTimer = lv_timer_create(onDismissTimer, durationMs + SlideMs, this);
		lv_timer_set_repeat_count(dismissTimer, 1);
	}
}

void PhoneNotificationToast::hide(){
	if(!visible) return;
	cancelTimers();
	lv_anim_del(obj, slideExec);
	slideTo(-((int16_t) ToastHeight));
	// Mark not-visible only after the slide-out completes; until then
	// show() re-issued during the slide-out reuses the same instance.
	hideTimer = lv_timer_create(onHideDone, SlideMs + 20, this);
	lv_timer_set_repeat_count(hideTimer, 1);
}

// ----- slide animation -----

void PhoneNotificationToast::slideTo(int16_t targetY){
	const int16_t fromY = (int16_t) lv_obj_get_y(obj);

	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, obj);
	lv_anim_set_values(&a, fromY, targetY);
	lv_anim_set_time(&a, SlideMs);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
	lv_anim_set_exec_cb(&a, slideExec);
	lv_anim_start(&a);
}

void PhoneNotificationToast::slideExec(void* var, int32_t v){
	auto* target = static_cast<lv_obj_t*>(var);
	lv_obj_set_y(target, (lv_coord_t) v);
}

void PhoneNotificationToast::onDismissTimer(lv_timer_t* timer){
	auto* self = static_cast<PhoneNotificationToast*>(timer->user_data);
	if(self == nullptr) return;
	// One-shot: LVGL deletes the timer itself after this firing, so we
	// just drop our pointer to avoid a double-delete in cancelTimers().
	self->dismissTimer = nullptr;
	if(!self->visible) return;
	self->slideTo(-((int16_t) PhoneNotificationToast::ToastHeight));
	// Schedule the final hide flip a tick after the slide-out completes.
	self->hideTimer = lv_timer_create(onHideDone, SlideMs + 20, self);
	lv_timer_set_repeat_count(self->hideTimer, 1);
}

void PhoneNotificationToast::onHideDone(lv_timer_t* timer){
	auto* self = static_cast<PhoneNotificationToast*>(timer->user_data);
	if(self == nullptr) return;
	self->hideTimer = nullptr;
	self->visible = false;
	if(self->obj != nullptr){
		lv_obj_add_flag(self->obj, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneNotificationToast::cancelTimers(){
	if(dismissTimer != nullptr){
		lv_timer_del(dismissTimer);
		dismissTimer = nullptr;
	}
	if(hideTimer != nullptr){
		lv_timer_del(hideTimer);
		hideTimer = nullptr;
	}
}
