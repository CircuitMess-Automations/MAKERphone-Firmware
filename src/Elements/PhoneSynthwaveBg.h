#ifndef MAKERPHONE_PHONESYNTHWAVEBG_H
#define MAKERPHONE_PHONESYNTHWAVEBG_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneSynthwaveBg
 *
 * Reusable retro-feature-phone wallpaper (160x128) for MAKERphone 2.0. It is
 * the first Phase-2 element after PhoneStatusBar, PhoneSoftKeyBar and
 * PhoneClockFace, and is intended to sit at the bottom of any screen that
 * wants the trademark synthwave look:
 *
 *      .                .                  .          <- twinkling stars
 *           ___                                          (purple -> magenta
 *          (   )                                          -> orange sky)
 *         (_____)                                       <- sun (with three
 *      ====== H O R I Z O N ======                          dark scan lines)
 *       \    \   |   /    /                            <- vanishing-point
 *        \    \  |  /    /                                perspective rays
 *      ----------+----------                            <- ground horizontals
 *      ------------+----------
 *
 * Implementation notes:
 *  - 100% code-only - no SPIFFS assets, no canvas backing buffers - so it
 *    adds zero data partition cost and only a handful of LVGL objects.
 *  - The sun is a circular lv_obj clipped to the sky band so only the top
 *    half is visible (overlap_clip via parent bounds, no manual masking
 *    needed in LVGL 8.x).
 *  - Sky and ground use LVGL's native bg_grad_color stops, which the
 *    framework renders as a vertical gradient on flush - a single object
 *    each, no multi-stripe overdraw.
 *  - Perspective rays are lv_line widgets fanning out from a vanishing
 *    point at the center of the horizon. Each line stores its endpoints
 *    in this object's instance memory because lv_line_set_points keeps
 *    a pointer rather than copying the array.
 *  - Anchored with LV_OBJ_FLAG_IGNORE_LAYOUT (same pattern as the other
 *    phone widgets) so it cooperates with parents that already use a
 *    flex/grid layout. Constructed first on the host screen so it sits
 *    behind every other widget.
 *  - Twinkle stars are static for now; animation is a deliberate Phase-2.5
 *    follow-up so this initial drop stays small and easy to compile-test.
 */
class PhoneSynthwaveBg : public LVObject {
public:
	PhoneSynthwaveBg(lv_obj_t* parent);
	virtual ~PhoneSynthwaveBg() = default;

	static constexpr uint16_t BgWidth   = 160;
	static constexpr uint16_t BgHeight  = 128;
	static constexpr uint16_t HorizonY  = 72;        // pixel row of the horizon
	static constexpr uint8_t  RayCount  = 7;         // perspective rays
	static constexpr uint8_t  HLineCount = 4;        // horizontal grid lines
	static constexpr uint8_t  StarCount  = 7;        // twinkle stars in the sky
	static constexpr uint8_t  ScanCount  = 3;        // dark scan lines across the sun

private:
	lv_obj_t* sky;          // upper half: sky gradient
	lv_obj_t* ground;       // lower half: ground gradient
	lv_obj_t* sun;          // circle clipped to sky bottom

	lv_obj_t* scanLines[ScanCount];
	lv_obj_t* hGridLines[HLineCount];
	lv_obj_t* rays[RayCount];
	lv_obj_t* stars[StarCount];

	// lv_line keeps a *pointer* to the points array instead of copying, so
	// the storage has to outlive the widget. Two points per ray.
	lv_point_t rayPoints[RayCount][2];

	void buildSky();
	void buildSun();
	void buildGround();
	void buildRays();
	void buildHorizontals();
	void buildStars();
};

#endif //MAKERPHONE_PHONESYNTHWAVEBG_H
