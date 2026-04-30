#include "PhoneIconTile.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept consistent with the other phone widgets
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneSynthwaveBg).
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple tile background
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (selected border + halo)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (icon strokes)
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (idle border)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream label
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim label when not selected

PhoneIconTile::PhoneIconTile(lv_obj_t* parent, Icon icon, const char* label)
		: LVObject(parent), icon(icon), halo(nullptr), iconLayer(nullptr), labelEl(nullptr){

	// The tile is a fixed-size widget that flows naturally inside a flex
	// or grid parent (no IGNORE_LAYOUT flag, see header notes).
	lv_obj_set_size(obj, TileWidth, TileHeight);

	buildBackground();
	buildHalo();
	buildIconLayer();
	buildLabel(label);

	// Dispatch to the per-icon builder. Each pretends iconLayer is a 16x16
	// grid and drops opaque rectangles via px().
	switch(icon){
		case Icon::Phone:    drawPhone();    break;
		case Icon::Messages: drawMessages(); break;
		case Icon::Contacts: drawContacts(); break;
		case Icon::Music:    drawMusic();    break;
		case Icon::Camera:   drawCamera();   break;
		case Icon::Games:    drawGames();    break;
		case Icon::Settings: drawSettings(); break;
		case Icon::Mail:     drawMail();     break;
	}

	refreshSelection();
}

// ----- builders -----

