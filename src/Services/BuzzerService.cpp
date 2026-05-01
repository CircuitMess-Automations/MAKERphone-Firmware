#include "BuzzerService.h"
#include <Pins.hpp>
#include <Audio/Piezo.h>
#include <Notes.h>
#include <Loop/LoopManager.h>
#include <Input/Input.h>
#include <Settings.h>

BuzzerService Buzz;


void BuzzerService::begin(){
	Messages.addReceivedListener(this);
	Input::getInstance()->addListener(this);
}

const std::unordered_map<uint8_t, uint16_t> BuzzerService::noteMap = {
		{BTN_1, NOTE_C4},
		{BTN_2, NOTE_D4},
		{BTN_3, NOTE_E4},
		{BTN_4, NOTE_F4},
		{BTN_5, NOTE_G4},
		{BTN_6, NOTE_A4},
		{BTN_7, NOTE_B4},
		{BTN_8, NOTE_C5},
		{BTN_9, NOTE_D5},
		{BTN_L, NOTE_E5},
		{BTN_0, NOTE_F5},
		{BTN_R, NOTE_G5},

		{BTN_LEFT, NOTE_B4},
		{BTN_RIGHT, NOTE_B4},
		{BTN_ENTER, NOTE_C5},
		{BTN_B, NOTE_A4}
};

const std::vector<BuzzerService::Note> BuzzerService::Notes = {
		{ NOTE_B5, 100000 },
		{ 0, 50000 },
		{ NOTE_B4, 100000 }
};


void BuzzerService::msgReceived(const Message &message){
	if(!Settings.get().sound) return;
	if(message.convo == noBuzzUID && noBuzzUID != ESP.getEfuseMac()) return;

	LoopManager::defer([this](uint32_t){
		LoopManager::defer([this](uint32_t){
			LoopManager::addListener(this);

			noteIndex = 0;
			noteTime = 0;
			Piezo.tone(Notes[noteIndex].freq);
		});
	});
}

void BuzzerService::setNoBuzzUID(UID_t noBuzzUid){
	noBuzzUID = noBuzzUid;
}

void BuzzerService::buttonPressed(uint i){
	extern bool gameStarted;
	if(gameStarted) return;
	if(i == BTN_ENTER && muteEnter) return;

	// S68 — subtle haptic-style nav-key tick. When the device is in
	// Mute / Vibrate (legacy `sound` flag off) but the user has opted
	// into key-tick haptics, emit a very short, very high-pitched
	// "click" on navigation buttons. The 4 ms / NOTE_F6 envelope is
	// deliberately at the low edge of "audible" so it reads as a soft
	// tactile confirmation rather than a chime. Only navigation keys
	// trigger it -- the dialer / alpha keys stay silent in Mute so a
	// long T9 message does not turn into a buzzy stream of clicks.
	// In Loud profile (`sound` true) the existing 25 ms per-button
	// musical tones below already give the user feedback, so the tick
	// layer skips itself to avoid double-firing.
	if(!Settings.get().sound){
		if(Settings.get().keyTicks){
			switch(i){
				case BTN_LEFT:
				case BTN_RIGHT:
				case BTN_2:
				case BTN_4:
				case BTN_6:
				case BTN_8:
				case BTN_ENTER:
				case BTN_BACK:
				case BTN_L:
				case BTN_R:
					Piezo.tone(NOTE_F6, 4);
					break;
				default:
					break;
			}
		}
		return;
	}
	Piezo.tone(noteMap.at(i), 25);
}

void BuzzerService::loop(uint micros){
	if(!Settings.get().sound){
		Piezo.noTone();
		LoopManager::removeListener(this);
		return;
	}

	noteTime += micros;
	if(noteTime < Notes[noteIndex].duration) return;

	noteIndex++;
	noteTime = 0;

	if(noteIndex >= Notes.size()){
		LoopManager::removeListener(this);
		Piezo.noTone();
		return;
	}

	if(Notes[noteIndex].freq == 0){
		Piezo.noTone();
		return;
	}


	Piezo.tone(Notes[noteIndex].freq);
}

void BuzzerService::setMuteEnter(bool muteEnter){
	BuzzerService::muteEnter = muteEnter;
}


