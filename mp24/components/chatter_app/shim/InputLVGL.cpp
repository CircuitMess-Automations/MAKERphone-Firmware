/*
 * mp24/components/chatter_app/shim/InputLVGL.cpp
 *
 * Replacement for src/InputLVGL.cpp. The upstream version constructs
 * its OWN lv_indev_drv_t in the InputLVGL constructor and calls
 * lv_indev_drv_register — which would create a second indev fighting
 * our lvgl_glue keypad device. We exclude the upstream .cpp from
 * the chatter_app build (see CMakeLists.txt SRCS list) and define
 * the same symbols here.
 *
 * The header (src/InputLVGL.h) is reached via the file's relative
 * "../InputLVGL.h" includes from src/Interface/. We keep the same
 * public ABI:
 *
 *   InputLVGL::InputLVGL(lv_indev_type_t)   constructor — does
 *                                            NOT register a new
 *                                            indev. Just stashes
 *                                            our pre-built indev.
 *   InputLVGL::getInstance() -> InputLVGL*  lazy-creates the
 *                                            singleton.
 *   InputLVGL::getIndev() -> lv_indev_t*    returns the lvgl_glue
 *                                            keypad indev.
 *   read()                                   pure virtual — we
 *                                            provide a concrete
 *                                            MP24InputLVGL subclass
 *                                            with a no-op read,
 *                                            since the actual read
 *                                            callback lives in
 *                                            lvgl_glue.c.
 *
 * Why we need this shim at all:
 *   - LVScreen.cpp calls InputLVGL::getInstance()->getIndev() to
 *     get the LVGL indev handle so it can call lv_indev_set_group
 *     when a screen loads. Without an InputLVGL implementation
 *     present, the link step fails.
 *   - We can't just delete the call sites in LVScreen — that
 *     would require patching upstream code, which we explicitly
 *     don't want to do (Decision in S-MP16-lvgl8: keep src/
 *     untouched, write shims around it).
 */

/* Declared in mp24/main/lvgl_glue.h. We don't pull that header in
 * directly to avoid dragging mp24/main into chatter_app's include
 * path — a one-line extern declaration is enough and keeps the
 * coupling visible. */
struct _lv_indev_t;
typedef struct _lv_indev_t lv_indev_t;
extern "C" lv_indev_t *lvgl_glue_get_indev(void);

#include "InputLVGL.h"   /* src/InputLVGL.h via PRIV_INCLUDE_DIRS */

/* Concrete subclass: makes the abstract InputLVGL instantiable.
 * read() is unused — the real keypad callback is the one in
 * lvgl_glue.c (lvgl_keypad_read_cb). LVGL only ever calls read()
 * on the indev that registered it; our lvgl_glue indev was
 * registered with its OWN read_cb so the path here never fires.
 */
class MP24InputLVGL : public InputLVGL {
public:
    MP24InputLVGL() : InputLVGL(LV_INDEV_TYPE_KEYPAD) {}
    void read(lv_indev_drv_t * /*drv*/, lv_indev_data_t * /*data*/) override
    {
        /* intentionally empty — see file header */
    }
};

/* Static singleton storage. Lives in this TU because we're providing
 * the InputLVGL class definition; if upstream's .cpp ever got
 * mistakenly compiled alongside this one, the linker would catch
 * the duplicate. */
InputLVGL *InputLVGL::instance = nullptr;

/* Base-class constructor. The `type` argument is ignored — every
 * use site of InputLVGL on MP2.4 wants the keypad device that
 * lvgl_glue already created, regardless of what they pass. */
InputLVGL::InputLVGL(lv_indev_type_t /*type*/)
{
    instance    = this;
    inputDevice = lvgl_glue_get_indev();
}

InputLVGL *InputLVGL::getInstance()
{
    if (instance == nullptr) {
        /* Lazy construction. The MP24InputLVGL ctor calls the base
         * InputLVGL ctor, which sets `instance = this` — so by the
         * time `new` returns we already have the singleton wired up.
         * The assignment below is redundant but explicit. */
        instance = new MP24InputLVGL();
    }
    return instance;
}

lv_indev_t *InputLVGL::getIndev()
{
    return inputDevice;
}
