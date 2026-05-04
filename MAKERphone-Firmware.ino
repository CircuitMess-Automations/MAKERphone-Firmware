#include <Arduino.h>
#include <CircuitOS.h>
#include <Chatter.h>
#include <Loop/LoopManager.h>
#include <lvgl.h>
#include "src/InputChatter.h"
#include "src/FSLVGL.h"
#include "src/ChatterTheme.h"
#include <SPIFFS.h>
#include "src/Screens/MainMenu.h"
#include "src/Storage/Storage.h"
#include "src/Services/LoRaService.h"
#include "src/Services/MessageService.h"
#include "src/Services/PhoneCallService.h"
#include "src/Elements/IntroScreen.h"
#include "src/Screens/PhoneBootSplash.h"
#include "src/Screens/PhoneSimPinScreen.h"
#include "src/MAKERphoneConfig.h"
#include "src/Interface/Pics.h"
#include "src/Services/ProfileService.h"
#include "src/Screens/UserHWTest.h"
#include <Settings.h>
#include <Util/HWRevision.h>
#include "src/Services/SleepService.h"
#include "src/Services/PhoneIdleDim.h"
#include "src/Services/PhoneKonamiCode.h"
#include "src/Services/PhoneTiltSimulator.h"
#include "src/Services/ShutdownService.h"
#include "src/Services/BuzzerService.h"
#include "src/Services/PhoneRingtoneEngine.h"
#include "src/Services/PhoneVibrationEngine.h"
#include "src/Services/PhoneAlarmService.h"
#include "src/Services/PhoneVirtualPet.h"
#include "src/Services/PhoneChargeChime.h"
#include "src/Services/PhoneDeliveredChime.h"
#include "src/JigHWTest/JigHWTest.h"
#include "src/Games/GameEngine/Game.h"

lv_disp_draw_buf_t drawBuffer;
Display* display;

void lvglFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p){
	uint32_t w = (area->x2 - area->x1 + 1);
	uint32_t h = (area->y2 - area->y1 + 1);

	TFT_eSPI &tft = *display->getTft();
	tft.startWrite();
	tft.setAddrWindow(area->x1, area->y1, w, h);
	tft.pushColors(&color_p->full, w * h, true);
	tft.endWrite();
	lv_disp_flush_ready(disp);
}

struct {
	UID_t uid;
	const char* nickname;
	uint8_t avatar;
	uint16_t hue;
} const Chatters[] = {
		{ 0xdcba4bf23a08, "Grle v0.5", 1, 0 },
		{ 0x48275612cfa4, "Choki v0.6", 2, 80 },
		{ 0x88a61312cfa4, "Zeus v0.6", 5, 90 },
		{ 0x48e4e0e6e2e0, "Johnny v0.4", 3, 160 },
		{ 0xe4c14bf23a08, "Rus v0.5", 4, 240 },
		{ 0x38c24bf23a08, "Crni v0.5", 6, 40 },
};

void loadMock(bool clear = false){
	if(clear){
		for(UID_t uid : Storage.Messages.all()) Storage.Messages.remove(uid);
		for(UID_t uid : Storage.Friends.all()) Storage.Friends.remove(uid);
		for(UID_t uid : Storage.Convos.all()) Storage.Convos.remove(uid);
	}

	for(const auto& chatter : Chatters){
		Friend fren;
		fren.uid = chatter.uid;
		fren.profile.avatar = chatter.avatar;
		fren.profile.hue = (uint8_t) (chatter.hue / 2);
		strncpy(fren.profile.nickname, chatter.nickname, 15);
		Storage.Friends.add(fren);

		if(chatter.uid == ESP.getEfuseMac()) continue;

		Convo convo;
		convo.uid = fren.uid;

		const int count = LoRa.rand() % 5;
		for(int i = 0; i < count; i++){
			Message message;
			if(LoRa.rand() % 2){
				message.setText("Hello!");
			}else{
				message.setPic(LoRa.rand() % NUM_PICS);
			}

			message.uid = LoRa.randUID();
			message.received = LoRa.rand() % 2;
			message.outgoing = LoRa.rand() % 2;
			convo.messages.push_back(message.uid);
			Storage.Messages.add(message);
		}

		Storage.Convos.add(convo);
	}

	// Reinit my profile
	Profiles.begin();
}

void printData(){
	for(UID_t uid : Storage.Friends.all()){
		Friend fren = Storage.Friends.get(uid);
		printf("%llx | Fren: %s | Hue: %d | Avatar: %d\n", fren.uid, fren.profile.nickname, fren.profile.hue, fren.profile.avatar);

		Convo convo = Storage.Convos.get(uid);
		printf("%llx | Convo: %d messages\n", convo.uid, convo.messages.size());

		for(UID_t uid : convo.messages){
			Message message = Storage.Messages.get(uid);
			printf("%llx | Message: %s\n", message.uid, message.getText().c_str());
		}
	}
}

