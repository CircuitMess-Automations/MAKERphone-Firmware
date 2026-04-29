#include "PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include <string.h>

// MAKERphone retro palette (kept consistent with PhoneStatusBar).
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple bar background
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (separator + arrows)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (label text)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream (center hint)

PhoneSoftKeyBar::PhoneSoftKeyBar(lv_obj_t* parent) : LVObject(parent){
	leftLabel   = nullptr;
	rightLabel  = nullptr;
	centerLabel = nullptr;
	leftArrow   = nullptr;
	rightArrow  = nullptr;

	// Anchor to the bottom of the 160x128 display regardless of parent layout.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, ScreenWidth, BarHeight);
	lv_obj_set_pos(obj, 0, ScreenHeight - BarHeight);

	buildBackground();
	buildLabels();
	buildArrows();
	refreshArrows();
}

// ----- builders -----

void PhoneSoftKeyBar::buildBackground(){
	// Background slab + thin orange separator at the top edge (mirrors the
	// status bar's bottom separator so the two frame the screen content).
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_color(obj, MP_ACCENT, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP, 0);
}

void PhoneSoftKeyBar::buildLabels(){
	// Left softkey label - bottom-left corner, leaving 5 px for the arrow.
	leftLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(leftLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(leftLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(leftLabel, "");
	lv_obj_set_align(leftLabel, LV_ALIGN_LEFT_MID);
	lv_obj_set_x(leftLabel, 7);
	lv_obj_add_flag(leftLabel, LV_OBJ_FLAG_HIDDEN);

	// Right softkey label - bottom-right corner, leaving 5 px for the arrow.
	rightLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(rightLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(rightLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(rightLabel, "");
	lv_obj_set_align(rightLabel, LV_ALIGN_RIGHT_MID);
	lv_obj_set_x(rightLabel, -7);
	lv_obj_add_flag(rightLabel, LV_OBJ_FLAG_HIDDEN);

	// Optional center hint - quieter cream color so it does not compete
	// with the active softkey labels.
	centerLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(centerLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(centerLabel, MP_TEXT, 0);
	lv_label_set_text(centerLabel, "");
	lv_obj_set_align(centerLabel, LV_ALIGN_CENTER);
	lv_obj_add_flag(centerLabel, LV_OBJ_FLAG_HIDDEN);
}

void PhoneSoftKeyBar::buildArrows(){
	// Both arrows are 3-pixel wide pixel-art triangles, 5 px tall, drawn as
	// a stack of three thin rectangles approximating the triangle shape.
	// LVGL 8.x has no native triangle primitive that renders cleanly at
	// this scale, so a stack of opaque rectangles gives the crispest look.

	// Left arrow (points left): rows widths 1, 2, 3 from top to bottom-left
	// of the triangle, then mirrored back. We approximate with three rects:
	//   row y=2: width 3 (the wide back)
	//   row y=1,3: width 2 (middle)
	//   row y=0,4: width 1 (the tip pixels)
	// Anchored at x=2..4, y=2..6.
	leftArrow = lv_obj_create(obj);
	lv_obj_remove_style_all(leftArrow);
	lv_obj_clear_flag(leftArrow, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(leftArrow, 3, 5);
	lv_obj_set_pos(leftArrow, 2, 2);
	lv_obj_set_style_bg_color(leftArrow, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(leftArrow, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(leftArrow, 0, 0);

	rightArrow = lv_obj_create(obj);
	lv_obj_remove_style_all(rightArrow);
	lv_obj_clear_flag(rightArrow, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(rightArrow, 3, 5);
	lv_obj_set_pos(rightArrow, ScreenWidth - 5, 2);
	lv_obj_set_style_bg_color(rightArrow, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(rightArrow, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(rightArrow, 0, 0);
}

// ----- public setters -----

void PhoneSoftKeyBar::setLeft(const char* label){
	if(label == nullptr) label = "";
	lv_label_set_text(leftLabel, label);
	if(label[0] == '\0'){
		lv_obj_add_flag(leftLabel, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_clear_flag(leftLabel, LV_OBJ_FLAG_HIDDEN);
	}
	refreshArrows();
}

void PhoneSoftKeyBar::setRight(const char* label){
	if(label == nullptr) label = "";
	lv_label_set_text(rightLabel, label);
	if(label[0] == '\0'){
		lv_obj_add_flag(rightLabel, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_clear_flag(rightLabel, LV_OBJ_FLAG_HIDDEN);
	}
	refreshArrows();
}

void PhoneSoftKeyBar::setCenter(const char* label){
	if(label == nullptr) label = "";
	lv_label_set_text(centerLabel, label);
	if(label[0] == '\0'){
		lv_obj_add_flag(centerLabel, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_clear_flag(centerLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneSoftKeyBar::setShowArrows(bool show){
	if(showArrows == show) return;
	showArrows = show;
	refreshArrows();
}

// ----- internal -----

void PhoneSoftKeyBar::refreshArrows(){
	// Hide the arrow when the corresponding label is empty so a single-
	// softkey screen does not show a stray arrow on the unused side. Also
	// honor the global showArrows toggle.
	bool leftVisible  = showArrows && !lv_obj_has_flag(leftLabel, LV_OBJ_FLAG_HIDDEN);
	bool rightVisible = showArrows && !lv_obj_has_flag(rightLabel, LV_OBJ_FLAG_HIDDEN);

	if(leftVisible){
		lv_obj_clear_flag(leftArrow, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_add_flag(leftArrow, LV_OBJ_FLAG_HIDDEN);
	}

	if(rightVisible){
		lv_obj_clear_flag(rightArrow, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_add_flag(rightArrow, LV_OBJ_FLAG_HIDDEN);
	}
}
