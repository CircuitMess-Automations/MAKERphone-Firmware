/*
 * mp24/components/chatter_app/shim/FSLVGLStub.cpp
 *
 * S-MP20/6b -- pre-emptively defines the FSLVGL static-method
 * symbols that Games/GameEngine/Game.cpp's loop() pop path
 * references:
 *
 *     FSLVGL::loadCache();
 *
 * Once any game subclass (Snake / Bonk / SpaceInvaders /
 * SpaceRocks) lands in chatter_app SRCS in S-MP20/7, Snake's
 * vtable pulls Game's vtable, which pulls Game::loop() (declared
 * `final` in Game, overrides LoopListener::loop), which calls
 * FSLVGL::loadCache() inside the pop branch. Without this stub
 * the link would fail with an unresolved external.
 *
 * Why upstream src/FSLVGL.cpp can NOT be compiled directly:
 *
 *   - FSLVGL.cpp #includes <SPIFFS.h> AND <FS/RamFile.h>. The
 *     FS/RamFile.h dependency is the showstopper -- it was
 *     already determined in S-MP18i that RamFile inherits from
 *     fs::FileImpl in arduino-esp32 3.3.8, but the upstream
 *     RamFile does not override all of FileImpl's pure
 *     virtuals. std::make_shared<RamFile> fails with `invalid
 *     new-expression of abstract class type`. The same problem
 *     blocked the Storage layer (now stubbed by
 *     shim/StorageStub.cpp).
 *
 *   - FSLVGL.cpp registers an lv_fs_drv with LVGL for the 'S'
 *     drive letter -- our SPIFFS is already mounted at /spiffs
 *     by the C HAL layer, and LVGL would either need a separate
 *     drive letter or to coexist. Not yet wired up. Real
 *     integration is a separate session once the GamesScreen
 *     resource-loading flow comes online.
 *
 * What this stub provides:
 *
 *   FSLVGL::cacheLoaded      static bool, initialised false.
 *   FSLVGL::cache            static unordered_map, empty.
 *   FSLVGL::specialCache     static fs::File*, nullptr.
 *   FSLVGL::loadCache()      no-op.
 *   FSLVGL::unloadCache()    no-op companion.
 *
 * What this stub does NOT provide:
 *
 *   - FSLVGL::cached[] -- the static const char* array of
 *     22 SPIFFS paths to cache. Not referenced from any
 *     in-SRCS TU (verified by grep -- only src/FSLVGL.cpp
 *     references it, and that file is excluded). Leaving the
 *     definition unresolved is safe; if a future SRCS addition
 *     references it, that fire defines it.
 *   - FSLVGL non-static methods (ctor, lv_fs callbacks, fs
 *     accessor, loadSpecialCache, unloadSpecialCache). No
 *     in-SRCS code constructs an FSLVGL instance or calls these
 *     methods. Same logic -- if a future addition needs them,
 *     that fire provides bodies.
 *
 * Build-time effect: this TU is currently DEAD (no in-SRCS file
 * references the symbols it provides -- Game.cpp's loop() body
 * is gc-sections'd away because no game subclass is in SRCS).
 * --gc-sections drops the TU. Once S-MP20/7 lands the first
 * game subclass, the symbols here become live and resolve the
 * Game::loop() reference to FSLVGL::loadCache(). Behavioural
 * effect (when live): the LVGL image cache is not pre-warmed
 * after a game pops, so the host screen's lv_img widgets re-
 * read from SPIFFS on next display. Acceptable for bench
 * testing; real cache management lands when FSLVGL.cpp is
 * landed for real in a later session.
 */

#include "FSLVGL.h"

/* Static-member storage. The declarations live in FSLVGL.h; the
 * linker needs definitions in exactly one TU. The four members
 * below cover everything the header declares as `static`. */
bool                                 FSLVGL::cacheLoaded  = false;
std::unordered_map<std::string, fs::File *> FSLVGL::cache;
fs::File                            *FSLVGL::specialCache = nullptr;

void FSLVGL::loadCache()
{
    /* No-op. See file header. */
}

void FSLVGL::unloadCache()
{
    /* No-op companion to loadCache(). */
}
