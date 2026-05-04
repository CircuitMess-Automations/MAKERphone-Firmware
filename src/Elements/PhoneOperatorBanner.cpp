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

	// Host container for the 16x5 logo cells. We never style the host
	// itself; cells are children with their own bg colour. Rebuilding
	// the bitmap is then as simple as lv_obj_clean(logoHost) followed
	// by re-adding cells for each set bit.
	logoHost = lv_obj_create(obj);
	lv_obj_remove_style_all(logoHost);
	lv_obj_clear_flag(logoHost, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(logoHost, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(logoHost, LogoWidth, LogoHeight);
	// Anchor the logo to the right edge of the banner, vertically
	// centred so the 5 px tall bitmap floats inside the 11 px tall
	// strip without crowding either edge.
	lv_obj_set_pos(logoHost,
				   BannerWidth - LogoWidth,
				   (BannerHeight - LogoHeight) / 2);
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
	if(logoHost == nullptr) return;

	// Wipe the previous bitmap. lv_obj_clean recursively frees every
	// child; the LVObject base destructor will not run for them
	// because we never wrapped the cells in LVObjects.
	lv_obj_clean(logoHost);

	const uint16_t* rows = Settings.get().operatorLogo;
	for(uint8_t r = 0; r < LogoRows; ++r) {
		uint16_t bits = rows[r];
		if(bits == 0) continue;
		for(uint8_t c = 0; c < LogoCols; ++c) {
			// Bit 15 = leftmost column so the bitmap reads naturally
			// when written out as binary literals.
			const uint16_t mask = (uint16_t) 0x8000u >> c;
			if((bits & mask) == 0) continue;

			lv_obj_t* cell = lv_obj_create(logoHost);
			lv_obj_remove_style_all(cell);
			lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_set_size(cell, LogoCellPx, LogoCellPx);
			lv_obj_set_pos(cell, c * LogoCellPx, r * LogoCellPx);
			lv_obj_set_style_radius(cell, 0, 0);
			lv_obj_set_style_bg_color(cell, MP_ACCENT, 0);
			lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
		}
	}
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
