# mp24/components/circuitos — vendored CircuitOS (MIT, upstream
# github.com/CircuitMess/CircuitOS).
#
# The library is shipped as Arduino-style source files in src/.
# We glob them all here. Sources that are gated on feature flags
# we haven't set in Setup.hpp (CIRCUITOS_PIEZO_PWM, CIRCUITOS_NET,
# CIRCUITOS_MPU6050 etc.) compile to empty translation units because
# every .cpp top-level uses '#ifdef CIRCUITOS_X / #include
# "X.impl"' — so leaving them in the SRCS list is harmless.
#
# Setup.hpp at the component root is what each upstream source
# pulls in via "../../Setup.hpp" relative to src/<subdir>/<file>.cpp.
# It MUST sit at this exact level (one above src/).

file(GLOB_RECURSE CIRCUITOS_SRCS
    "src/*.cpp"
    "src/*.c"
)

# .impl files are NOT translation units — they're partial code
# pulled in via #include from a wrapping .cpp when a feature flag
# is set. Defensive filter in case glob accidentally pulled any.
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "\\.impl$")

# Exclude subsystems we don't use OR that pull dependencies we
# haven't wired up yet. Each grouping explained:
#
#   Display/, UI/, Elements/    Depend on TFT_eSPI which we don't
#                               vendor. The phone firmware drives
#                               the screen via LVGL anyway, with
#                               CircuitOS Display only used to
#                               grab the TFT_eSPI handle. We'll
#                               replace that call site with a
#                               direct LVGL→hal/display flush in
#                               S-MP16. Until then these stay out.
#
#   Support/                    Context, ContextTransition, Modal,
#                               ModalTransition all reference
#                               Screen/Sprite from UI/. Excluded
#                               for the same reason.
#
#   Devices/Matrix/             IS31FL3731 LED matrix — not on
#                               MP2.4 hardware. Matrix.cpp also
#                               fails -Werror=reorder under our
#                               strict-error toolchain.
#
#   Devices/Motion/             ICM20948 / MPU6050 IMU — not on
#                               MP2.4 hardware.
#
#   Devices/SerialFlash/        External SPI flash adapter — we
#                               use the in-package flash directly.
#
#   Devices/ShiftOutput.cpp     References an undeclared 'GPIO'
#                               symbol; designed for AVR. Not used
#                               by the phone firmware.
#
#   Network/                    Wi-Fi networking — replaced by
#                               cellular. CIRCUITOS_NET is unset
#                               but Network/Net.cpp file itself
#                               isn't fully guarded.
#
#   Motion/                     MPU fusion — depends on IMU.
#
#   FS/                         CompressedFile needs heatshrink
#                               (not vendored); PGMFile + RamFile
#                               use legacy Arduino-FS abstract
#                               classes that fail under our newer
#                               libstdc++ (construct_at on abstract
#                               types). Re-enable when porting
#                               screens that actually need these
#                               adapters (most use plain SPIFFS
#                               File from arduino-esp32 anyway).
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Display/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/UI/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Elements/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Support/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Devices/Matrix/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Devices/Motion/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Devices/SerialFlash/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Devices/ShiftOutput\\.cpp$")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Network/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/Motion/")
list(FILTER CIRCUITOS_SRCS EXCLUDE REGEX "/src/FS/")

idf_component_register(
    SRCS
        ${CIRCUITOS_SRCS}
    INCLUDE_DIRS
        "src"           # so '#include <CircuitOS.h>' resolves
        "."             # so '#include "../../Setup.hpp"' from inside
                        # src/<subdir>/<file>.cpp lands on our Setup.hpp
    REQUIRES
        espressif__arduino-esp32  # CircuitOS code is full of '#include <Arduino.h>'
        freertos
        nvs_flash                 # CIRCUITOS_NVS path uses NVS
)

# CircuitOS code emits a lot of -Wmissing-field-initializers and a
# couple of signed/unsigned warnings on stricter toolchains. We
# don't own this source — silence those locally to keep our own
# compile warnings audible.
target_compile_options(${COMPONENT_LIB} PRIVATE
    # Force-include Arduino.h on every translation unit. The upstream
    # was written for the Arduino IDE which implicitly does this for
    # every sketch + library source. Without it, types like 'uint',
    # 'byte', and 'String' are undeclared. We added arduino-esp32 as
    # a managed component in S-MP13a so the header is on the
    # include path; this just makes the implicit prepend.
    "SHELL:-include Arduino.h"
    # Suppress noisy warnings we don't own:
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-format
    # Downgrade errors that are legitimate but pervasive in upstream:
    -Wno-error=reorder              # member init order vs declaration order
    -Wno-error=maybe-uninitialized  # GCC-14 false-positives on some paths
    -Wno-error=uninitialized
    -Wno-error=return-type          # rare paths missing explicit return
)
