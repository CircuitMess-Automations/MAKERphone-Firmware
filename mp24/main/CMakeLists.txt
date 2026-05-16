# mp24/main — entry point component
#
# S-MP01..S-MP06 scope: USB-Serial/JTAG console, ST7735 display, I²C
# HAL with AW9523B + XL9555 drivers, interrupt-driven keypad input
# dispatcher, I²S MAX98357A amp + square-wave piezo synthesiser,
# ADC1_CH2 battery monitor with Li-Po SOC estimate. SPIFFS (S-MP07)
# and GSM (S-MP08+) fill out across subsequent sessions.

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
        "hal/battery.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_driver_i2s
        esp_adc
        log
        freertos
        esp_common
        circuitos_shim
)
