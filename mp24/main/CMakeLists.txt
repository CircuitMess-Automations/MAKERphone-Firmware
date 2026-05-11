# mp24/main — entry point component
#
# S-MP01..S-MP04 scope: USB CDC console, ST7735 display, I²C HAL
# with AW9523B + XL9555 drivers, interrupt-driven keypad input
# dispatcher. Audio (S-MP05), battery (S-MP06), GSM (S-MP08+) fill
# out across subsequent sessions.

idf_component_register(
    SRCS
        "app_main.c"
        "hal/display.c"
        "hal/i2c_bus.c"
        "hal/aw9523b.c"
        "hal/xl9555.c"
        "hal/input_keypad.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        log
        freertos
        esp_common
        circuitos_shim
)
