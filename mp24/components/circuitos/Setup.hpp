#ifndef MP24_CIRCUITOS_SETUP_HPP
#define MP24_CIRCUITOS_SETUP_HPP

/*
 * MAKERphone v2.4 — CircuitOS configuration flags.
 *
 * The upstream CircuitOS expects to find a Setup.hpp at a specific
 * location (each consuming project provides its own). Each source
 * file in CircuitOS includes it via "../../Setup.hpp" relative to
 * src/<subdir>/<file>.h. In our vendored layout that path lands here
 * at mp24/components/circuitos/Setup.hpp.
 *
 * --- What's enabled ---
 */

/* FreeRTOS is present (we're on ESP-IDF). Enables Task, Mutex,
 * BinarySemaphore, Semaphore, Queue across the library. */
#define CIRCUITOS_FREERTOS

#ifdef CIRCUITOS_FREERTOS
#define CIRCUITOS_TASK
#define CIRCUITOS_MUTEX
#define CIRCUITOS_BINARY_SEMAPHORE
#define CIRCUITOS_SEMAPHORE
#define CIRCUITOS_QUEUE
#endif

/* NVS is the right backing for Settings on ESP32 — way faster and
 * less wear than the LittleFS variant. */
#define CIRCUITOS_NVS

/*
 * --- What's deliberately disabled ---
 *
 * LovyanGFX: CircuitOS Display can be backed by LovyanGFX or
 * TFT_eSPI. We're going to back it differently — via the MP24Display
 * subclass that talks directly to hal/display. Leaving this off
 * means Display.h pulls the TFT_eSPI flavour for its default
 * implementation; we override before it matters.
 */
// #define CIRCUITOS_LOVYANGFX

/*
 * Piezo: CircuitOS Piezo only works in DAC or PWM mode. MP2.4 has
 * neither — sound comes from the MAX98357A I²S amp. The Chatter app
 * code uses Piezo.tone() in a few places; we'll provide our own
 * Piezo singleton via the shim that routes to hal/piezo's I²S synth.
 */
// #define CIRCUITOS_PIEZO_DAC
// #define CIRCUITOS_PIEZO_PWM

/*
 * Tone API (arduino-esp32 tone()/noTone() globals): not used; we
 * route through the shim. Leaving disabled prevents Audio/Piezo.cpp
 * from pulling in the Arduino tone library transitively.
 */
// #define CIRCUITOS_TONE

/*
 * LittleFS: we use SPIFFS (the partition was already set up in
 * S-MP07). The CircuitOS Settings class supports both via runtime
 * choice; CIRCUITOS_NVS is plenty for our needs.
 */
// #define CIRCUITOS_LITTLEFS

/*
 * Device drivers — none of these chips are on MP2.4:
 *   - ICM20948 (IMU)
 *   - MPU6050 (IMU)
 *   - IS31FL3731 LED matrix
 *   - W25Qxx serial flash via the dedicated SerialFlash driver
 */
// #define CIRCUITOS_ICM20948
// #define CIRCUITOS_MPU6050
// #define CIRCUITOS_LEDMATRIX
// #define CIRCUITOS_SERIALFLASH

/*
 * Network / WiFi: the Chatter firmware has no Wi-Fi code path. The
 * cellular modem replaces it. Leaving disabled keeps Net.cpp,
 * StreamableHTTPClient.cpp etc. out of the build.
 */
// #define CIRCUITOS_NET

/*
 * U8g2 fonts: not used by the phone firmware (it uses LVGL fonts).
 * Saves ~30 KB of PROGMEM.
 */
// #define CIRCUITOS_U8G2FONTS

/*
 * Low RAM mode: ESP32-S3FH4R2 has 512 KB on-chip SRAM + 2 MB PSRAM.
 * We're nowhere near RAM-constrained for context transitions, so
 * leave this off — keeps the smoother dual-buffered transitions
 * that the screens were designed against.
 */
// #define CIRCUITOS_LOWRAM

#endif // MP24_CIRCUITOS_SETUP_HPP
