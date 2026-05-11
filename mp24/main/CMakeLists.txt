# mp24/main — entry point component
#
# S-MP01 scope: bring up USB CDC console, I²C bus, AW9523B,
# and blink the 8 on-board LEDs to prove the build pipeline.
# Display, input, audio, modem, etc. land in later sessions.

idf_component_register(
    SRCS
        "app_main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        log
        freertos
        esp_common
        circuitos_shim
)
