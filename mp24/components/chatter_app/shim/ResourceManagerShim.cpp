/*
 * mp24/components/chatter_app/shim/ResourceManagerShim.cpp
 *
 * S-MP20/12a: drop-in implementation of ResourceManager's four
 * undefined symbols (ctor / dtor / load / getResource) without
 * pulling FS/RamFile.h or FS/CompressedFile.h.
 *
 * Why this exists:
 *   - mp24/components/circuitos/CMakeLists.txt excludes
 *     src/FS/ wholesale because CompressedFile needs heatshrink
 *     (not vendored) and RamFile/PGMFile inherit from
 *     arduino-esp32's FileImpl in a way that fails
 *     construct_at<AbstractType> under our newer libstdc++.
 *
 *   - Upstream src/Games/GameEngine/ResourceManager.cpp #includes
 *     both <FS/CompressedFile.h> and <FS/RamFile.h>, so we cannot
 *     simply add the upstream .cpp to SRCS -- those headers are not
 *     on any include path the compiler can see.
 *
 *   - Game.cpp (already in chatter_app SRCS) keeps a
 *     `ResourceManager resMan;` member, so the link of any
 *     PhoneGamesScreen that instantiates Snake/Bonk/SpaceInvaders/
 *     SpaceRocks failed at S-MP20/11 with four undefined references
 *     into ResourceManager.
 *
 * What this does:
 *   - Same public API surface as upstream ResourceManager.h.
 *   - load() always opens the SPIFFS file directly. The inRam +
 *     compParams paths are deliberately skipped -- every consumer
 *     (Game::resMan.getResource(path)) calls seek(0) + read()
 *     just the same on a plain SPIFFS File as on a RamFile,
 *     because RamFile inherits from FileImpl. The cost is a
 *     SPIFFS read per access instead of a memcpy from PSRAM,
 *     which is acceptable for first-pass functional games on a
 *     phone (we have 2 MB PSRAM headroom but no urgency to use
 *     it for resource caching yet).
 *   - getResource() returns the stored File by value (matches
 *     upstream signature). seek(0) is called before return so the
 *     caller always starts reading at byte 0.
 *
 * Symbol parity:
 *   This file MUST define exactly the four mangled symbols Game.cpp
 *   references:
 *     ResourceManager::ResourceManager(const char*)
 *     ResourceManager::~ResourceManager()            [virtual]
 *     ResourceManager::load(const std::vector<ResDescriptor>&)
 *     ResourceManager::getResource(std::string)
 *
 *   Because the header declares the dtor `virtual`, this TU also
 *   emits the vtable for ResourceManager (one-definition rule
 *   says the vtable lives in the TU that defines the key function,
 *   i.e. the first non-inline virtual). Game.cpp's resMan member
 *   doesn't take a pointer/reference to the base, so it would
 *   compile without the vtable, but having it here means any
 *   future subclass would resolve correctly too.
 *
 * Future work:
 *   - If we ever want the inRam optimization back, shim RamFile
 *     (and optionally heatshrink + CompressedFile) under
 *     shim_includes/FS/ -- that's a separate fire (S-MP25-ish,
 *     not blocking).
 *   - SPIFFS open errors become an ESP_LOGE and the descriptor
 *     is silently skipped, matching upstream behavior.
 */

#include "Games/GameEngine/ResourceManager.h"

#include <SPIFFS.h>
#include "esp_log.h"

#include <string>
#include <vector>

static const char *TAG_RM = "ResMan";

ResourceManager::ResourceManager(const char* root) : root(root) {}

ResourceManager::~ResourceManager() {
    for (auto pair : resources) {
        pair.second.close();
    }
    resources.clear();
}

void ResourceManager::load(const std::vector<ResDescriptor>& descriptors) {
    resources.reserve(descriptors.size());

    for (auto descriptor : descriptors) {
        std::string path;

        if (!descriptor.path.empty() && descriptor.path[0] == 'c') {
            /* "c/..." -> shared common resources under
             * /Games/Common/<rest>. Mirrors upstream substr(1)
             * + "/Games/Common" prefix concatenation. */
            descriptor.path = descriptor.path.substr(1);
            path = std::string("/Games/Common") + descriptor.path;
        } else {
            /* Otherwise prefix with the per-game root passed to
             * the ResourceManager ctor. Empty root (the Snake /
             * Bonk / SpaceInvaders / SpaceRocks case) leaves the
             * descriptor path unchanged. */
            path = std::string(root ? root : "") + descriptor.path;
        }

        File original = SPIFFS.open(path.c_str());
        if (!original) {
            ESP_LOGE(TAG_RM, "Failed to load resource %s", path.c_str());
            continue;
        }

        /* Always use the SPIFFS File directly. The inRam +
         * compParams paths from upstream are intentionally
         * skipped -- see file header for rationale. */
        resources[descriptor.path] = original;
    }
}

File ResourceManager::getResource(std::string path) {
    auto it = resources.find(path);
    if (it == resources.end()) return File{};
    it->second.seek(0);
    return it->second;
}
