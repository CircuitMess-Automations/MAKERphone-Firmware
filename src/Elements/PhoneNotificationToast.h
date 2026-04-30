#ifndef MAKERPHONE_PHONENOTIFICATIONTOAST_H
#define MAKERPHONE_PHONENOTIFICATIONTOAST_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneNotificationToast
 *
 * Reusable retro feature-phone slide-down notification toast for
 * MAKERphone 2.0. Whenever the firmware needs to surface a transient
 * event - an incoming SMS, a missed call, "battery low", "paired with
 * peer" - the host screen builds a toast, hands it a variant + title +
 * one-line message, and calls `show()`. The widget then:
 *
 *      - slides itself down from above the screen (y = -ToastHeight) to
 *        rest just below the status bar (y = StatusBarHeight),
 *      - holds for the requested duration,
 *      - and slides back up out of view, deleting nothing - the same
 *        instance can be re-used for the next event with another
 *        `show()` call.
 *
 *      +----------------------------------------+
 *      |  [#]  New message                      |   <- title in MP_TEXT
 *      |       Hey, you up?                     |   <- preview in MP_LABEL_DIM
 *      +----------------------------------------+
 *      ^                                        ^
 *      MP_DIM bg + MP_ACCENT bottom rule         154 px wide, 28 px tall
 *
 * Implementation notes:
 *  - 100 % code-only - one rounded `lv_obj` slab, a 14x14 icon badge
 *    composed of plain `lv_obj` rectangles, and two `lv_label`s. No
 *    SPIFFS assets, no canvas backing buffer. Sits in the same family
 *    as `PhoneStatusBar`, `PhoneSoftKeyBar`, `PhoneChatBubble`.
 *  - The toast lifetime is owned by its `lv_obj` parent (typically
 *    the active `LVScreen`'s root object). The destructor cancels both
 *    pending lv_timers and any in-flight slide animation, so a screen
 *    tear-down mid-show never leaves a stale callback pointing into
 *    freed memory.
 *  - The slide is a plain `lv_anim_t` on the y coordinate; the
 *    "hold" + "slide-out" steps are driven by two one-shot
 *    `lv_timer_t`s (dismiss + hide) so we never depend on the
 *    `lv_anim_set_user_data` API which is not present in every
 *    LVGL 8.x build. Two timers cost two malloc'd structs each
 *    `show()` call, which is fine on the Chatter's heap.
 *  - Variants drive the icon glyph and badge tint. Generic, Call, SMS
 *    and Battery cover every Phase D / E / K caller; new variants are
 *    cheap to add (one `case` in `redrawIcon()`).
 *  - Palette is the shared MAKERphone palette the rest of the Phone*
 *    widgets use: `MP_DIM` slab background, `MP_ACCENT` bottom rule,
 *    `MP_TEXT` warm cream title, `MP_LABEL_DIM` muted cream preview,
 *    `MP_HIGHLIGHT` cyan / `MP_ACCENT` orange variant tints. Constants
 *    are duplicated rather than centralised at this small scale - if
 *    the palette ever moves out of the widgets, every widget moves
 *    together.
 *  - The widget is constructed hidden and offscreen. `isVisible()`
 *    flips on the first frame of slide-in and stays true until the
 *    slide-out animation finishes, so callers can guard against
 *    overlapping `show()` calls (the second one cancels the first
 *    and replaces its content in place).
 */
class PhoneNotificationToast : public LVObject {
public:
	enum class Variant : uint8_t {
		Generic,   // generic info dot - cyan badge tint
		Call,      // incoming / missed call - orange badge tint
		SMS,       // new SMS - cyan badge tint, envelope glyph
		Battery,   // battery low / charging - orange badge tint
	};

	/**
	 * Build a toast inside `parent`. The toast starts hidden above the
	 * top of the 160x128 display; nothing is visible until the first
	 * `show()` call.
	 */
	PhoneNotificationToast(lv_obj_t* parent);
	virtual ~PhoneNotificationToast();

	/**
	 * Slide the toast in with the given content, hold for `durationMs`,
	 * then slide it back out. Calling `show()` while a previous toast
	 * is still on-screen replaces its content in place and resets the
	 * dismiss countdown.
	 *
	 * @param variant   Icon glyph + badge tint to use.
	 * @param title     Bold one-liner (auto-truncates with ellipsis).
	 * @param message   Optional second-line preview. nullptr or "" hides it.
	 * @param durationMs How long to hold before sliding out. Defaults to
	 *                   `DefaultDurationMs`. Use 0 to keep visible until
	 *                   `hide()` is called manually.
	 */
	void show(Variant variant, const char* title,
			  const char* message = "", uint32_t durationMs = DefaultDurationMs);

	/** Slide out immediately. No-op if already hidden. */
	void hide();

	bool isVisible() const { return visible; }

	// ----- layout tunables -----

	// 160 px display - toast leaves a 3 px gutter on either side so it
	// reads as a "card" rather than a full-width banner.
	static constexpr uint16_t ToastWidth        = 154;
	static constexpr uint16_t ToastHeight       = 28;
	// Height of the existing PhoneStatusBar - the toast docks just
	// below it when fully shown. Hard-coded rather than #include-ing
	// PhoneStatusBar.h to keep the dependency graph flat.
	static constexpr uint16_t StatusBarHeight   = 11;
	static constexpr uint16_t ScreenWidth       = 160;
	static constexpr uint16_t ScreenHeight      = 128;
	static constexpr uint16_t ToastX            = 3;

	// Auto-dismiss + animation tunables. 2.5 s feels right for a
	// passive notification on a tiny screen - long enough to read a
	// short preview, short enough to not feel intrusive.
	static constexpr uint32_t DefaultDurationMs = 2500;
	static constexpr uint32_t SlideMs           = 220;

private:
	lv_obj_t*   iconBox        = nullptr;  // 14x14 badge background
	lv_obj_t*   titleLabel     = nullptr;
	lv_obj_t*   msgLabel       = nullptr;
	lv_timer_t* dismissTimer   = nullptr;  // armed at show(), fires "hold" -> slide-out
	lv_timer_t* hideTimer      = nullptr;  // armed after slide-out kicks off, flips HIDDEN

	Variant currentVariant = Variant::Generic;
	bool    visible        = false;        // true between slide-in start and slide-out done

	void buildSlab();
	void buildIcon();
	void buildLabels();
	void redrawIcon(Variant v);
	void clearIconGlyph();
	lv_obj_t* px(int16_t x, int16_t y, uint16_t w, uint16_t h, lv_color_t color);

	void slideTo(int16_t targetY);
	void cancelTimers();

	static void slideExec(void* var, int32_t v);
	static void onDismissTimer(lv_timer_t* timer);
	static void onHideDone(lv_timer_t* timer);
};

#endif //MAKERPHONE_PHONENOTIFICATIONTOAST_H
