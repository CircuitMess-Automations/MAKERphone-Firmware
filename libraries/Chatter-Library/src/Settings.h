#ifndef CHATTER_LIBRARY_SETTINGS_H
#define CHATTER_LIBRARY_SETTINGS_H

#include <Arduino.h>

#define SLEEP_STEPS 5
#define SHUTDOWN_STEPS 5

extern const uint32_t SleepSeconds[SLEEP_STEPS];
extern const char* SleepText[SLEEP_STEPS];

extern const uint32_t ShutdownSeconds[SHUTDOWN_STEPS];
extern const char* ShutdownText[SHUTDOWN_STEPS];

struct SettingsData {
	// MAKERphone prototype defaults: sound off, no auto-sleep, no auto-shutdown.
	// sleepTime / shutdownTime are indices into SleepSeconds[] / ShutdownSeconds[];
	// index 0 corresponds to "OFF" (0 seconds, which SleepService treats as
	// "never auto-enter sleep"). Production Chatter previously defaulted to 30s
	// sleep / 30 min shutdown; for MAKERphone we want a phone that stays on.
	bool sound = false;
	uint8_t sleepTime = 0;
	uint8_t shutdownTime = 0;
	uint8_t screenBrightness = 200;
	bool tested = false;
	uint32_t messagesSent = 0;
	uint32_t messagesReceived = 0;
};

class SettingsImpl {
public:
	bool begin();

	void store();

	SettingsData& get();

	void reset();

private:
	SettingsData data;
};

extern SettingsImpl Settings;

#endif //CHATTER_LIBRARY_SETTINGS_H
