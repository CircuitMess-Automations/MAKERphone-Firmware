# mp24/components/chatter_library — ESP-IDF wrapper around the
# existing CircuitMess Chatter-Library at libraries/Chatter-Library.
#
# We don't copy the source here — it lives at the repo root for the
# legacy Arduino IDE build, and we just point at it. The repo layout:
#
#   <repo>/
#     libraries/Chatter-Library/src/
#       Chatter.cpp                  (excluded — TFT_eSPI dep)
#       ChatterDisplay.cpp           (excluded — TFT_eSPI dep)
#       Battery/BatteryService.cpp   (excluded — Util/HWRevision dep
#                                                + esp_adc_cal API)
#       Settings.cpp                 INCLUDED
#       Notes.h                      header-only constants
#       Pins.hpp                     IGNORED — MP2.4 has its own
#                                              Pins.hpp that wins via
#                                              include-path order
#     mp24/components/chatter_library/CMakeLists.txt  ← this file
#
# Net effect after this commit: #include <Notes.h>, #include
# <Settings.h>, and #include "Battery/BatteryService.h" all resolve.
# Chatter.h still can't be included (it pulls Display.h → TFT_eSPI),
# but the application code that needs it will go through our shim's
# MP24Chatter equivalent.

set(CHATTER_LIB_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/Chatter-Library/src")

# Files we DO compile:
set(CHATTER_LIB_SRCS
    "${CHATTER_LIB_SRC_DIR}/Settings.cpp"
)

# We deliberately do NOT include:
#   - Chatter.cpp / ChatterDisplay.cpp:  pull Display.h → TFT_eSPI
#   - Battery/BatteryService.cpp:        uses HWRevision::get() and
#                                        esp_adc_cal_raw_to_voltage()
#                                        (v4 API). MP24Battery will
#                                        provide the actual impl.

idf_component_register(
    SRCS
        ${CHATTER_LIB_SRCS}
    INCLUDE_DIRS
        "${CHATTER_LIB_SRC_DIR}"  # so '#include <Notes.h>' etc. work
    REQUIRES
        espressif__arduino-esp32  # Arduino.h types
        circuitos                 # Util/Settings, Loop/LoopManager
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    # Same Arduino prepend pattern as CircuitOS — Chatter-Library
    # was an Arduino IDE library, so the bare 'String' / 'uint' /
    # 'byte' types need Arduino.h pulled in implicitly.
    "SHELL:-include Arduino.h"
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-format
    -Wno-error=reorder
    -Wno-error=maybe-uninitialized
    -Wno-error=uninitialized
    -Wno-error=return-type
)
