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

/* S-MP19/4: gameStarted is normally defined inside the Games
 * engine (Games/GameEngine/Game.cpp) which we don't compile —
 * needs glm.h vendored. BuzzerService.cpp and SleepService.cpp
 * both read it via 'extern bool gameStarted;' to suppress
 * audio + sleep timers while a game is active.
 *
 * Define it here as 'permanently false'. Once the games engine
 * lands this definition moves out to its real home. */
bool gameStarted = false;

/* S-MP20/5: startedGame is a peer of gameStarted, set/cleared
 * by Game::start() / Game::stop() to point at the currently
 * running Game. BuzzerService.cpp, ShutdownService.cpp, and the
 * Game pop-self-delete path all read it. With Game.cpp now in
 * chatter_app SRCS (compile-only validation -- gc-sections still
 * drops the TU at link time because nothing in SRCS instantiates
 * a Game), Game.cpp's `extern Game* startedGame;` would become
 * an unresolved external symbol if anything DID pull the start/
 * stop bodies in. Define it here as nullptr so the symbol is
 * always available, sized correctly, and trivially defined.
 *
 * Forward-declared `class Game;` is sufficient -- we never deref
 * the pointer in this TU. Including Games/GameEngine/Game.h
 * here would pull glm.h transitively, which we want to keep
 * scoped to the chatter_app component. */
class Game;
Game* startedGame = nullptr;

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
