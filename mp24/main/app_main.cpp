/*
 * mp24/main/app_main.cpp
 *
 * S-MP13a — entry point is now C++ so we can call into the
 * arduino-esp32 component (initArduino()) and, in S-MP13b+, into
 * the CircuitOS C++ classes that the existing phone firmware is
 * written against.
 *
 * The HAL still lives in C — its headers are wrapped in extern "C"
 * below so the function declarations don't get C++ name-mangled.
 *
 * Boot sequence:
 *   1. USB-Serial/JTAG banner.
 *   2. initArduino() — bootstraps the Arduino core (millis() clock,
 *      pinMode table, Serial object). Does NOT call Serial.begin();
 *      our log path is ESP_LOG over USB-Serial/JTAG.
 *   3. Display + boot screen (S-MP02 deliverable).
 *   4. I²C bus + AW9523B + XL9555s + interrupt-driven keypad
 *      input dispatcher (S-MP03 + S-MP04).
 *   5. A small local listener fires on every press / release —
 *      bumps a counter, captures the event for on-screen display,
 *      and logs via USB-Serial/JTAG.
 *   6. Forever loop: blink the LEDs at ~2.5 Hz and redraw the live
 *      dashboard at ~5 fps.
 */

#include <cstdio>
#include <cstring>
#include <atomic>

#include <Arduino.h>     // arduino-esp32 — for initArduino() and the
                         // Arduino-shaped types the rest of the phone
                         // firmware depends on. Pulls in <stdint.h>,
                         // <FreeRTOS.h>, esp_log etc. transitively.
#include <CircuitOS.h>   // umbrella; right now just brings in Arduino.h
                         // but real CircuitOS classes (LoopManager,
                         // Display, Util/Vector, Sync/Mutex) become
                         // reachable now that this links.

// S-MP13d smoke test: include the shim Display.h and force at least
// one symbol reference so the linker has to instantiate the class
// (and therefore prove the implementation compiles+links cleanly).
// `__attribute__((used))` keeps the function past --gc-sections.
// Once MP24Chatter (S-MP14d) actually instantiates a Display, this
// smoke test can be removed.
#include <Display/Display.h>
#include <Display/Sprite.h>
#include <Battery/BatteryService.h>
#include <Chatter.h>
#include <lvgl.h>

extern "C" {
#include "lvgl_glue.h"
}

__attribute__((used))
static void disp_smoketest_never_called()
{
    /* S-MP14d smoke test: drive the whole singleton chain from
     * Chatter.begin() outward — Display, MP24Input, Battery,
     * Settings — and confirm each method's symbol resolves at
     * link time. */
    Chatter.begin();
    (void)Chatter.getDisplay();
    (void)Chatter.getInput();
    Chatter.setBrightness(255);
    Chatter.fadeIn();
    Chatter.fadeOut();
    Chatter.backlightOff();
    (void)Chatter.backlightPowered();

    /* The Display + Battery direct probes earlier in the function
     * are now redundant — Chatter.begin() exercises them
     * transitively — but keeping them is cheap and traceable. */
    (void)Battery.getPercentage();
    (void)Battery.getVoltage();

    /* S-MP16b smoke test: prove the LVGL glue links + LVGL widgets
     * resolve. None of this runs at boot — once integration lands
     * in S-MP16c, app_main will call lvgl_glue_init() at the right
     * point in the boot sequence. */
    lvgl_glue_init();
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "MP2.4 LVGL");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lvgl_glue_run();
}

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_system.h"

#include "hal/display.h"
#include "hal/i2c_bus.h"
#include "hal/pins.h"
#include "hal/aw9523b.h"
#include "hal/xl9555.h"
#include "hal/buttons.h"
#include "hal/input_keypad.h"
#include "hal/piezo.h"
#include "hal/battery.h"
#include "hal/storage.h"
#include "hal/modem.h"
#include "hal/power.h"
#include "hal/sms.h"
#include "hal/calls.h"
#include "hal/audio_i2s2.h"
}

static const char *TAG = "MP24";

/* ----------------------------------------------------------------- */
/* Listener that captures every event for the on-screen dashboard.   */
/* ----------------------------------------------------------------- */

