# mp24/main — entry point component
#
# S-MP01..S-MP10 scope: USB-Serial/JTAG console, ST7735 display, I²C
# HAL with AW9523B + XL9555 drivers, interrupt-driven keypad input
# dispatcher, I²S MAX98357A amp + square-wave piezo synthesiser,
# ADC1_CH2 battery monitor with Li-Po SOC estimate, SPIFFS storage
# HAL mounted at /spiffs from a 1984 KB partition, Quectel EG912U-GL
# modem layer 1 (UART + AT framework + URC dispatch + power-on state
# machine), power button + idle-dim, text-mode SMS send/receive
# (+CMGS / +CMTI), voice-call control plane (ATD / ATA / ATH +
# RING / CLIP / NO CARRIER state machine). The I²S2 PCM audio
# bridge for in-call audio routing lands in a follow-up session
# once we have hardware to tune it against.
#
# S-MP13a — entry point is now C++ (.cpp) so the arduino-esp32
# component's initArduino() can be called and so the CircuitOS
# classes (S-MP13b+) link in. The HAL stays in C; its headers are
# wrapped in extern "C" at every C++ inclusion site.

idf_component_register(
    SRCS
        "app_main.cpp"
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
        "hal/sms.c"
        "hal/calls.c"
        "hal/audio_i2s2.c"
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
        circuitos
        chatter_library
        # arduino-esp32 is pulled in by idf_component.yml as
        # "espressif/arduino-esp32"; when the IDF Component Manager
        # installs it, the directory becomes
        # managed_components/espressif__arduino-esp32/ and the
        # CMake component name uses the same '<namespace>__<name>'
        # double-underscore form.
        espressif__arduino-esp32
)

# Pack everything in mp24/spiffs/ into the `spiffs` partition image at
# build time. FLASH_IN_PROJECT makes the image part of the standard
# flash payload (i.e. our CI flash step writes it alongside the app),
# so `idf.py build` produces a single coherent set of artefacts.
spiffs_create_partition_image(spiffs ../spiffs FLASH_IN_PROJECT)
