# components/circuitos_shim — placeholder
#
# Future home of the ESP-IDF-compatible reimplementation of CircuitOS
# headers used by the legacy Chatter firmware:
#   - Loop/LoopManager.h    polled-loop scheduler
#   - Display/Display.h     LovyanGFX-backed display wrapper
#   - Input/Input.h         button input base class + listener pattern
#   - Audio/Piezo.h         the tone()/noTone() audio API (Session 5 = PiezoI2S)
#   - Util/HWRevision.h     hardware revision detection
#   - <CircuitOS.h>         umbrella header
#
# Sessions 3-6 populate this component as each subsystem comes online.
# For now it is an empty component that satisfies `REQUIRES circuitos_shim`
# in the main component.

idf_component_register(
    SRCS
        "circuitos_shim_stub.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        log
)
