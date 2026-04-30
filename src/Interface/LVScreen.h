#ifndef CHATTER_FIRMWARE_LVSCREEN_H
#define CHATTER_FIRMWARE_LVSCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include "LVObject.h"
#include <unordered_set>

class LVScreen : public LVObject {
public:
	LVScreen();
	virtual ~LVScreen();

	virtual void onStarting();
	virtual void onStart();
	virtual void onStop();

	void start(bool animate = false, lv_scr_load_anim_t animation = LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
	void stop();

	// S21: push/pop now accept an optional transition animation. Defaults
	// preserve the legacy behavior (push slides up from bottom, pop slides
	// back down to top), so every existing call site keeps working.
	// PhoneHomeScreen <-> PhoneMainMenu uses MOVE_LEFT / MOVE_RIGHT for
	// the feature-phone "drill into menu" feel.
	void push(LVScreen* other, lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
	void pop(lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_MOVE_TOP);

	lv_group_t* getInputGroup();

	void setParent(LVScreen* parent);
	LVScreen* getParent() const;

	static LVScreen* getCurrent();

protected:
	lv_group_t* inputGroup;

	LVScreen* parent = nullptr;

private:
	static LVScreen* current;
	std::unordered_set<LVScreen*> delOnStart;

	bool running = false;

};


#endif //CHATTER_FIRMWARE_LVSCREEN_H