static std::atomic<uint32_t> s_press_count   {0};
static std::atomic<uint32_t> s_release_count {0};

/* Last-event snapshot. We pack [pressed:1 | btn_id:7] into a byte
 * with the top bit reserved as a "valid" flag (so bit 7 set = an
 * event has occurred; bit 6 = pressed). 0 = uninitialised. */
#define EVT_VALID   (1u << 7)
#define EVT_PRESSED (1u << 6)
static std::atomic<uint32_t> s_last_event_packed {0};

static void on_button_event(btn_id_t btn, bool pressed, void *ctx)
{
    (void)ctx;
    if (pressed) s_press_count.fetch_add(1);
    else         s_release_count.fetch_add(1);

    uint32_t packed = EVT_VALID | ((uint32_t)btn & 0x3F)
                                | (pressed ? EVT_PRESSED : 0);
    s_last_event_packed.store(packed);

    /* Audible feedback on press — different frequency per button
     * group, so a technician can tell at a glance whether the
     * keypad-to-audio pipeline is working end-to-end. Releases are
     * silent so we don't double-beep on a tap. */
    if (pressed) {
        uint32_t freq;
        if (btn <= BTN_HASH)                       freq = 880;   /* numpad */
        else if (btn <= BTN_FACE_D)                freq = 1320;  /* face A-D */
        else if (btn <= BTN_JOY_CLICK)             freq = 660;   /* joystick */
        else                                       freq = 440;   /* volume */
        piezo_tone(freq, 40);   /* ~40 ms click */
    }

    ESP_LOGI(TAG, "%s %s",
             pressed ? "PRESS  " : "release", btn_name(btn));
}

static input_listener_t s_listener = {
    .cb  = on_button_event,
    .ctx = NULL,
};

/* ----------------------------------------------------------------- */
/* USB CDC boot banner                                               */
/* ----------------------------------------------------------------- */

static void print_banner(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, " MAKERphone v2.4 firmware — S-MP08 modem");
    ESP_LOGI(TAG, " IDF=%s  chip=%s rev%d  cores=%d",
             esp_get_idf_version(),
             (info.model == CHIP_ESP32S3) ? "ESP32-S3" : "?",
             info.revision, info.cores);
    ESP_LOGI(TAG, " free heap = %lu B   PSRAM target = 2 MB octal",
             (unsigned long) esp_get_free_heap_size());
    ESP_LOGI(TAG, "==========================================");
}

/* ----------------------------------------------------------------- */
/* Dashboard — static layout drawn once, value strips redrawn live   */
/* ----------------------------------------------------------------- */

static uint16_t MP_BG     = 0;
static uint16_t MP_ACCENT = 0;
static uint16_t MP_TEXT   = 0;
static uint16_t MP_DIM    = 0;
static uint16_t MP_HILITE = 0;

static void draw_boot_screen(void)
{
    display_fill(MP_BG);

    display_fill_rect(0, 0, TFT_WIDTH, 14, MP_ACCENT);
    display_str(4, 4, "MAKERphone v2.4", MP_BG, MP_ACCENT);

    display_str(4, 22, "S-MP08+ pwr btn",          MP_TEXT, MP_BG);
    display_str(4, 34, "Modem + power monitor",    MP_DIM,  MP_BG);

    /* Chroma stripe stays — visual confidence in every boot. */
    const int bar_y = 50;
    const int bar_h = 6;
    const int bar_w = TFT_WIDTH / 4;
    display_fill_rect(0 * bar_w, bar_y, bar_w, bar_h, COLOR_RED);
    display_fill_rect(1 * bar_w, bar_y, bar_w, bar_h, COLOR_GREEN);
    display_fill_rect(2 * bar_w, bar_y, bar_w, bar_h, COLOR_BLUE);
    display_fill_rect(3 * bar_w, bar_y, bar_w, bar_h, COLOR_WHITE);

    /* Labels for live values — six 11-px rows. U5/U9 debug row
     * traded for power-button state now that the keypad path is
     * verified working. */
    display_str(4, 62,  "Last :",   MP_DIM, MP_BG);
    display_str(4, 73,  "Events:",  MP_DIM, MP_BG);
    display_str(4, 84,  "Tone :",   MP_DIM, MP_BG);
    display_str(4, 95,  "Batt :",   MP_DIM, MP_BG);
    display_str(4, 106, "Modem:",   MP_DIM, MP_BG);
    display_str(4, 117, "Pwr  :",   MP_DIM, MP_BG);
}

