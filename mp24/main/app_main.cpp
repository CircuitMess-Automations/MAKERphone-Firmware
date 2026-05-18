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
#include <Pins.hpp>
#include <lvgl.h>

extern "C" {
#include "lvgl_glue.h"
#include "nvs_flash.h"
}

/* S-MP14c compile-time assertion: prove the Pins.hpp that resolves
 * here is the MP2.4 shim, not the upstream Chatter copy. If the
 * include-search order ever regresses and we accidentally pick up
 * the wrong file, BTN_LEFT would equal 4 (Chatter's shift-register
 * bit position) instead of BTN_JOY_LEFT's enum value. The static
 * assertions below fail the build at that point. */
static_assert(BTN_LEFT == BTN_JOY_LEFT,
              "Pins.hpp override regressed — BTN_LEFT must equal "
              "BTN_JOY_LEFT on MP2.4, not a raw GPIO number");
static_assert(BTN_BACK == BTN_FACE_C,
              "Pins.hpp override regressed — BTN_BACK must equal "
              "BTN_FACE_C on MP2.4");
static_assert(LORA_SS == -1,
              "Pins.hpp override regressed — LORA_SS must be -1 on "
              "MP2.4 (cellular modem replaces LoRa)");
static_assert(BATTERY_PIN == 3,
              "Pins.hpp override regressed — BATTERY_PIN must be "
              "GPIO3 (ADC1_CH2) on MP2.4");

/* S-MP17a anchor: force the linker to pull in chatter_app's static
 * library archive so that LVObject.o + LVScreen.o + our InputLVGL
 * shim end up in the final binary. Without an external symbol
 * reference, ESP-IDF wraps chatter_app in --gc-sections / ar-style
 * archive linking and drops every .o that no one references —
 * meaning we'd happily build a 'green' firmware that never actually
 * contained the code we want to test.
 *
 * The pattern: a __attribute__((used)) function in main (kept past
 * compiler GC) that calls into a chatter_app extern symbol. The
 * symbol resolution itself loads the relevant .o from the .a, and
 * that .o's own references chain forward to LVScreen + LVObject.
 *
 * The anchor function is never executed at runtime. Its job is
 * purely to add an unresolved external reference at compile time
 * so the linker fills it. */
extern "C" void chatter_app_force_link(void);

__attribute__((used))
static void chatter_app_link_anchor()
{
    chatter_app_force_link();
}

/* S-MP15a: LoopManager pump task. CircuitOS's LoopManager is a
 * pure-static singleton (a class, not an object — all state in
 * static members) and exposes `static void loop()` that must be
 * called from somewhere. In Arduino setups this is called from
 * the main loop(); under ESP-IDF we run a dedicated FreeRTOS task.
 *
 * Header pulled via CircuitOS' module path. LoopManager.h includes
 * <Arduino.h> + <unordered_set>, both already in this TU's tree. */
#include <Loop/LoopManager.h>

