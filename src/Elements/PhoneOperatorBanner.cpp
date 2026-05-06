#include "PhoneOperatorBanner.h"

#include <Settings.h>
#include <stdint.h>
#include <string.h>

#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the operator banner slots in between the status bar and the clock face
// without a visual seam. Inlined per the established pattern (the same way
// PhoneStatusBar / PhoneClockFace / PhoneSoftKeyBar / PhoneSynthwaveBg
// inline their per-widget palette without pulling in a shared header).
#define MP_BG_DARK     lv_color_make( 20,  12,  36)  // deep purple (transparent here)
#define MP_ACCENT      lv_color_make(255, 140,  30)  // sunset orange (logo cells)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (unused, kept for parity)
#define MP_DIM         lv_color_make( 70,  56, 100)  // muted purple (unused)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream (text)

PhoneOperatorBanner::PhoneOperatorBanner(lv_obj_t* parent) : LVObject(parent) {
	// Anchor independently of the parent's layout (PhoneHomeScreen uses
	// an empty container so flex/grid is not strictly relevant, but we
	// keep IGNORE_LAYOUT for parity with PhoneStatusBar / PhoneClockFace).
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, BannerWidth, BannerHeight);
	lv_obj_set_pos(obj, BannerX, BannerY);

	// Transparent background -- the banner overlays whatever wallpaper /
	// content the host screen has. The host can paint a synthwave gradient
	// or a plain swatch behind us without any changes here.
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

	buildLayout();
	refresh();
}

PhoneOperatorBanner::~PhoneOperatorBanner() = default;

// ----- layout -------------------------------------------------------------

void PhoneOperatorBanner::buildLayout() {
	// Operator-name label on the left. pixelbasic7 cream so it reads on the
	// same baseline as the digits inside the status bar above. Dot-truncate
	// at the start of the logo strip so a long carrier name never crowds
	// the bitmap.
	textLabel = lv_label_create(obj);
	lv_obj_add_flag(textLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(textLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(textLabel, MP_TEXT, 0);
	lv_label_set_long_mode(textLabel, LV_LABEL_LONG_DOT);
	// Reserve 16 px for the logo cells on the right plus a 4 px gap.
	const lv_coord_t textWidth = BannerWidth - LogoWidth - 4;
	lv_obj_set_width(textLabel, textWidth);
	lv_obj_set_pos(textLabel, 0, 2);
	lv_label_set_text(textLabel, "");

	// S204 -- single canvas replaces the up-to-80 lv_obj cells the
	// pre-S204 banner rebuilt on every refresh. The canvas owns a
	// 16x5 LV_IMG_CF_TRUE_COLOR_ALPHA buffer (240 bytes) backed by
	// the inline `logoBuf` member; the buffer outlives the canvas
	// object only while the banner instance is alive, which is the
	// exact lifetime LVGL needs.
	logoCanvas = lv_canvas_create(obj);
	lv_obj_remove_style_all(logoCanvas);
	lv_obj_clear_flag(logoCanvas, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(logoCanvas, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_canvas_set_buffer(logoCanvas, logoBuf,
	                     LogoCols, LogoRows,
	                     LV_IMG_CF_TRUE_COLOR_ALPHA);
	// Anchor the logo to the right edge of the banner, vertically
	// centred so the 5 px tall bitmap floats inside the 11 px tall
	// strip without crowding either edge.
	lv_obj_set_pos(logoCanvas,
	               BannerWidth - LogoWidth,
	               (BannerHeight - LogoHeight) / 2);

	// Start fully transparent; rebuildLogo() paints the lit cells on
	// the next refresh().
	lv_canvas_fill_bg(logoCanvas, MP_ACCENT, LV_OPA_TRANSP);
}

// ----- repainters ---------------------------------------------------------

void PhoneOperatorBanner::rebuildText() {
	if(textLabel == nullptr) return;

	// Defensive copy so a corrupt blob with no nul terminator cannot
	// overrun the buffer when handed to lv_label. Same pattern the
	// LockScreen ownerName path uses.
	const char* src = Settings.get().operatorText;
	char safe[16];
	const size_t cap = sizeof(safe) - 1;
	size_t n = 0;
	while(n < cap && src[n] != '\0') ++n;
	memcpy(safe, src, n);
	safe[n] = '\0';

	lv_label_set_text(textLabel, safe);
}

void PhoneOperatorBanner::rebuildLogo() {
	if(logoCanvas == nullptr) return;

	// S204 -- repaint into the existing canvas buffer instead of
	// destroying-and-rebuilding the per-cell lv_obj tree. Step 1:
	// flood-fill the buffer to a fully transparent accent so any
	// previously-lit cells reset to "off" without leaving stale
	// orange pixels behind.
	lv_canvas_fill_bg(logoCanvas, MP_ACCENT, LV_OPA_TRANSP);

	// Step 2: walk the user's bitmap and stamp each lit cell as a
	// 1x1 opaque accent rectangle. lv_canvas_draw_rect is the
	// LVGL 8.x portable API for "set this pixel to color+opacity"
	// across both the older set_px() and the newer
	// set_px_color/set_px_opa split, which keeps the build clean
	// regardless of which 8.x point release the Arduino package
	// pulls in. ~80 draw calls in the worst case, fired only when
	// refresh() runs (on screen push or on operator-edit), not per
	// frame -- so the extra abstraction cost is invisible.
	lv_draw_rect_dsc_t cellDsc;
	lv_draw_rect_dsc_init(&cellDsc);
	cellDsc.bg_color    = MP_ACCENT;
	cellDsc.bg_opa      = LV_OPA_COVER;
	cellDsc.border_width = 0;
	cellDsc.radius      = 0;

	const uint16_t* rows = Settings.get().operatorLogo;
	for(uint8_t r = 0; r < LogoRows; ++r) {
		uint16_t bits = rows[r];
		if(bits == 0) continue;
		for(uint8_t c = 0; c < LogoCols; ++c) {
			// Bit 15 = leftmost column so the bitmap reads naturally
			// when written out as binary literals.
			const uint16_t mask = (uint16_t) 0x8000u >> c;
			if((bits & mask) == 0) continue;
			lv_canvas_draw_rect(logoCanvas,
			                    c, r,
			                    LogoCellPx, LogoCellPx,
			                    &cellDsc);
		}
	}

	// lv_canvas_draw_rect already invalidates the canvas internally
	// (LVGL flags the canvas's image source dirty on every draw_*
	// call) so no explicit lv_obj_invalidate() is required here.
}

void PhoneOperatorBanner::refresh() {
	rebuildText();
	rebuildLogo();

	// Detect "everything empty" so the host can shift the clock face
	// up by BannerHeight when there is nothing to show.
	const char* src = Settings.get().operatorText;
	bool textEmpty = (src == nullptr) || (src[0] == '\0');

	bool logoEmpty = true;
	const uint16_t* rows = Settings.get().operatorLogo;
	for(uint8_t r = 0; r < LogoRows; ++r) {
		if(rows[r] != 0) { logoEmpty = false; break; }
	}

	visible = !(textEmpty && logoEmpty);
	if(visible) {
		lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	}
}
