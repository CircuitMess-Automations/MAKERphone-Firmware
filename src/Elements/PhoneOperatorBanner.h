#ifndef MAKERPHONE_PHONEOPERATORBANNER_H
#define MAKERPHONE_PHONEOPERATORBANNER_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneOperatorBanner - S147
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
 *  - The 80 logo cells are drawn as 80 small lv_obj rectangles only
 *    when set (i.e. when the bit is 1); inactive cells are simply not
 *    created. A typical retro pixel logo fills well under half the
 *    cells, so the LVGL object count stays tiny -- a few dozen rather
 *    than 80 -- and rebuild() is cheap enough to fire on every push.
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
	lv_obj_t* logoHost   = nullptr;   // child container holding the 5x16 cells
	bool      visible    = false;     // mirror of !empty(text + logo)

	void buildLayout();
	void rebuildLogo();
	void rebuildText();
};

#endif // MAKERPHONE_PHONEOPERATORBANNER_H