void boot(){
	lv_timer_handler();

	display->getBaseSprite()->drawIcon(SPIFFS.open("/splash.raw"), 0, 0, 160, 128);
	display->commit();

	Chatter.fadeIn();

	FSLVGL::loadCache();

	Storage.begin();
	Messages.begin();

	LoRa.begin();
	Profiles.begin();

	// S28: register the call service with MessageService so a magic
	// CALL_REQUEST text from a paired peer raises PhoneIncomingCall on
	// top of the current screen. Must come after Messages.begin() so the
	// listener subscription is active before LoRa starts delivering.
	Phone.begin();

	//loadMock(true);
	//printData();

	Sleep.begin();

	// S69: idle-dim runs alongside SleepService - it is the soft,
	// reversible step (auto-dim backlight after 30 s, restore on any
	// key) that precedes the hard sleep / light-sleep that
	// SleepService still owns. Both share the same any-key activity
	// reset semantics through Input::addListener().
	IdleDim.begin();

	// S166: global Konami-code Easter-egg detector. Listens to every
	// button press for the canonical 10-press sequence and unlocks
	// the rainbow theme on a successful match. Idle-cheap (no
	// LoopManager subscription, just an InputListener); registering
	// the listener at boot means the same gesture works on every
	// screen without each LVScreen having to wire it. Ordered after
	// IdleDim.begin() so Input is fully alive before we subscribe -
	// matches the SleepService / BuzzerService / IdleDim pattern.
	Konami.begin();

	// S168: PhoneTiltSimulator - global "shake the phone" gesture
	// detector (hold BTN_L + BTN_R together for HoldMs, then fire
	// onShake() on whichever LVScreen is visible). Idle-cheap (single
	// millis compare per loop while the chord is inactive). Ordered
	// after Konami.begin() because both are passive Input listeners
	// that benefit from being subscribed before the first screen
	// pushes input through. Screens that opt-in to randomize via
	// onShake() pick this up automatically; everyone else inherits
	// the LVScreen::onShake() no-op default and is unaffected.
	Tilt.begin();


	// S148: the Ringtone engine has to come up BEFORE PhoneBootSplash
	// starts playing the four-note boot chime in its onStart(). The
	// engine's begin() is a pure state reset (no allocation, no
	// LoopManager subscription until play() is called), so promoting it
	// to here is side-effect free for the rest of the boot flow. The
	// remaining service begin()s stay tucked inside the IntroScreen
	// dismiss callback so they keep their original ordering after the
	// CircuitMess intro plays.
	Ringtone.begin();

	// S161: companion engine that drives the same piezo with low-pitch
	// rhythmic pulses for the Meeting-profile vibration choreography.
	// begin() is a pure state reset (no allocation, no LoopManager
	// subscription until play() is called) so promoting it next to
	// Ringtone.begin() above keeps both engines in lockstep without
	// changing boot ordering. PhoneCallService wires the matching
	// pattern into PhoneIncomingCall before push() so a Meeting-
	// profile incoming call rings via vibration the moment the
	// screen takes over.
	Vibrate.begin();

	// S56 + S162: the very first screen on boot is now PhoneBootSplash
	// - the MAKERphone-branded sunset wordmark splash. It holds for 3 s
	// (or any-key skip) and on dismiss runs the next stage of the boot
	// chain. With MAKERPHONE_SHOW_SIM_PIN enabled (the default) that
	// next stage is the decorative S162 "SIM PIN unlock" screen, which
	// in turn dismisses into the legacy IntroScreen. The intro keeps
	// doing all the post-intro routing into LockScreen -> PhoneHomeScreen
	// and the Service.begin() side-effects below. This is a precede-
	// not-replace wiring: the existing CircuitMess intro gif still plays,
	// but the user sees MAKERphone-branded screens first.
	//
	// startIntro is the shared continuation - both the SIM PIN dismiss
	// path (S162 enabled) and the direct splash-dismiss path (S162
	// disabled) call it to instantiate + start the IntroScreen.
	auto startIntro = [](){
		auto intro = new IntroScreen([](){
			Shutdown.begin();
			Buzz.begin();
			Alarms.begin();
			Pet.begin();
			ChargeChime.begin();
			DeliveredChime.begin();
		});
		intro->start();
	};
#if MAKERPHONE_SHOW_SIM_PIN
	auto* splash = new PhoneBootSplash([](){
		// S162: between the boot splash and the intro gif, flash a
		// decorative SIM-card PIN entry surface. Any 4-digit PIN
		// unlocks; BACK on an empty buffer skips immediately. The
		// dismiss callback below then starts the legacy intro,
		// preserving every downstream side-effect.
		auto* pin = new PhoneSimPinScreen([](){
			auto intro = new IntroScreen([](){
				Shutdown.begin();
				Buzz.begin();
				Alarms.begin();
				Pet.begin();
				ChargeChime.begin();
				DeliveredChime.begin();
			});
			intro->start();
		});
		pin->start();
	});
#else
	auto* splash = new PhoneBootSplash(startIntro);
#endif
	(void) startIntro;

	lv_timer_handler();

	splash->start();
}

