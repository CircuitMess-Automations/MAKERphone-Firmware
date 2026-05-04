#include "PhoneVibrationEngine.h"
#include <Audio/Piezo.h>
#include <Loop/LoopManager.h>

PhoneVibrationEngine Vibrate;

void PhoneVibrationEngine::begin(){
	playing = false;
	step = 0;
	stepElapsedUs = 0;
	inOff = false;
	current = Pattern{ nullptr, 0, false, DefaultFreq, nullptr };
}

void PhoneVibrationEngine::play(const Pattern& pattern){
	if(pattern.pulses == nullptr || pattern.count == 0) return;

	current = pattern;
	step = 0;
	stepElapsedUs = 0;
	inOff = false;

	if(!playing){
		playing = true;
		LoopManager::addListener(this);
	}

	enterStep(0);
}

void PhoneVibrationEngine::stop(){
	const bool wasPlaying = playing;
	playing = false;

	if(wasPlaying){
		LoopManager::removeListener(this);
	}

	Piezo.noTone();
	step = 0;
	stepElapsedUs = 0;
	inOff = false;
}

void PhoneVibrationEngine::enterStep(uint16_t i){
	step = i;
	stepElapsedUs = 0;
	inOff = false;
	emitPulse();
}

uint16_t PhoneVibrationEngine::effectiveFreq() const {
	// Per-pattern override wins; otherwise fall back to the felt-not-
	// heard 80 Hz default. Clamp to a safe band so a stray Pattern
	// authored with freq=0 always renders cleanly.
	const uint16_t f = current.freq;
	if(f == 0) return DefaultFreq;
	return f;
}

void PhoneVibrationEngine::emitPulse(){
	if(step >= current.count) return;
	const uint16_t onMs = current.pulses[step].onMs;
	if(onMs == 0){
		// Zero-length pulse degenerates to silence -- treat the whole
		// step as off so we never call tone(freq, 0) on the piezo.
		Piezo.noTone();
		inOff = true;
		return;
	}
	Piezo.tone(effectiveFreq());
}

void PhoneVibrationEngine::loop(uint micros){
	if(!playing) return;
	if(step >= current.count){
		stop();
		return;
	}

	stepElapsedUs += micros;

	if(!inOff){
		const uint32_t onUs = (uint32_t) current.pulses[step].onMs * 1000UL;
		if(stepElapsedUs < onUs) return;

		// Pulse finished -- silence the piezo and roll into the off-
		// phase. We always enter the off-phase even when offMs==0 so
		// the LoopManager tick that follows hands the next pulse a
		// clean state machine; an offMs of 0 just means we'll
		// transition out of inOff on the next tick after one frame
		// of silence.
		Piezo.noTone();
		stepElapsedUs = 0;
		inOff = true;
		return;
	}

	const uint32_t offUs = (uint32_t) current.pulses[step].offMs * 1000UL;
	if(stepElapsedUs < offUs) return;

	stepElapsedUs = 0;
	inOff = false;

	// Advance to the next pulse.
	step++;
	if(step >= current.count){
		if(current.loop){
			step = 0;
		}else{
			stop();
			return;
		}
	}
	emitPulse();
}
