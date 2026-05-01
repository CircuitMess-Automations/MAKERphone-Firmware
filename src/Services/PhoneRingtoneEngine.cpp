#include "PhoneRingtoneEngine.h"
#include <Audio/Piezo.h>
#include <Loop/LoopManager.h>
#include <Settings.h>

PhoneRingtoneEngine Ringtone;

void PhoneRingtoneEngine::begin(){
	playing = false;
	step = 0;
	stepElapsedUs = 0;
	inGap = false;
	current = Melody{ nullptr, 0, 0, false, nullptr };
}

void PhoneRingtoneEngine::play(const Melody& melody){
	if(melody.notes == nullptr || melody.count == 0) return;

	current = melody;
	step = 0;
	stepElapsedUs = 0;
	inGap = false;

	if(!playing){
		playing = true;
		LoopManager::addListener(this);
	}

	enterStep(0);
}

void PhoneRingtoneEngine::play(const Note* notes, uint16_t count, bool loopForever,
							   uint16_t gapMs, const char* name){
	Melody m{ notes, count, gapMs, loopForever, name };
	play(m);
}

void PhoneRingtoneEngine::stop(){
	const bool wasPlaying = playing;
	playing = false;

	if(wasPlaying){
		LoopManager::removeListener(this);
	}

	Piezo.noTone();
	step = 0;
	stepElapsedUs = 0;
	inGap = false;
}

void PhoneRingtoneEngine::enterStep(uint16_t i){
	step = i;
	stepElapsedUs = 0;
	inGap = false;
	emitTone();
}

void PhoneRingtoneEngine::emitTone(){
	if(!Settings.get().sound){
		Piezo.noTone();
		return;
	}
	if(step >= current.count) return;

	const uint16_t f = current.notes[step].freq;
	if(f == 0){
		Piezo.noTone();
	}else{
		Piezo.tone(f);
	}
}

void PhoneRingtoneEngine::loop(uint micros){
	if(!playing) return;

	// Sound muted mid-ring — hush, but keep the playhead moving so the
	// melody resumes naturally if the user re-enables sound.
	if(!Settings.get().sound){
		Piezo.noTone();
	}

	stepElapsedUs += micros;

	if(!inGap){
		const uint32_t durUs = (uint32_t) current.notes[step].durationMs * 1000UL;
		if(stepElapsedUs < durUs) return;

		// Note finished — silence the piezo and (optionally) enter the gap.
		Piezo.noTone();
		stepElapsedUs = 0;

		if(current.gapMs > 0){
			inGap = true;
			return;
		}
		// no gap: fall through and advance immediately
	}else{
		const uint32_t gapUs = (uint32_t) current.gapMs * 1000UL;
		if(stepElapsedUs < gapUs) return;
		stepElapsedUs = 0;
		inGap = false;
	}

	// Advance to the next note.
	step++;
	if(step >= current.count){
		if(current.loop){
			step = 0;
		}else{
			stop();
			return;
		}
	}
	emitTone();
}