/* Erase a fixed strip first so changing-width values don't leave
 * artefacts from the previous frame. */
static void redraw_value(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    display_fill_rect(x, y, 110, 7, bg);
    display_str(x, y, s, fg, bg);
}

static void update_dashboard(void)
{
    char buf[48];

    /* "Last :" line */
    uint32_t evt = s_last_event_packed.load();
    if (evt & EVT_VALID) {
        btn_id_t btn  = (btn_id_t)(evt & 0x3F);
        bool   was_p  = (evt & EVT_PRESSED) != 0;
        snprintf(buf, sizeof(buf), "%s %s",
                 was_p ? "PRESS" : "REL  ", btn_name(btn));
        redraw_value(48, 62, buf, was_p ? MP_HILITE : MP_TEXT, MP_BG);
    }

    /* "Events: N / N" */
    uint32_t p = (uint32_t) s_press_count.load();
    uint32_t r = (uint32_t) s_release_count.load();
    snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)p, (unsigned long)r);
    redraw_value(48, 73, buf, MP_TEXT, MP_BG);

    /* "Tone : 880 Hz" */
    uint32_t freq = piezo_current_freq();
    if (freq > 0) snprintf(buf, sizeof(buf), "%lu Hz", (unsigned long)freq);
    else          snprintf(buf, sizeof(buf), "--");
    redraw_value(48, 84, buf, freq > 0 ? MP_HILITE : MP_DIM, MP_BG);

    /* "Batt : 3.85 V (74%)" */
    if (!battery_ready()) {
        redraw_value(48, 95, "...", MP_DIM, MP_BG);
    } else {
        float volts = battery_voltage();
        int   pct   = battery_percent();
        uint16_t batt_col = MP_TEXT;
        if      (pct <= 10) batt_col = COLOR_RED;
        else if (pct <= 25) batt_col = display_rgb(255, 180, 60);
        else                batt_col = display_rgb(120, 220, 120);
        snprintf(buf, sizeof(buf), "%.2f V (%d%%)", volts, pct);
        redraw_value(48, 95, buf, batt_col, MP_BG);
    }

    /* "Modem: STATE  <model>" */
    modem_state_t mst = modem_state();
    const char *model = modem_model();
    if (model && model[0]) {
        snprintf(buf, sizeof(buf), "%s %s", modem_state_name(mst), model);
    } else {
        snprintf(buf, sizeof(buf), "%s", modem_state_name(mst));
    }
    uint16_t mcol = MP_TEXT;
    switch (mst) {
        case MODEM_READY:        mcol = display_rgb(120, 220, 120); break;
        case MODEM_FAILED:       mcol = COLOR_RED;                  break;
        case MODEM_BOOTING:
        case MODEM_POWERING_ON:  mcol = display_rgb(255, 180, 60);  break;
        default:                 mcol = MP_DIM;                     break;
    }
    redraw_value(48, 106, buf, mcol, MP_BG);

    /* "Pwr  : idle (#N)" or "Pwr  : HELD 1.2s (#N)" with colour
     * shift on press so it's visually obvious. */
    bool pressed = power_button_pressed();
    uint32_t held = power_button_held_ms();
    uint32_t n    = power_button_press_count();
    if (pressed) {
        snprintf(buf, sizeof(buf), "HELD %lu.%lus (#%lu)",
                 (unsigned long)(held / 1000),
                 (unsigned long)((held % 1000) / 100),
                 (unsigned long)n);
        redraw_value(48, 117, buf, MP_HILITE, MP_BG);
    } else {
        snprintf(buf, sizeof(buf), "idle (#%lu)", (unsigned long)n);
        redraw_value(48, 117, buf, MP_DIM, MP_BG);
    }
}

/* ----------------------------------------------------------------- */