static void loop_manager_task(void * /*arg*/)
{
    /* Initialise the elapsed-time tracker. The very first call to
     * loop() after resetTime() reports delta_us == 0; subsequent
     * calls report micros-since-previous-loop. */
    LoopManager::resetTime();

    for (;;) {
        LoopManager::loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
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
#include "hal/clock_source.h"
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

/* S-MP17c: the status-label timer that used to live here was
 * removed when TestScreen replaced the three-button boot UI.
 * That UI built s_lvgl_btn_label as a widget on lv_scr_act();
 * since TestScreen now lv_scr_loads its own screen instead, the
 * old label widget is orphaned and the timer callback would
 * write to a hidden (or freed) object. If we want a press
 * counter on TestScreen later, we add it as a widget inside
 * TestScreen and refresh it from a TestScreen-owned timer. */

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
/* The two functions below are retained but no longer called as of
 * S-MP16c (LVGL took over panel rendering). Kept for reference
 * since LVGL widgets for battery / modem / power-button status are
 * a near-term port — these functions are the spec for what those
 * widgets need to show. Marked unused so GCC doesn't warn under
 * -Wunused-function; the linker will GC them out anyway. */
__attribute__((unused))
static void redraw_value(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    display_fill_rect(x, y, 110, 7, bg);
    display_str(x, y, s, fg, bg);
}

__attribute__((unused))
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

    /* S-MP24/1: query AT+CCLK? for a network-supplied wall clock.
     * Single-shot: on success the parsed UTC epoch is cached and
     * exposed via clock_source_epoch_utc(). On no-modem or no-NITZ
     * hardware this returns ESP_ERR_TIMEOUT / ESP_FAIL and the
     * firmware keeps running on PhoneClock's synthetic 2026-01-01
     * anchor.
     *
     * S-MP24/2: on success, hand the cached (epoch_utc,
     * tz_offset_sec) pair to clock_source_bridge_apply() so
     * PhoneClock (and through it the status bar, Date&Time
     * picker, PhoneAlarmService, PhoneVirtualPet, ...) flip
     * over to the network-supplied wall clock. The bridge
     * lives in chatter_app's shim/ClockSourceBridge.cpp; we
     * forward-declare its extern-"C" entry point here so we
     * don't have to drag a new header into the main component.
     * On clock_source_init() failure we skip the bridge call;
     * PhoneClock stays on its 2026-01-01 synthetic anchor. */
    r = clock_source_init(5000);
    if (r != ESP_OK) {
        ESP_LOGI(TAG, "Network clock not available (err: %s) -- using anchor",
                 esp_err_to_name(r));
    } else if (clock_source_have_time()) {
        /* Defined in chatter_app's shim/ClockSourceBridge.cpp. The
         * extern-"C" linkage is what lets a C++ implementation in
         * chatter_app expose a stable symbol that any caller --
         * including the pure-C HAL on the main side -- can link
         * against. Declared locally so we don't have to pull a new
         * header into the main component just for one symbol. */
        extern "C" void clock_source_bridge_apply(uint32_t epoch_utc,
                                                  int32_t  tz_offset_sec);
        clock_source_bridge_apply(clock_source_epoch_utc(),
                                  clock_source_tz_offset_seconds());
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

    /* S-MP23/1 — explicit NVS flash bring-up.
     *
     * Settings.begin() (called from Chatter.begin() below) does
     * nvs_open("CircuitOS", ...) and silently fails with
     * ESP_ERR_NVS_NOT_INITIALIZED unless nvs_flash_init() has run
     * first. arduino-esp32 v3 does NOT call nvs_flash_init() from
     * initArduino(); it deferred it to consumers (Preferences,
     * WiFi.begin) that we don't use. So we own it here.
     *
     * Idempotent: a second call returns ESP_OK without side
     * effects. The recovery branch handles a corrupt or
     * version-mismatched partition by erasing + reinitialising.
     *
     * This unblocks both the existing Settings path AND the
     * upcoming Storage layer migration (S-MP23 proper, where
     * StorageStub.cpp's Repo<T> methods will key NVS records by
     * a per-type prefix + numeric uid). */
    {
        esp_err_t r = nvs_flash_init();
        if (r == ESP_ERR_NVS_NO_FREE_PAGES ||
            r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS reinit (was: %s)", esp_err_to_name(r));
            ESP_ERROR_CHECK(nvs_flash_erase());
            r = nvs_flash_init();
        }
        if (r == ESP_OK) {
            ESP_LOGI(TAG, "NVS flash initialised");
        } else {
            ESP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(r));
        }
    }

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

    /* S-MP16c: build a minimal LVGL UI and start the frame pump.
     * From this point on, the LVGL task owns the panel — the
     * dashboard's manual display_fill / display_str calls are
     * retired (would race on the SPI bus). The keypad listener and
     * battery/modem/power-button background tasks continue as
     * before; they don't touch the panel.
     *
     * UI for this first cut is intentionally tiny:
     *   - dark synthwave bg
     *   - "MP2.4" title centred
     *   - button counter line that an lv_timer refreshes 5×/s
     * That's enough to prove the flush path end-to-end and lets us
     * eyeball the panel for tearing / colour-channel swaps / wrong
     * geometry. Real screens (IntroScreen, MainMenu) land on top of
     * this same flush in later sessions. */
    if (lvgl_glue_init() != ESP_OK) {
        ESP_LOGE(TAG, "LVGL init failed — continuing without UI");
    } else {
        ESP_LOGI(TAG, "LVGL initialised, bringing up CircuitOS layer");

        /* S-MP19/5: call Chatter.begin() BEFORE instantiating any
         * screen. Chatter.begin() constructs MP24Input, which is
         * the only path that sets the Input::instance singleton.
         *
         * Without this, PhoneHomeScreen's constructor crashes:
         *   PhoneHomeScreen::PhoneHomeScreen()
         *   → new PhoneIdleHint(obj)
         *   → Input::getInstance()->addListener(this)
         *   → null deref at offset 0xb4 (the listeners Vector)
         *
         * Diagnosed from boot.log backtrace (May 17):
         *   PC 0x4208434b  Vector<InputListener*>::indexOf
         *   PC 0x4202e6bc  Input::addListener
         *   PC 0x4201abd5  PhoneIdleHint::PhoneIdleHint
         *   PC 0x420106de  PhoneHomeScreen::PhoneHomeScreen
         *   EXCVADDR 0x000000b4  (listeners vector at offset 0xb4)
         *
         * Chatter.begin() also instantiates Display, registers
         * MP24Input with LoopManager, brings up Settings (NVS),
         * and registers Battery — all of which downstream screens
         * + LoopListeners assume are live. */
        Chatter.begin();
        ESP_LOGI(TAG, "Chatter.begin() done — Input singleton live");

        ESP_LOGI(TAG, "Instantiating PhoneHomeScreen");

        /* S-MP18z: replaced PhoneAppStubScreen with PhoneHomeScreen
         * as the boot destination. PhoneHomeScreen is the Sony-
         * Ericsson-silhouette homescreen — the most representative
         * MAKERphone 2.0 screen we have compiled. It composes the
         * universal-UI trio (PhoneSynthwaveBg + PhoneStatusBar +
         * PhoneSoftKeyBar) plus PhoneClockFace (running clock),
         * plus overlay widgets (PhoneIdleHint, PhoneTipBanner,
         * PhoneNotificationToast, PhoneYawnOverlay, PhoneChargeBars,
         * PhoneChargingOverlay, PhoneOperatorBanner, PhoneConfetti
         * Overlay) — basically a full Element showcase.
         *
         * Navigation wired in S-MP19 (HomeFactory.cpp):
         *   - BTN_RIGHT (MENU)    → push PhoneMainMenu
         *   - BTN_BACK hold       → push LockScreen
         *
         * Earlier factories (Welcome / AppStub / Test) stay in
         * the build for one-line revert.
         *
         * Order constraint: lvgl_glue_init first (LVGL state
         * ready), Chatter.begin() second (Input singleton +
         * Display + Settings + Battery), screen instantiation
         * third (constructor uses LVGL API + Input on app_main
         * task), then lvgl_glue_run (LVGL task starts, processes
         * the queued screen load on its first iteration). */
        extern void chatter_app_start_home_screen(void);
        chatter_app_start_home_screen();
        ESP_LOGI(TAG, "PhoneHomeScreen instantiated + start()ed");

        /* The mp24_status_timer for the 'btn:N' counter is no
         * longer hooked to any visible widget — TestScreen owns
         * the screen now. Leaving the timer creation in would
         * just call into a callback that touches the old (now
         * orphaned) s_lvgl_btn_label pointer. Drop it. */

        lvgl_glue_run();
        ESP_LOGI(TAG, "LVGL frame pump running");

        /* S-MP15a: LoopManager task. Drives every CircuitOS
         * LoopListener (Display, Input, Battery, Settings — all
         * registered by MP24Chatter::begin()) AND, crucially,
         * the LoopManager::defer() queue that LVScreen uses to
         * run its onStart() callbacks after lv_scr_load completes.
         *
         * Without this task running, any LVScreen subclass we
         * try to instantiate would lv_scr_load to the panel
         * fine, but its onStart() would never fire and the
         * keypad-group switch on screen entry would never happen
         * either — the user would see the new screen but be
         * unable to interact with anything on it.
         *
         * Why a separate task instead of CIRCUITOS_TASK mode:
         * the CircuitOS-internal startTask() machinery uses its
         * own Util/Task wrapper that depends on Arduino's
         * FreeRTOS bridge. We get a cleaner failure surface by
         * just spawning the task ourselves from ESP-IDF directly.
         *
         * Sizing:
         *   - 8 KB stack matches the LVGL task. LoopListener
         *     subclasses are mostly small (timer ticks, state
         *     advances), but downstream code may eventually do
         *     LVGL widget creation inside deferred callbacks.
         *   - 5 ms polling = 200 Hz. CircuitOS's own task does
         *     vTaskDelay(1) for ~1 kHz, but that's wasteful for
         *     UI work where we redraw at 30 fps max anyway. */
        xTaskCreate(loop_manager_task, "loopmgr", 8192, NULL,
                    4 /* priority — same band as lvgl task */,
                    NULL);
        ESP_LOGI(TAG, "LoopManager task spawned");
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

    ESP_LOGI(TAG, "Entering background-tasks-only loop (LVGL owns the panel).");

    /* Main task post-bringup: every visible/audible piece of work
     * has its own task or LVGL/LoopManager-driven cadence.
     *
     *   - LVGL renders on the lvgl task (lvgl_glue_run).
     *   - LoopManager pumps Display/Input/Battery/Settings + the
     *     LVScreen deferred queue on the loopmgr task.
     *   - Keypad ISRs feed the kp_input task, which fans out to
     *     hal/input_keypad listeners.
     *   - Battery sampler, modem RX, audio_i2s2 sampler all run
     *     in their own tasks too.
     *
     * So app_main literally has nothing left to do. The previous
     * revision blinked the four AW9523B LEDs at 2.5 Hz here as a
     * "we're alive" indicator — now removed (user feedback: the
     * blink is visually noisy and the LVGL screen + battery LED
     * status are already plenty of alive-signal). The LEDs go
     * dark; if we want them re-enabled for a specific signal
     * (e.g. modem state, missed-call indicator), that comes
     * later as deliberate UI, not a heartbeat.
     *
     * Putting the AW9523B LEDs to OFF once explicitly so we
     * don't inherit whatever level they were at when the loop
     * was running. */
    aw9523b_set_leds(false);

    /* Idle indefinitely. The task can't exit without taking the
     * FreeRTOS scheduler down, so we vTaskDelay forever. 1 s
     * granularity — nothing depends on this task waking. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
