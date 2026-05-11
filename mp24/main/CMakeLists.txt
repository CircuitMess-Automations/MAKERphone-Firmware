# mp24/main — entry point component
#
# S-MP01..S-MP02 scope: USB CDC console, ST7735 display init + boot
# splash, I²C bus, AW9523B, LED blink. Display, input, audio, modem,
# etc. fill out across subsequent sessions.

idf_component_register(
    SRCS
        "app_main.c"
        "hal/display.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        log
        freertos
        esp_common
        circuitos_shim
)