void PhoneIconTile::buildBackground(){
	// Dark purple slab with a thin idle-purple border. The border color
	// switches to MP_ACCENT when the tile is selected. Rounded 2 px to
	// echo the soft Sony-Ericsson tile look without going overboard at
	// this resolution.
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, 2, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_color(obj, MP_DIM, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
}

void PhoneIconTile::buildHalo(){
	// Outer glow ring. Lives as the FIRST child of the tile so it draws
	// behind the background; we set its size larger than the tile and
	// align centered, then mask its draw to only the border via radius +
	// no fill. When the tile is unselected it is fully transparent; when
	// selected an animation ping-pongs its opacity for the "live" feel.
	halo = lv_obj_create(obj);
	lv_obj_remove_style_all(halo);
	lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(halo, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(halo, TileWidth + 4, TileHeight + 4);
	lv_obj_set_align(halo, LV_ALIGN_CENTER);
	lv_obj_set_style_radius(halo, 4, 0);
	lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(halo, MP_ACCENT, 0);
	lv_obj_set_style_border_width(halo, 2, 0);
	lv_obj_set_style_border_opa(halo, LV_OPA_TRANSP, 0);
	// Send to back so it sits behind the tile body and only its border
	// pokes out around the edges, simulating a glow ring.
	lv_obj_move_background(halo);
}

void PhoneIconTile::buildIconLayer(){
	// 16x16 transparent container that holds the per-icon pixel rectangles.
	// Centered horizontally; sits 3 px from the top of the tile so the
	// label has room beneath it (tile is 36 high: 3 pad + 16 icon + 2 gap +
	// ~9 label + 3 pad = 33, with a couple px of slack for the border).
	iconLayer = lv_obj_create(obj);
	lv_obj_remove_style_all(iconLayer);
	lv_obj_clear_flag(iconLayer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(iconLayer, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(iconLayer, IconSize, IconSize);
	lv_obj_set_align(iconLayer, LV_ALIGN_TOP_MID);
	lv_obj_set_y(iconLayer, 3);
	lv_obj_set_style_bg_opa(iconLayer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(iconLayer, 0, 0);
	lv_obj_set_style_border_width(iconLayer, 0, 0);
}

void PhoneIconTile::buildLabel(const char* label){
	if(label == nullptr || label[0] == '\0'){
		labelEl = nullptr;
		return;
	}
	labelEl = lv_label_create(obj);
	lv_obj_add_flag(labelEl, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(labelEl, &pixelbasic7, 0);
	lv_obj_set_style_text_color(labelEl, MP_LABEL_DIM, 0);
	lv_label_set_text(labelEl, label);
	lv_obj_set_align(labelEl, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(labelEl, -2);
}

// ----- public API -----

void PhoneIconTile::setSelected(bool sel){
	if(selected == sel) return;
	selected = sel;
	refreshSelection();
}

// ----- internal -----

void PhoneIconTile::refreshSelection(){
	// Idle look: muted purple border, dim label, transparent halo, no anim.
	// Selected look: orange border, cream label, halo border opacity pulses
	//                between LV_OPA_30 and LV_OPA_80 forever.
	if(selected){
		lv_obj_set_style_border_color(obj, MP_ACCENT, 0);
		if(labelEl != nullptr){
			lv_obj_set_style_text_color(labelEl, MP_TEXT, 0);
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
		// Cancel any running pulse and snap the halo back to invisible.
		lv_anim_del(halo, haloPulseExec);
		lv_obj_set_style_border_opa(halo, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_color(obj, MP_DIM, 0);
		if(labelEl != nullptr){
			lv_obj_set_style_text_color(labelEl, MP_LABEL_DIM, 0);
		}
	}
}

void PhoneIconTile::haloPulseExec(void* var, int32_t v){
	auto target = static_cast<lv_obj_t*>(var);
	lv_obj_set_style_border_opa(target, (lv_opa_t) v, 0);
}

// ----- composition primitive -----

lv_obj_t* PhoneIconTile::px(int16_t x, int16_t y, uint16_t w, uint16_t h, lv_color_t color){
	lv_obj_t* p = lv_obj_create(iconLayer);
	lv_obj_remove_style_all(p);
	lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(p, w, h);
	lv_obj_set_pos(p, x, y);
	lv_obj_set_style_bg_color(p, color, 0);
	lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(p, 0, 0);
	return p;
}

// ----- per-icon builders -----
//
// Every icon pretends iconLayer is a 16x16 grid (origin top-left). All the
// numeric coordinates below are in that local coordinate system. Each
// icon uses 5-12 small rectangles - just enough to read at this scale.

// Classic slanted handset (earpiece top-left, mouthpiece bottom-right,
// connecting handle as a stair-step diagonal). Reads as a phone receiver
// at 16x16, mirroring the SonyEricsson "Phone" menu icon.
void PhoneIconTile::drawPhone(){
	const lv_color_t c = MP_HIGHLIGHT;
	// Earpiece blob (top-left).
	px(1, 2, 4, 3, c);
	px(0, 3, 1, 1, c);          // tiny corner pixel (rounding hint)
	px(4, 1, 1, 1, c);
	// Diagonal handle - 6 stacked 2x2 squares stepping down-right.
	px(4, 4, 2, 2, c);
	px(5, 5, 2, 2, c);
	px(6, 6, 2, 2, c);
	px(7, 7, 2, 2, c);
	px(8, 8, 2, 2, c);
	px(9, 9, 2, 2, c);
	// Mouthpiece blob (bottom-right).
	px(11, 11, 4, 3, c);
	px(15, 12, 1, 1, c);        // tiny corner pixel
	px(11, 14, 1, 1, c);
}

// Hollow speech bubble with a small tail at the bottom-left. Outline
// is drawn as four edge rectangles so the inside reads as the bubble.
void PhoneIconTile::drawMessages(){
	const lv_color_t c = MP_HIGHLIGHT;
	// Top edge.
	px(2, 2, 12, 1, c);
	// Bottom edge.
	px(2, 9, 9, 1, c);
	// Left edge.
	px(2, 2, 1, 8, c);
	// Right edge.
	px(13, 2, 1, 8, c);
	// Tail (down-left from the bubble).
	px(4, 10, 2, 1, c);
	px(3, 11, 2, 1, c);
	px(3, 12, 1, 1, c);
}

// Person silhouette: 4x4 head over a wider rounded shoulders/body.
void PhoneIconTile::drawContacts(){
	const lv_color_t c = MP_HIGHLIGHT;
	// Head - 4x4 with rounded corners suggested by missing corner pixels.
	px(6, 2, 4, 4, c);
	// Shoulders/body - wider trapezoid stack.
	px(4, 8, 8, 1, c);
	px(3, 9, 10, 1, c);
	px(3, 10, 10, 4, c);
}

// Eighth note: stem + flag + filled head (rectangular, reads as a quaver).
void PhoneIconTile::drawMusic(){
	const lv_color_t c = MP_HIGHLIGHT;
	// Stem.
	px(8, 2, 2, 9, c);
	// Flag at the top of the stem (two stair-step rectangles for the curl).
	px(10, 2, 3, 3, c);
	px(11, 5, 2, 2, c);
	// Note head (oval-ish, drawn as a stack of three rows).
	px(4, 10, 5, 1, c);
	px(3, 11, 6, 3, c);
}

// Compact camera with body outline, lens, viewfinder bump and flash dot.
void PhoneIconTile::drawCamera(){
	const lv_color_t c = MP_HIGHLIGHT;
	// Body outline (top, bottom, left, right rectangles).
	px(1, 5, 14, 1, c);
	px(1, 13, 14, 1, c);
	px(1, 5, 1, 9, c);
	px(14, 5, 1, 9, c);
	// Top viewfinder bump (sits above the body).
	px(8, 3, 4, 2, c);
	// Lens (filled square in the middle of the body).
	px(6, 7, 4, 4, MP_ACCENT);
	// Flash indicator (small accent dot, top-right of the body).
	px(12, 7, 1, 1, MP_ACCENT);
}

// D-pad cross with an action button to the side. Reads as a generic
// "Games" icon - the same archetype Sony Ericsson used for its games menu.
void PhoneIconTile::drawGames(){
	const lv_color_t c = MP_HIGHLIGHT;
	// Horizontal arm of the D-pad.
	px(2, 7, 9, 3, c);
	// Vertical arm of the D-pad.
	px(5, 4, 3, 9, c);
	// Action button (right side - hint of A/B button).
	px(12, 5, 3, 3, MP_ACCENT);
	px(12, 9, 3, 3, MP_ACCENT);
}

// Gear silhouette: hub + four cardinal teeth. The hub stays solid because
// drawing a hollow center would require subtraction at this scale.
void PhoneIconTile::drawSettings(){
	const lv_color_t c = MP_HIGHLIGHT;
	// Center hub.
	px(5, 5, 6, 6, c);
	// Four cardinal teeth.
	px(7, 2, 2, 3, c);
	px(7, 11, 2, 3, c);
	px(2, 7, 3, 2, c);
	px(11, 7, 3, 2, c);
	// Inner "bolt hole" hint - small accent square in the middle of the hub.
	px(7, 7, 2, 2, MP_BG_DARK);
}

// Envelope: outline + chevron flap pointing up from the center.
void PhoneIconTile::drawMail(){
	const lv_color_t c = MP_HIGHLIGHT;
	// Outline (top, bottom, left, right).
	px(1, 4, 14, 1, c);
	px(1, 12, 14, 1, c);
	px(1, 4, 1, 9, c);
	px(14, 4, 1, 9, c);
	// Flap chevron - rises from corners to a point near the top center.
	px(2, 5, 2, 1, c);
	px(4, 6, 2, 1, c);
	px(6, 7, 2, 1, c);
	px(8, 7, 2, 1, c);
	px(10, 6, 2, 1, c);
	px(12, 5, 2, 1, c);
}
