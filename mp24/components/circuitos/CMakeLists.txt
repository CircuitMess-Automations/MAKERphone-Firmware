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
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-format
)
