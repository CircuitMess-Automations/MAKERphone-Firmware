/*
 * mp24/components/circuitos_shim/include/Pins.hpp
 *
 * MP2.4 override of the upstream Chatter-Library Pins.hpp.
 *
 * The upstream file (libraries/Chatter-Library/src/Pins.hpp)
 * encodes raw ESP32 GPIO numbers for the Chatter board's
 * shift-register button matrix and direct-wired backlight /
 * piezo. None of those numbers apply on MP2.4:
 *
 *   - Buttons go through an XL9555 I²C expander, not GPIO; the
 *     btn_id_t enum in hal/buttons.h is what the rest of the
 *     MP2.4 firmware speaks. The BTN_* symbols here resolve to
 *     those enum values so any legacy call site that does
 *     setBtnPressCallback(BTN_LEFT, ...) lands on the right
 *     virtual button.
 *
 *   - Backlight is hardware-on via Q1 G3035; no PIN_BL.
 *
 *   - Piezo audio is via I²S into the MAX98357A amp; no PIN_BUZZ
 *     in the GPIO sense.
 *
 *   - LoRa is replaced by the Quectel EG912U-GL cellular modem
 *     (Decision B). The LORA_* / RADIO_* symbols stay defined as
 *     -1 so legacy LoRaService code compiles — runtime behaviour
 *     is replaced by MP24LoRaService → SMS adapter in S-MP15.
 *
 *   - Battery is on ADC1_CH2 = GPIO3, divider 2.0, eFuse-cal'd in
 *     hal/battery. The BATTERY_PIN constant is informational only;
 *     legacy code that calls analogRead(BATTERY_PIN) is replaced
 *     by Battery.getVoltage() via the MP24Battery shim.
 *
 *   - CALIB_EN / CALIB_READ (TL431 reference) aren't wired on
 *     v2.4 — the eFuse cal in hal/battery covers absolute
 *     accuracy without it. Symbols stay as -1.
 *
 * include-search ordering: anything that does <Pins.hpp> from a
 * component that REQUIRES circuitos_shim before chatter_library
 * gets THIS file. Files inside libraries/Chatter-Library/src/
 * itself use #include "Pins.hpp" (relative) and resolve to the
 * upstream version next to them — fine, since we exclude every
 * .cpp from chatter_library that uses Pins.hpp anyway (Chatter.cpp,
 * ChatterDisplay.cpp, Battery/BatteryService.cpp — see S-MP13c).
 *
 * The 'CHATTER_LIBRARY_PINS_H' include guard intentionally matches
 * upstream so if both copies somehow both reach the preprocessor
 * (would indicate a misconfigured include order), the second pass
 * is a no-op rather than a redefinition cascade.
 */
#ifndef CHATTER_LIBRARY_PINS_H
#define CHATTER_LIBRARY_PINS_H

/* The MP2.4 button enum lives here. Including it pulls in BTN_0..9,
 * BTN_STAR/HASH, BTN_FACE_A..D, BTN_JOY_*, BTN_VOL_UP/DOWN, plus
 * the layered macros (BTN_LEFT → BTN_JOY_LEFT etc.) the legacy
 * Chatter app expects. We don't need to re-define any of those
 * macros here — they're already declared in hal/buttons.h. */
#include "hal/buttons.h"

/* ------- physical GPIO numbers, MP2.4 ESP32-S3 ------- */

/* Backlight is hardware-on (Decision C). PIN_BL stays defined so
 * legacy code that does pinMode(PIN_BL, OUTPUT) / digitalWrite
 * compiles, but those calls target an unused / -1 pin and have no
 * visible effect. */
#define PIN_BL          (-1)

/* Piezo is the I²S → MAX98357A path on MP2.4. There's no GPIO that
 * legacy code can ledcWriteTone against. PIN_BUZZ stays defined as
 * -1 for compile compat. */
#define PIN_BUZZ        (-1)

/* Battery ADC. GPIO3 = ADC1_CH2 on the ESP32-S3, via 2:1 divider.
 * Legacy code's analogRead(BATTERY_PIN) won't give a usable mV
 * value without the eFuse curve-fit cal, so we route through the
 * Battery singleton (MP24Battery on hal/battery) instead. */
#define BATTERY_PIN     3

/* TL431 reference calibration. Not populated on MP2.4 v2.4 — the
 * eFuse curve-fit cal in hal/battery covers absolute accuracy. */
#define CALIB_EN        (-1)
#define CALIB_READ      (-1)

/* ------- LoRa / RadioLib pins ------- */

/* MP2.4 has a Quectel EG912U-GL cellular modem on UART1 instead of
 * a LoRa radio (Decision B). Every LORA_* / RADIO_* symbol stays
 * defined as -1 so legacy LoRaService.cpp + the RadioLib include
 * chain still parse — the runtime LoRaService is replaced by an
 * MP24LoRaService → SMS adapter in S-MP15. Any analogRead /
 * digitalWrite / spi.begin call on these pins is a no-op at the
 * GPIO layer. */
#define LORA_SS         (-1)
#define LORA_RST        (-1)
#define LORA_DIO0       (-1)

#define RADIO_SCK       (-1)
#define RADIO_MISO      (-1)
#define RADIO_MOSI      (-1)
#define RADIO_CS        (-1)
#define RADIO_BUSY      (-1)
#define RADIO_DIO1      (-1)
#define RADIO_RST       (-1)

/* ------- legacy shift-register input pins ------- */

/* Chatter put 16 buttons through a 74HC165 shift register on these
 * three GPIOs. MP2.4 uses two XL9555 I²C expanders + AW9523B
 * instead — see hal/i2c_bus + hal/input_keypad. Symbols stay
 * defined as -1 so any InputShift constructor call site compiles;
 * the actual input path goes through MP24Input. */
#define INPUT_DATA      (-1)
#define INPUT_CLOCK     (-1)
#define INPUT_LOAD      (-1)

#endif /* CHATTER_LIBRARY_PINS_H */
