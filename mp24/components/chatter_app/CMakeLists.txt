# mp24/components/chatter_app/CMakeLists.txt
#
# First component to compile actual upstream phone-firmware code
# from src/. Starts tiny — just the Interface/ base layer plus our
# InputLVGL shim — so we can verify the build path works before
# growing the SRCS list one feature at a time.
#
# Design choices:
#
#   - We compile src/*.cpp at their absolute paths instead of adding
#     src/ to INCLUDE_DIRS. This matters because if src/ were on the
#     include path, the BASE-LAYER headers like LVObject.h would
#     leak into other components, and worse, any other Pins.hpp /
#     InputLVGL.h that lives under src/ would compete with our
#     circuitos_shim versions. The relative #includes inside src/
#     files (e.g. "../InputLVGL.h", "LVObject.h") work locally
#     without any -I tweaking because C/C++ relative includes
#     resolve from the source file's directory, not the build dir.
#
#   - src/ IS on PRIV_INCLUDE_DIRS so the shim file shim/InputLVGL.cpp
#     can `#include "InputLVGL.h"` to pull in the class declaration
#     we're implementing. PRIV keeps it scoped to this component.
#
#   - The circuitos_shim component comes FIRST in REQUIRES so any
#     '<Pins.hpp>' from inside src/ files finds our MP2.4 override
#     before any upstream Chatter copy. (chatter_library — which has
#     the upstream Pins.hpp at libraries/Chatter-Library/src/Pins.hpp
#     — is intentionally NOT in REQUIRES here. We don't need its
#     contents for the Interface/ base layer; if a later screen
#     port pulls something from Chatter-Library we add it then with
#     care about ordering.)
#
#   - SRCS deliberately exclude src/InputLVGL.cpp. The shim provides
#     the symbols InputLVGL::{InputLVGL, getInstance, getIndev}.
#     If we ever accidentally compile both, the linker catches the
#     duplicate definition immediately.

# Absolute path to repo-root src/. CMAKE_CURRENT_LIST_DIR is this
# CMakeLists's directory: mp24/components/chatter_app/. Go up three
# levels to repo root, then into src/.
set(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../src")

idf_component_register(
    SRCS
        # Interface/ base classes — every LVScreen subclass inherits
        # from these, so they're the first dependency to satisfy.
        "${SRC_DIR}/Interface/LVObject.cpp"
        "${SRC_DIR}/Interface/LVScreen.cpp"

        # S-MP17b: more Interface/ infrastructure. Each pulls
        # additional LVGL 8 + CircuitOS surface through the
        # compiler. ConvoView.cpp deliberately omitted — it pulls
        # Model/Storage/MessageService which we haven't shimmed
        # yet.
        "${SRC_DIR}/Interface/LVSelectable.cpp"
        "${SRC_DIR}/Interface/LVModal.cpp"
        "${SRC_DIR}/Interface/Pics.cpp"
        "${SRC_DIR}/Interface/PhoneTransitions.cpp"

        # S-MP17d: first Element/ widget. BatteryElement is the
        # leanest one — 24 LOC, depends only on LVObject (already
        # compiled), Battery singleton (MP24Battery shim), and
        # LoopListener (CircuitOS). Good runtime smoke test for
        # the LoopManager listener pattern: it registers itself
        # and ticks every LoopManager pump cycle, calling
        # Battery.getLevel() and updating an lv_img if changed.
        "${SRC_DIR}/Elements/BatteryElement.cpp"

        # S-MP18a: theme + fonts + foundational service + the
        # background widget every "real-looking" screen uses, plus
        # the first lean screen that consumes them.
        #
        # The whole stack is dependency-clean: MakerphoneTheme is
        # ~3500 LOC of pure data tables with no service or model
        # deps. The fonts are LVGL binary font data in .c files,
        # also zero deps. PhoneClock is a ~150 LOC RAM-only wall-
        # clock service (S54 backing for the Date & Time screen),
        # no SPIFFS / NVS / RTC. PhoneSynthwaveBg builds on those
        # three; PhoneWelcomeScreen builds on PhoneSynthwaveBg.
        #
        # No instantiation in TestScreen yet — this commit just
        # proves the compile path; runtime use of PhoneWelcomeScreen
        # lands when we wire it as a real boot destination.
        "${SRC_DIR}/MakerphoneTheme.cpp"
        "${SRC_DIR}/Fonts/pixelbasic7.c"
        "${SRC_DIR}/Fonts/pixelbasic16.c"
        "${SRC_DIR}/Services/PhoneClock.cpp"
        "${SRC_DIR}/Elements/PhoneSynthwaveBg.cpp"
        "${SRC_DIR}/Screens/PhoneWelcomeScreen.cpp"

        # S-MP18c: universal-UI Elements + the service one of them
        # depends on. PhoneStatusBar (top strip with time, battery,
        # signal indicators) and PhoneSoftKeyBar (bottom strip with
        # softkey labels) appear in 105 callers each — once these
        # compile most screens are unblocked.
        #
        # PhoneIdleDim is a service PhoneSoftKeyBar pulls for the
        # input-activity timestamp it uses to dim the soft keys
        # after N seconds of no input. Tiny (~260 LOC), zero new
        # external deps (just LoopListener + InputListener +
        # Settings, all of which are already in our build).
        "${SRC_DIR}/Services/PhoneIdleDim.cpp"
        "${SRC_DIR}/Elements/PhoneStatusBar.cpp"
        "${SRC_DIR}/Elements/PhoneSoftKeyBar.cpp"

        # Our replacement for src/InputLVGL.cpp. The upstream one is
        # intentionally NOT listed.
        "shim/InputLVGL.cpp"

        # Linker anchor — its single extern "C" symbol is what
        # mp24/main/app_main.cpp references to keep this whole
        # archive from being dropped by --gc-sections. See file
        # header for the static-archive linking explanation.
        "shim/Anchor.cpp"

        # S-MP17c: first real LVScreen subclass that runs on
        # hardware. Replaces the temporary three-button boot UI
        # in app_main. Proves the LVScreen → SCREEN_LOADED →
        # LoopManager defer → onStart pipeline works at runtime.
        "screens/TestScreen.cpp"

        # S-MP18b: factory wrapper that wires PhoneWelcomeScreen
        # into the boot path. Pre-populates Settings.ownerName so
        # the greeting label has content, then constructs +
        # starts the screen.
        "screens/WelcomeFactory.cpp"

    INCLUDE_DIRS
        # Nothing exported to other components yet. Screens will be
        # invoked from chatter_app's own entry point eventually.

    PRIV_INCLUDE_DIRS
        # So shim/InputLVGL.cpp can find src/InputLVGL.h.
        "${SRC_DIR}"

    REQUIRES
        circuitos_shim           # FIRST: <Pins.hpp> override
        espressif__arduino-esp32 # <Arduino.h>
        lvgl__lvgl               # <lvgl.h>
        circuitos                # <Loop/LoopManager.h>, <Input/...>
        log
)

# Soften a few warnings that are pervasive in the legacy code and
# would otherwise be promoted to errors by ESP-IDF's strict default.
# Matches the policy applied to the circuitos component in S-MP13b
# so behaviour is consistent across components compiling old code.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=reorder
    -Wno-error=maybe-uninitialized
    -Wno-error=return-type
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=sign-compare
    # Arduino.h gets pre-included so 'String', 'pinMode', 'millis'
    # etc. resolve without each src .cpp having to remember to
    # include it. Same trick used for the circuitos build.
    -include Arduino.h
)
