# mp24/main — entry point component
#
# S-MP01..S-MP05 scope: USB CDC console, ST7735 display, I²C HAL
# with AW9523B + XL9555 drivers, interrupt-driven keypad input
# dispatcher, I²S MAX98357A amp + square-wave piezo synthesiser.
# Battery (S-MP06), SPIFFS (S-MP07), GSM (S-MP08+) fill out across
# subsequent sessions.

idf_component_register(
    SRCS
        "app_main.c"
        "hal/display.c"
        "hal/i2c_bus.c"
        "hal/aw9523b.c"
        "hal/xl9555.c"
        "hal/input_keypad.c"
        "hal/audio_i2s.c"
        "hal/piezo.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_driver_i2s
        log
        freertos
        esp_common
        circuitos_shim
)