bool checkJig(){
	char buf[7];
	int wp = 0;

	uint32_t start = millis();
	int c;
	while(millis() - start < 500){
		vTaskDelay(1);
		c = getchar();
		if(c == EOF) continue;
		buf[wp] = (char) c;
		wp = (wp + 1) % 7;

		for(int i = 0; i < 7; i++){
			int match = 0;
			static const char* target = "JIGTEST";

			for(int j = 0; j < 7; j++){
				match += buf[(i + j) % 7] == target[j];
			}

			if(match == 7) return true;
		}
	}

	return false;
}

void initLog(){
	esp_log_level_set("*", ESP_LOG_NONE);
	return;

	const static auto tags = { "*" };

	for(const char* tag : tags){
		esp_log_level_set(tag, ESP_LOG_VERBOSE);
	}
}

void setup(){
	Serial.begin(115200);

	randomSeed(analogRead(BATTERY_PIN) * 13 + analogRead(BATTERY_PIN) * 7 + 2);

	LoopManager::reserve(24);

	Chatter.begin(false);
	display = Chatter.getDisplay();

	initLog();

	if(checkJig()){
		printf("Jig\n");
		auto test = new JigHWTest(display);
		Chatter.fadeIn();
		test->start();
		for(;;);
	}else{
		printf("Hello\n");
	}

	if(Battery.getPercentage() == 0){
		LoRa.initStateless();
		Sleep.turnOff();
		for(;;);
	}

	// MAKERphone prototype: keep sound muted and disable auto-sleep / auto-shutdown
	// regardless of any previously stored Settings values. The Settings.h struct
	// defaults already match these for fresh devices, but production Chatters that
	// were running the original firmware have stored sound=true, sleepTime=1,
	// shutdownTime=1 in NVS - this override + store migrates them once.
	// If you ever want these user-tunable again from the Settings menu, remove
	// this block (the on-disk defaults will then determine startup behaviour).
	{
		auto& cfg = Settings.get();
		if(cfg.sound || cfg.sleepTime != 0 || cfg.shutdownTime != 0){
			cfg.sound        = false;
			cfg.sleepTime    = 0;  // SleepSeconds index 0 == "OFF" (never sleep)
			cfg.shutdownTime = 0;  // ShutdownSeconds index 0 == "OFF" (never shutdown)
			Settings.store();
		}
	}

	Piezo.setMute(!Settings.get().sound);

	lv_init();
	lv_disp_draw_buf_init(&drawBuffer, display->getBaseSprite()->getBuffer(), NULL, 160 * 128);

	new FSLVGL(SPIFFS, 'S');

	static lv_disp_drv_t displayDriver;
	lv_disp_drv_init(&displayDriver);
	displayDriver.hor_res = 160;
	displayDriver.ver_res = 128;
	displayDriver.flush_cb = lvglFlush;
	displayDriver.draw_buf = &drawBuffer;
	displayDriver.full_refresh = true;
	lv_disp_t * disp = lv_disp_drv_register(&displayDriver);
	chatterThemeInit(disp);

	Chatter.getInput()->addListener(new InputChatter());

	printf("UID: 0x%llx\n", ESP.getEfuseMac());

	if(!Settings.get().tested){
		if(HWRevision::get() > 0){
			Settings.get().tested = true;
			Settings.store();
		}else{
			FSLVGL::loadCache();

			auto test = new UserHWTest([](){
				Settings.get().tested = true;
				Settings.store();
				Chatter.fadeOut();
				boot();
			});

			test->start();
			lv_timer_handler();
			Chatter.fadeIn();
			return;
		}
	}

	boot();
}

bool gameStarted = false;
Game* startedGame = nullptr;

void loop(){
	if(!gameStarted){
		lv_timer_handler();
	}
	LoopManager::loop();
}

