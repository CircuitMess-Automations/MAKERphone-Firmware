#ifndef CHATTER2_FIRMWARE_UNLOCKSLIDE_H
#define CHATTER2_FIRMWARE_UNLOCKSLIDE_H

#include <Arduino.h>
#include <Loop/LoopListener.h>
#include "../Interface/LVObject.h"
#include <functional>

class UnlockSlide : public LVObject, private LoopListener {
public:
	UnlockSlide(lv_obj_t* parent, std::function<void()> onDone);

	// Legacy BTN_R hold animation - kept as a fallback unlock path.
	void start();
	void stop();
	void reset();
	void shake();

	// S48: Left -> Right input chord. Pressing BTN_LEFT calls armChord()
	// to slide the lock icon halfway, opening a 1.5 s window in which a
	// BTN_RIGHT press triggers completeChord() to finish the slide and
	// fire onDone. If the window expires the slide auto-resets and the
	// `onArmTimeout` callback is invoked so the host (LockScreen) can
	// drop the matching shimmer state on PhoneLockHint.
	void armChord();
	void completeChord();
	bool isArmed() const;
	void setOnArmTimeout(std::function<void()> cb);

private:
	void loop(uint32_t dt);

	enum State {
		Idle, Shake, Slide,
		ChordArming, ChordSliding
	} state = Idle;

	lv_obj_t* text;
	lv_obj_t* icon;
	lv_obj_t* lock;

	std::function<void()> onDone;
	std::function<void()> onArmTimeout;
	bool unlocked = false;

	float t = 0;
	void repos();

	uint32_t armedAt = 0;

	static constexpr float slideSpeed          = 1.6f;
	static constexpr float shakeSpeed          = 2.6f;
	static constexpr float armSpeed            = 4.0f;  // 0 -> 0.5 in ~125 ms
	static constexpr float chordCompleteSpeed  = 2.4f;  // 0.5 -> 1.0 in ~210 ms
	static constexpr float shakeHalfDist       = 2.0f;
	static constexpr float chordHalfTarget     = 0.5f;
	static constexpr uint32_t ChordWindowMs    = 1500;

};


#endif //CHATTER2_FIRMWARE_UNLOCKSLIDE_H
