#include "ShutdownService.h"
#include <Loop/LoopManager.h>
#include <Battery/BatteryService.h>
#include "../Modals/BatteryNotification.h"
#include "../Modals/PhoneBatteryLowModal.h"
#include "SleepService.h"
#include "../Games/GameEngine/Game.h"

ShutdownService Shutdown;

void ShutdownService::begin(){
	checkTimer = checkInterval;
	LoopManager::addListener(this);
}

void ShutdownService::loop(uint micros){
	checkTimer+=micros;
	if(checkTimer >= checkInterval){
		checkTimer = 0;
		if(Battery.getPercentage() <= 1 && !shutdownStarted){
			shutdownStarted = true;
			showShutdown();
		}else if(Battery.getPercentage() <= 15 && !warningShown){
			// S58: bumped the warning threshold from 10% to 15% so
			// users get nudged earlier (matches the MAKERphone roadmap
			// entry "Battery-low modal (≤15%)") and replaced the
			// stock BatteryNotification::WARNING with the new
			// MAKERphone-styled PhoneBatteryLowModal which has a
			// palette-coherent slab + ringtone chirp.
			warningShown = true;
			showWarning();
		}
	}
}

void ShutdownService::showWarning(){
	extern bool gameStarted;
	if(gameStarted) return;

	Sleep.resetActivity();
	auto* parent = LVScreen::getCurrent();
	if(parent == nullptr){
		// Fall back to the legacy modal if there is no current LVScreen
		// (very early boot / jig-test contexts) - PhoneBatteryLowModal
		// requires a parent screen, the legacy one does too. Keeping
		// the fallback path means a missing parent never crashes
		// instead of nagging.
		return;
	}
	(new PhoneBatteryLowModal(parent))->start();
}

void ShutdownService::showShutdown(){
	extern bool gameStarted;
	if(gameStarted){
		extern Game* startedGame;
		if(startedGame){
			startedGame->pop();
		}
	}
	Sleep.resetActivity();
	(new BatteryNotification(LVScreen::getCurrent(), BatteryNotification::SHUTDOWN))->start();
}
