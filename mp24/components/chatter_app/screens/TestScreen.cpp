/*
 * mp24/components/chatter_app/screens/TestScreen.cpp
 *
 * First LVScreen subclass that runs on hardware. This is the
 * milestone that proves the whole stack works end-to-end at
 * runtime, not just at compile time:
 *
 *   - LVObject ctor calls lv_obj_create(nullptr) → an LVGL screen
 *     gets created.
 *   - LVScreen ctor attaches an LV_EVENT_SCREEN_LOADED handler
 *     that defers onStart() via LoopManager.
 *   - TestScreen ctor builds the actual visible UI and explicitly
 *     adds the OK button to inputGroup so keypad nav works.
 *   - start(false) calls lv_scr_load(obj) → LVGL swaps active
 *     screen → SCREEN_LOADED fires → handler defers → next
 *     LoopManager tick picks it up → indev's group switches to
 *     inputGroup → onStart() runs.
 *
 * Visible on-panel proof points:
 *   1. The orange 'TestScreen' title at top — proves the screen
 *      loaded and is being drawn.
 *   2. Two-line subtitle in pink — proves text rendering and
 *      multi-line layout work on the new screen.
 *   3. A focused 'OK' button at the bottom with the LVGL focus
 *      ring around it — proves the deferred onStart fired AND
 *      the indev group switch succeeded. If the button shows
 *      but isn't highlighted, the deferred call didn't run
 *      (LoopManager task probably isn't pumping).
 *   4. Joystick UP/DOWN should keep the focus on the single
 *      button (only one item in the group) but pressing JOY_CLICK
 *      should produce the LVGL click-pulse animation. Lack of
 *      pulse means the indev isn't pointing at this group.
 *
 * The screen is leaked (never deleted) — fine for a smoke test.
 * Proper push/pop lifecycle waits for real screens with parent
 * relationships.
 */

#include "Interface/LVScreen.h"   /* via PRIV_INCLUDE_DIRS = src/ */
#include "Elements/BatteryElement.h"

#include <lvgl.h>

class TestScreen : public LVScreen {
public:
    TestScreen() : LVScreen()
    {
        /* Background — same synthwave palette as the boot UI so
         * the visual continuity is obvious. obj is the screen's
         * lv_obj_t, inherited from LVObject. */
        lv_obj_set_style_bg_color(obj,
                                  lv_color_hex(0x140C24),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);

        /* Title — non-focusable label. */
        lv_obj_t *title = lv_label_create(obj);
        lv_label_set_text(title, "TestScreen");
        lv_obj_set_style_text_color(title,
                                    lv_color_hex(0xFF8C1E),
                                    LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        /* S-MP17d: BatteryElement in the top-right. Even without
         * the SPIFFS asset pack present, the widget should
         * construct cleanly, register as a LoopListener, and tick
         * every LoopManager pump cycle. The lv_img inside it
         * tries to load 'S:/Battery/N.bin' from LVGL's filesystem
         * driver — we haven't wired the LVGL FS-driver yet so
         * those reads will fail silently; the visible widget will
         * be a 19x12 transparent rectangle. That's fine for now.
         * The compile-link path + LoopListener registration is
         * what we're proving. The visual asset path lands when
         * we wire LVGL's lv_fs_drv against /spiffs (probably
         * an S-MP07b follow-up). */
        BatteryElement *bat = new BatteryElement(obj);
        lv_obj_align(bat->getLvObj(), LV_ALIGN_TOP_RIGHT, -4, 4);

        /* Centred subtitle — also non-focusable. */
        lv_obj_t *info = lv_label_create(obj);
        lv_label_set_text(info, "real LVScreen\nat runtime");
        lv_obj_set_style_text_color(info,
                                    lv_color_hex(0xFFDCB4),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_align(info,
                                    LV_TEXT_ALIGN_CENTER,
                                    LV_PART_MAIN);
        lv_obj_align(info, LV_ALIGN_CENTER, 0, 0);

        /* One focusable widget. lv_btn is LVGL 8 spelling (LVGL 9
         * added the lv_button alias). We add it explicitly to
         * `inputGroup` — the LVScreen-owned group that the
         * deferred onStart will point the indev at when the
         * screen finishes loading. Without this lv_group_add_obj
         * call the button would default-add to lvgl_glue's group
         * instead, and after the indev switch it'd be orphaned. */
        lv_obj_t *btn = lv_btn_create(obj);
        lv_obj_set_size(btn, 100, 26);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -6);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "OK");
        lv_obj_center(lbl);

        lv_group_add_obj(inputGroup, btn);
    }
};

/* Factory exported to app_main. C linkage keeps the call site
 * simple. The screen is heap-allocated and intentionally never
 * freed — TestScreen has no parent and no destruction path. */
extern "C" void chatter_app_start_test_screen(void)
{
    auto *s = new TestScreen();
    /* animate=false: lv_scr_load synchronously. Avoids depending
     * on LoopManager::defer for the screen swap itself (it's
     * still used for the post-load onStart callback). */
    s->start(false);
}