/* One-shot worker: waits up to 35 s for the modem to enter READY,
 * then runs sms_init() + calls_init() to configure text mode +
 * +CMTI routing + RING/CONNECT/NO_CARRIER call URCs. Keeps
 * app_main free of long blocking calls so the dashboard goes live
 * while the modem boots in parallel. */
static void sms_boot_task(void *arg)
{
    (void) arg;
    esp_err_t r = sms_init(35000);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "SMS layer not started (modem state: %s, err: %s)",
                 modem_state_name(modem_state()), esp_err_to_name(r));
    }
    /* calls_init's ready-wait will return immediately since the
     * modem is already READY at this point (sms_init succeeded) or
     * FAILED (in which case calls_init also short-circuits). */
    r = calls_init(5000);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "Call control not started (err: %s)",
                 esp_err_to_name(r));
    }

    /* I²S2 modem voice bus: AT+QDAI=4 then bring up our slave RX.
     * Idle until a call goes ACTIVE; calls.c flips audio_i2s2_start
     * and _stop in the pump task. */
    r = audio_i2s2_init(5000);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "I²S2 modem audio not started (err: %s)",
                 esp_err_to_name(r));
    }
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    print_banner();

    /* Bootstrap the Arduino layer BEFORE any HAL init so its global
     * constructors are run and millis() / pinMode() / Serial work in
     * any code we call subsequently. initArduino() does NOT call
     * Serial.begin() — we leave Serial silent and log via ESP_LOG over
     * USB-Serial/JTAG, which survives panics and never collides with
     * peripheral pin assignments. */
    initArduino();
    ESP_LOGI(TAG, "Arduino layer initialised");

    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed — continuing headless");
    } else {
        MP_BG     = display_rgb( 20,  12,  36);
        MP_ACCENT = display_rgb(255, 140,  30);
        MP_TEXT   = display_rgb(255, 220, 180);
        MP_DIM    = display_rgb(170, 140, 200);
        MP_HILITE = display_rgb(122, 232, 255);
        draw_boot_screen();
    }

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "I²C init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "bus probe: U5=%s U9=%s AW9523B=%s",
             i2c_bus_probe(I2C_ADDR_XL9555_U5) ? "OK" : "MISSING",
             i2c_bus_probe(I2C_ADDR_XL9555_U9) ? "OK" : "MISSING",
             i2c_bus_probe(I2C_ADDR_AW9523B)   ? "OK" : "MISSING");

    if (aw9523b_init() != ESP_OK) {
        ESP_LOGE(TAG, "AW9523B init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (xl9555_init(I2C_ADDR_XL9555_U5) != ESP_OK) {
        ESP_LOGW(TAG, "XL9555 U5 init failed (continuing)");
    }
    if (xl9555_init(I2C_ADDR_XL9555_U9) != ESP_OK) {
        ESP_LOGW(TAG, "XL9555 U9 init failed (continuing)");
    }

    /* Register the listener BEFORE input_keypad_init() so any boot-time
     * presses (technician sitting on the keypad while flashing, etc.)
     * still get dispatched cleanly. */
    input_keypad_add_listener(&s_listener);

    if (input_keypad_init() != ESP_OK) {
        ESP_LOGW(TAG, "Keypad input HAL not started (continuing)");
    }

    /* Audio: piezo_init brings up I²S TX + spawns synth task. SD_MODE
     * is already HIGH from aw9523b_init(). Non-fatal: a silent firmware
     * is still useful for keypad debug. */
    if (piezo_init() != ESP_OK) {
        ESP_LOGW(TAG, "Piezo init failed (continuing silently)");
    } else {
        /* 3-note boot chime — C5 → E5 → G5 ascending arpeggio. The
         * synth task starts producing samples while we sleep here, so
         * the chime plays alongside the rest of bring-up. */
        piezo_tone(523, 140);   /* C5 */
        vTaskDelay(pdMS_TO_TICKS(150));
        piezo_tone(659, 140);   /* E5 */
        vTaskDelay(pdMS_TO_TICKS(150));
        piezo_tone(784, 200);   /* G5 */
        vTaskDelay(pdMS_TO_TICKS(220));
    }

    /* Battery: takes a synchronous first sample before returning, so
     * the dashboard's "Batt :" row never shows zero. Spawns a 1 Hz
     * background sampler for ongoing updates. Non-fatal: a firmware
     * with no battery readout still demonstrates the rest of the
     * stack. */
    if (battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor init failed (continuing)");
    }

    /* Storage: mount /spiffs from the 1984 KB partition at 0x210000.
     * Enumerates root and reads the sentinel file. format_if_mount_
     * failed=true means a wiped chip auto-formats on first boot
     * (slow — adds ~10 s — but the firmware survives). Non-fatal:
     * the GSM/dashboard stack works fine without assets while we
     * iterate on the C++ shim. */
    if (storage_init() != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed (continuing without assets)");
    }

    /* GSM modem: configures UART1 (GPIO 17/18) + PWR_KEY (GPIO 12) +
     * RESET_N (GPIO 15) and spawns a background task that drives the
     * power-on sequence asynchronously. Returns in ms; modem boot
     * itself takes 8-30 s and runs in parallel with the dashboard
     * loop. State is observable via modem_state() and shown on the
     * dashboard. Non-fatal: a missing or unpowered modem just sits
     * in MODEM_FAILED, the rest of the firmware is unaffected. */
    if (modem_init() != ESP_OK) {
        ESP_LOGW(TAG, "Modem HAL init failed (continuing without GSM)");
    }

    /* Power button: read-only this session. GPIO2 (uBUTTON_PWR) gets
     * configured as input with pull-up; a 50 Hz background task
     * debounces and exposes pressed / held-ms / press-count. The
     * kill-power output on GPIO1 (uPOWER_OFF) is intentionally NOT
     * driven yet — its polarity is unverified, and driving it wrong
     * cuts our own supply mid-test. Phase 2 wires the actual
     * shutdown call once polarity is confirmed. */
    if (power_init() != ESP_OK) {
        ESP_LOGW(TAG, "Power button HAL init failed (continuing)");
    }

    /* SMS: needs the modem in READY state, which takes 8-30 s after
     * modem_init returns. Spawn a one-shot task that waits for
     * readiness then configures text mode + +CMTI URC routing.
     * Putting the wait in a task keeps app_main non-blocking. */
    xTaskCreate(sms_boot_task, "sms_boot", 4096, NULL,
                tskIDLE_PRIORITY + 1, NULL);

    ESP_LOGI(TAG, "Entering live dashboard loop.");
    bool led_on = false;
    TickType_t last_wake = xTaskGetTickCount();

    /* Idle-dim (Decision C): blank the display after IDLE_TIMEOUT_MS
     * of no key/power-button activity. The backlight is hardware-on
     * (no software dimming possible), so "dim" really means "draw
     * black over the dashboard". Any keypad or power-button press
     * wakes the display back to the live dashboard. */
    const TickType_t IDLE_TIMEOUT = pdMS_TO_TICKS(30 * 1000);   /* 30 s */
    TickType_t       last_activity = xTaskGetTickCount();
    uint32_t         last_keypad_n = (uint32_t) s_press_count.load();
    uint32_t         last_power_n  = power_button_press_count();
    bool             blanked       = false;

    for (;;) {
        led_on = !led_on;
        aw9523b_set_leds(led_on);

        /* Activity detection: any new press on either input bumps
         * the timer and wakes from blank. Reading the keypad press
         * count via the atomic that the listener already maintains
         * avoids hooking another callback for this. */
        uint32_t k = (uint32_t) s_press_count.load();
        uint32_t p = power_button_press_count();
        if (k != last_keypad_n || p != last_power_n) {
            last_keypad_n = k;
            last_power_n  = p;
            last_activity = xTaskGetTickCount();
            if (blanked) {
                ESP_LOGI(TAG, "Idle wake — redrawing dashboard");
                draw_boot_screen();
                blanked = false;
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (!blanked && (now - last_activity) > IDLE_TIMEOUT) {
            ESP_LOGI(TAG, "Idle %lu ms — blanking display",
                     (unsigned long) ((now - last_activity) * portTICK_PERIOD_MS));
            display_fill(0x0000);   /* black */
            blanked = true;
        }

        if (!blanked) {
            update_dashboard();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(200));   /* 5 fps */
    }
}
