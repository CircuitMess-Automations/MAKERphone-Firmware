# components/circuitos_shim — MP2.4 board adapters layered over
# vendored CircuitOS + Chatter-Library.
#
# Goal: present the same Display / Input / Battery / Chatter
# singletons that the existing 506-file phone firmware expects,
# but back them with our MP2.4 hardware via the hal/* C drivers
# instead of the original Chatter shift-register + PWM-backlight
# stack.
#
# Class roster (filled in over S-MP14+):
#
#   MP24Input      ← Input (S-MP14a, here)
#   MP24Battery    ← BatteryService (S-MP14b)
#   MP24Pins.hpp   ← override of Pins.hpp (S-MP14c)
#   MP24Chatter    ← ChatterImpl bootstrap (S-MP14d)
#   MP24Piezo      ← Audio/Piezo (S-MP14e or later)
#
# Each one keeps the upstream interface unchanged so the screens /
# services / models above don't need editing.

idf_component_register(
    SRCS
        # Legacy placeholder from S-MP01 — keeps the
        # circuitos_shim_version() symbol around for now. Can be
        # removed once at least one real class is the entry point.
        "circuitos_shim_stub.c"
        # S-MP14a
        "MP24Input.cpp"
    INCLUDE_DIRS
        "include"
        # The hal/ headers live under main/ for the legacy
        # mp24/main component. Until we promote hal/ to its own
        # component, the shim reaches them via a relative path.
        "../../main"
    REQUIRES
        log
        circuitos                  # base Input / Vector / LoopListener
        espressif__arduino-esp32   # Arduino types via -include Arduino.h
)

# Same -include Arduino.h trick as the CircuitOS component — our
# shim sources transitively pull CircuitOS headers that assume the
# Arduino prelude is in scope (uint, byte, String, etc.).
target_compile_options(${COMPONENT_LIB} PRIVATE
    "SHELL:-include Arduino.h"
    -Wno-missing-field-initializers
)
