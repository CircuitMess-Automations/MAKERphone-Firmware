/*
 * mp24/components/chatter_app/shim/SleepServiceStub.cpp
 *
 * Replacement for src/Services/SleepService.cpp. Upstream
 * SleepService.cpp pulls Games/GameEngine/Game.h (for the
 * "is a game running?" check before light-sleep) + LockScreen
 * + FSLVGL + driver/rtc_io.h. Games/GameEngine needs glm.h
 * which we don't vendor.
 *
 * Same pattern as MessageServiceStub / LoRaServiceStub: the
 * upstream header is unchanged, the upstream .cpp is excluded
 * from chatter_app SRCS (just not listed), and this file
 * provides the public ABI as no-ops.
 *
 * What the stub does:
 *
 *   - SleepService()  constructor — info-log only.
 *   - begin()         info-log no-op.
 *   - loop(uint)      no-op. Real impl would compare activity
 *                     time vs sleepTime / shutdownTime and
 *                     enter light-sleep / power-off.
 *   - enterSleep()    no-op. Real impl would store state,
 *                     turn off backlight, and call
 *                     esp_light_sleep_start().
 *   - turnOff()       no-op. Real impl would power off.
 *   - resetActivity() no-op. Real impl resets the timer.
 *   - updateTimes()   no-op. Real impl reads Settings.
 *   - msgReceived()   no-op. Real impl flags gotMessage so
 *                     the wake-from-sleep path knows to show
 *                     a notification.
 *   - anyKeyPressed() no-op InputListener override. Real impl
 *                     calls resetActivity().
 *
 * The 'Sleep' global is defined here.
 *
 * Behavioural effect: device stays awake indefinitely, never
 * tries to enter light-sleep or power-off. Acceptable for
 * bench testing; real power management lands later.
 */

#include "Services/SleepService.h"

#include "esp_log.h"

static const char *TAG = "SleepService(stub)";

SleepService Sleep;

SleepService::SleepService()
{
    ESP_LOGI(TAG, "stub constructed");
}

void SleepService::begin()
{
    ESP_LOGI(TAG, "begin() — stub no-op (device stays awake)");
    sleepTime    = 0;
    shutdownTime = 0;
    activityTime = 0;
}

void SleepService::loop(uint /*micros*/)
{
    /* Real impl: if (millis - activityTime > sleepTime * 1000)
     * enterSleep(); if (millis - activityTime > shutdownTime *
     * 1000) turnOff(). Stub never sleeps. */
}

void SleepService::enterSleep()
{
    /* Real impl drives the display to black, turns off
     * peripherals, calls esp_light_sleep_start(). */
}

void SleepService::turnOff()
{
    /* Real impl asserts uPOWER_OFF (GPIO1) — Decision-C still
     * pending (Phase 2 hardware verification of polarity). */
}

void SleepService::resetActivity()
{
    activityTime = millis();
}

void SleepService::updateTimes()
{
    /* Real impl reads Settings.sleepTime / shutdownTime. */
}

void SleepService::msgReceived(const Message & /*message*/)
{
    gotMessage = true;
}

void SleepService::anyKeyPressed()
{
    resetActivity();
}
