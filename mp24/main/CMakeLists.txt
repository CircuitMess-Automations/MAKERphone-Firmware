# mp24/main — entry point component
#
# S-MP01..S-MP08 scope: USB-Serial/JTAG console, ST7735 display, I²C
# HAL with AW9523B + XL9555 drivers, interrupt-driven keypad input
# dispatcher, I²S MAX98357A amp + square-wave piezo synthesiser,
# ADC1_CH2 battery monitor with Li-Po SOC estimate, SPIFFS storage
# HAL mounted at /spiffs from a 1984 KB partition, Quectel EG912U-GL
# modem layer 1 (UART + AT framework + URC dispatch + power-on
# state machine). SMS (S-MP09) and voice calls (S-MP10) build on
# the modem HAL in subsequent sessions.

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
        "hal/storage.c"
        "hal/modem.c"
        "hal/power.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_driver_i2s
        esp_driver_uart
        esp_driver_gpio
        esp_adc
        spiffs
        log
        freertos
        esp_common
        circuitos_shim
)

# Pack everything in mp24/spiffs/ into the `spiffs` partition image at
# build time. FLASH_IN_PROJECT makes the image part of the standard
# flash payload (i.e. our CI flash step writes it alongside the app),
# so `idf.py build` produces a single coherent set of artefacts.
spiffs_create_partition_image(spiffs ../spiffs FLASH_IN_PROJECT)
