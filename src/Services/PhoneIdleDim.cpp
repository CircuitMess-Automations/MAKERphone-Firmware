#include "PhoneIdleDim.h"
#include <Input/Input.h>
#include <Loop/LoopManager.h>
#include <Chatter.h>
#include <Settings.h>

/*
 * S69 / S198 - PhoneIdleDim
 *
 * See PhoneIdleDim.h for the full design rationale. Implementation is
 * deliberately tiny: one timestamp, one Stage enum, one re-applied
 * brightness write per state transition. Everything heavy (the actual
 * sleep + LoRa-aware wake) still lives in SleepService.
 *
 * S198: the original single-stage dim is now a two-stage dim
 * (Bright -> Dim @ 30 s -> DeepDim @ 90 s). Each transition is a
 * single Chatter.setBrightness() call gated by the lastApplied
 * dedupe; the hot path stays cheap and integer.
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
	// applyBrightnessFor() through here we would prematurely re-init
	// the LEDC channel and fight SleepService's deinit/fadeIn pair.
	// Just bookkeep the activity timestamp; the next loop() tick
	// after fadeIn() will resync the bright state via the
	// stage-not-Bright + backlightPowered branch in loop().
	if(!Chatter.backlightPowered()) return;
	if(currentStage != Stage::Bright){
		applyBrightnessFor(Stage::Bright);
		currentStage = Stage::Bright;
	}
}

void PhoneIdleDim::loop(uint /*micros*/){
	// Skip while the backlight is electrically off - that means
	// SleepService is mid-fade-out, mid-light-sleep, or has just
	// powered the panel down for a deep-sleep shutdown. Writing PWM
	// duties through `Chatter.setBrightness()` here would re-init the
	// PWM peripheral and fight that path. The next anyKeyPressed()
	// after SleepService's fadeIn() will resync us automatically.
	if(!Chatter.backlightPowered()) return;

	const uint32_t now = millis();
	const uint32_t elapsed = (uint32_t)(now - activityTime);

	// Determine the desired stage purely from elapsed time. The order
	// of the comparisons matters: we want DeepDim to win over Dim
	// once we cross the deeper threshold.
	Stage want;
	if(elapsed >= DEEP_DIM_MS){
		want = Stage::DeepDim;
	}else if(elapsed >= IDLE_DIM_MS){
		want = Stage::Dim;
	}else{
		want = Stage::Bright;
	}

	if(want == currentStage) return;

	applyBrightnessFor(want);
	currentStage = want;
}

void PhoneIdleDim::anyKeyPressed(){
	resetActivity();
}

void PhoneIdleDim::applyBrightnessFor(Stage s){
	const uint8_t userBright = Settings.get().screenBrightness;
	uint8_t target;
	switch(s){
		case Stage::Bright:
			target = userBright;
			break;
		case Stage::Dim: {
			const uint16_t dimmedRaw = (uint16_t)((float)userBright * DIM_FACTOR);
			uint8_t dimmedClamped = (uint8_t) (dimmedRaw > 255 ? 255 : dimmedRaw);
			if(dimmedClamped < DIM_FLOOR) dimmedClamped = DIM_FLOOR;
			// Don't ever dim *up* past the user's setting (defensive
			// - would only happen if userBright < DIM_FLOOR).
			if(dimmedClamped > userBright) dimmedClamped = userBright;
			target = dimmedClamped;
			break;
		}
		case Stage::DeepDim: {
			const uint16_t deepRaw = (uint16_t)((float)userBright * DEEP_DIM_FACTOR);
			uint8_t deepClamped = (uint8_t) (deepRaw > 255 ? 255 : deepRaw);
			if(deepClamped < DEEP_DIM_FLOOR) deepClamped = DEEP_DIM_FLOOR;
			if(deepClamped > userBright) deepClamped = userBright;
			target = deepClamped;
			break;
		}
		default:
			target = userBright;
			break;
	}

	if(target == lastApplied) return;

	Chatter.setBrightness(target);
	lastApplied = target;
}
