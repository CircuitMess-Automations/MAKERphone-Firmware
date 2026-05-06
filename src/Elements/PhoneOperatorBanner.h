#ifndef MAKERPHONE_PHONEOPERATORBANNER_H
#define MAKERPHONE_PHONEOPERATORBANNER_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneOperatorBanner - S147 (S204 logo cache)
 *
 * Reusable retro feature-phone "operator banner" widget intended to sit
 * just below PhoneStatusBar on PhoneHomeScreen (and any future screen
 * that wants to advertise the carrier identity). It is the visual
 * partner of the S01 status bar and gives the homescreen the
 * unmistakable Sony-Ericsson / Nokia silhouette:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |  MAKERphone                  *.*.* *  | <- PhoneOperatorBanner
 *   |             12:34                      | <- PhoneClockFace
 *   |             ...                        |
 *
 * The banner has two halves:
 *
 *   - Left half  : up to 15-char operator-name label rendered in
 *                  pixelbasic7 cream, dot-truncated at the divider.
 *                  Sourced from Settings.operatorText, refreshed by
 *                  refresh() on every show.
 *
 *   - Right half : 16x5 user-pixelable bitmap (the literal "5x16
 *                  user-pixelable logo" from the S147 spec) drawn at
 *                  one device-pixel per cell so the bitmap fits in
 *                  exactly the cell budget the user authored. Sourced
 *                  from Settings.operatorLogo (five uint16_t rows).
 *                  Bit 15 of each row is the leftmost column, bit 0
 *                  is the rightmost -- so the bitmap reads naturally
 *                  when authored as binary literals.
 *
 * Implementation notes:
 *  - Code-only (zero SPIFFS asset cost) like every other Phone* widget.
 *  - Anchors with LV_OBJ_FLAG_IGNORE_LAYOUT so it cooperates with
 *    parents that already use flex/grid layouts -- same pattern as
 *    PhoneStatusBar.
 *  - **S204 logo cache.** The pre-S204 implementation rebuilt up to
 *    80 individual lv_obj rectangles every time `refresh()` ran (i.e.
 *    every screen push). The cells were thrown away and re-created on
 *    the next push, churning the LVGL object tree for a bitmap that
 *    in practice never changes between edits. S204 replaces that with
 *    a single `lv_canvas` whose 16x5 backing buffer (240 bytes,
 *    LV_IMG_CF_TRUE_COLOR_ALPHA) is rasterised once on edit and
 *    reused on every subsequent push. The LVGL object count for the
 *    banner drops from "1 host + up to 80 cells" to "1 host + 1
 *    canvas" without changing the rendered output by a single pixel.
 *  - The backing buffer is owned by the banner instance (an inline
 *    member, not heap-allocated) so its lifetime exactly matches the
 *    canvas object and there is no malloc/free churn on screen push.
 *  - When BOTH halves are empty (text == "" AND every logo row == 0)
 *    the widget hides itself, so a freshly-wiped banner does not eat
 *    layout space on the homescreen. PhoneHomeScreen consults
 *    isVisible() after refresh() to decide whether to shift the
 *    clock face down by BannerHeight or keep the legacy y-offset.
 *  - The banner reads Settings.get() lazily so an edit made inside
 *    PhoneOperatorScreen surfaces on the very next homescreen push
 *    without any cross-screen wiring.
 */
class PhoneOperatorBanner : public LVObject {
public:
	PhoneOperatorBanner(lv_obj_t* parent);
	virtual ~PhoneOperatorBanner();

	/** Re-read Settings.operatorText / operatorLogo and repaint the
	 *  banner. Cheap; safe to call on every screen push. */
	void refresh();

	/** True iff at least one of (text, logo) is non-empty. The
	 *  homescreen consults this after refresh() to decide whether
	 *  to shift the clock face down by BannerHeight. */
	bool isVisible() const { return visible; }

	// ----- geometry -----------------------------------------------------
	//
	// 156 wide x 11 tall, centred under the 10 px status bar so a 1 px
	// breathing strip separates the banner from the bar's orange
	// separator. Text occupies the left ~118 px (with 4 px padding on
	// each side); the 16x5 logo lives in the right 16 px. The 16 px
	// gap between the two regions is intentional -- it gives the
	// banner the relaxed retro carrier-banner feel rather than a
	// crowded stat-line.

	static constexpr lv_coord_t BannerWidth   = 156;
	static constexpr lv_coord_t BannerHeight  = 11;
	static constexpr lv_coord_t BannerX       = 2;
	static constexpr lv_coord_t BannerY       = 11;

	static constexpr lv_coord_t LogoCols      = 16;
	static constexpr lv_coord_t LogoRows      = 5;
	static constexpr lv_coord_t LogoCellPx    = 1;
	static constexpr lv_coord_t LogoWidth     = LogoCols * LogoCellPx; // 16
	static constexpr lv_coord_t LogoHeight    = LogoRows * LogoCellPx; // 5

private:
	lv_obj_t* textLabel  = nullptr;   // operator name
	lv_obj_t* logoCanvas = nullptr;   // S204: single canvas replaces the
	                                  // up-to-80 lv_obj cells the v2.0
	                                  // banner rebuilt on every refresh.
	bool      visible    = false;     // mirror of !empty(text + logo)

	// S204 backing buffer for `logoCanvas`.
	// LV_IMG_CF_TRUE_COLOR_ALPHA -> sizeof(lv_color_t)+1 bytes per pixel.
	// 16 x 5 x 3 = 240 bytes for an RGB565 build. Inline so the buffer's
	// lifetime matches the banner instance with no heap allocation.
	uint8_t   logoBuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(LogoCols, LogoRows)] = {0};

	void buildLayout();
	void rebuildLogo();
	void rebuildText();
};

#endif // MAKERPHONE_PHONEOPERATORBANNER_H
