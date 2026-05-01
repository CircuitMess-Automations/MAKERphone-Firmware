#include "PhoneIdleDim.h"
#include <Input/Input.h>
#include <Loop/LoopManager.h>
#include <Chatter.h>
#include <Settings.h>

/*
 * S69 — PhoneIdleDim
 *
 * See PhoneIdleDim.h for the full design rationale. Implementation is
 * deliberately tiny: one timestamp, one boolean, one re-applied
 * brightness write per state transition. Everything heavy (the actual
 * sleep + LoRa-aware wake) still lives in SleepService.
 */

PhoneIdleDim IdleDim;

PhoneIdleDim::PhoneIdleDim(){
}

void PhoneIdleDim::begin(){
	Input::getInstance()->addListener(this);
	LoopManager::addListener(this);

	resetActivity();
}

void PhoneIdleDim::resetActivity(){
	activityTime = millis();
	// SleepService runs Input::loop() during its wake-check window
	// while the panel PWM is still de-initialised - if we let
	// applyBrightness() through here we would prematurely re-init
	// the LEDC channel and fight SleepService's deinit/fadeIn pair.
	// Just bookkeep the activity timestamp; the next loop() tick
	// after fadeIn() will resync the bright state via
	// `dimmed && backlightPowered` -> apply.
	if(!Chatter.backlightPowered()) return;
	if(dimmed){
		applyBrightness(false);
		dimmed = false;
	}
}

void PhoneIdleDim::loop(uint /*micros*/){
	// Skip while the backlight is electrically off — that means
	// SleepService is mid-fade-out, mid-light-sleep, or has just
	// powered the panel down for a deep-sleep shutdown. Writing PWM
	// duties through `Chatter.setBrightness()` here would re-init the
	// PWM peripheral and fight that path. The next anyKeyPressed()
	// after SleepService's fadeIn() will resync us automatically.
	if(!Chatter.backlightPowered()) return;

	if(dimmed) return;

	const uint32_t now = millis();
	if((uint32_t)(now - activityTime) < IDLE_DIM_MS) return;

	applyBrightness(true);
	dimmed = true;
}

void PhoneIdleDim::anyKeyPressed(){
	resetActivity();
}

void PhoneIdleDim::applyBrightness(bool toDim){
	const uint8_t userBright = Settings.get().screenBrightness;
	uint8_t target;
	if(toDim){
		const uint16_t dimmedRaw = (uint16_t)((float)userBright * DIM_FACTOR);
		// Hard floor so we never drop the panel to fully off here —
		// SleepService is the only path allowed to extinguish the
		// backlight completely.
		uint8_t dimmedClamped = (uint8_t) (dimmedRaw > 255 ? 255 : dimmedRaw);
		if(dimmedClamped < DIM_FLOOR) dimmedClamped = DIM_FLOOR;
		// And don't ever dim *up* past the user's setting (defensive
		// — would only happen if userBright < DIM_FLOOR).
		if(dimmedClamped > userBright) dimmedClamped = userBright;
		target = dimmedClamped;
	}else{
		target = userBright;
	}

	if(target == lastApplied) return;

	Chatter.setBrightness(target);
	lastApplied = target;
}
