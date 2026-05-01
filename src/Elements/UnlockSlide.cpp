#include "UnlockSlide.h"
#include "../Fonts/font.h"
#include <utility>
#include <Loop/LoopManager.h>
#include <font/lv_font.h>

UnlockSlide::UnlockSlide(lv_obj_t* parent, std::function<void()> onDone) : LVObject(parent), onDone(std::move(onDone)){
	lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_hor(obj, 4, 0);
	lv_obj_set_style_pad_ver(obj, 3, 0);

	text = lv_label_create(obj);
	lv_label_set_text(text, "Hold        to unlock...");
	lv_obj_set_style_text_font(text, &pixelbasic7, 0);
	lv_obj_set_style_text_color(text, lv_color_make(220, 220, 220), 0);
	lv_obj_set_align(text, LV_ALIGN_BOTTOM_LEFT);
	lv_obj_set_x(text, 14);

	icon = lv_img_create(obj);
	lv_img_set_src(icon, "S:/Lock/Icon.bin");
	lv_obj_set_pos(icon, 41, 5);

	lock = lv_img_create(obj);
	lv_img_set_src(lock, "S:/Lock/Locked.bin");
	lv_obj_set_align(lock, LV_ALIGN_BOTTOM_LEFT);
}

void UnlockSlide::start(){
	LoopManager::addListener(this);
	lv_img_set_src(lock, "S:/Lock/Unlocked.bin");
	state = Slide;
	t = 0;
	lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(text, LV_OBJ_FLAG_HIDDEN);
}

void UnlockSlide::stop(){
	// Only the BTN_R hold flow is interruptible by release; the chord
	// states (ChordArming / ChordSliding) own their own lifecycle and
	// must not be ripped out by a stray button release event.
	if(state != Slide && state != Shake) return;

	LoopManager::removeListener(this);
	state = Idle;

	if(!unlocked){
		lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(text, LV_OBJ_FLAG_HIDDEN);
	}
}

void UnlockSlide::shake(){
	// A shake while a chord-slide is finalizing would fight the unlock
	// animation; ignore it. Arming-state shakes intentionally cancel
	// the armed chord (caller is expected to reset shimmer first).
	if(state == ChordSliding) return;

	LoopManager::addListener(this);
	state = Shake;
	t = 0;
}

void UnlockSlide::reset(){
	// Don't blow away an in-flight chord-sliding animation - it is
	// about to fire onDone and tear the screen down anyway.
	if(state == ChordSliding) return;

	LoopManager::removeListener(this);
	t = 0;
	repos();
	lv_img_set_src(lock, "S:/Lock/Locked.bin");
	if(!unlocked){
		lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(text, LV_OBJ_FLAG_HIDDEN);
	}
	state = Idle;
}

void UnlockSlide::armChord(){
	// Already running a slide / chord-completion - ignore so a stray
	// LEFT press cannot rewind in-flight animations.
	if(state == Slide || state == ChordSliding) return;

	LoopManager::addListener(this);
	state = ChordArming;
	t = 0;
	armedAt = millis();
	lv_img_set_src(lock, "S:/Lock/Locked.bin");
	// Hide the legacy "Hold ... to unlock" prompt during chord arming;
	// the shimmer hint above the slide carries the chord prompt now.
	lv_obj_add_flag(text, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
}

void UnlockSlide::completeChord(){
	if(state != ChordArming) return;

	LoopManager::addListener(this);
	state = ChordSliding;
	// t is left at chordHalfTarget so the lock animates from the
	// halfway position to the right edge - the visible "snap closed".
	lv_img_set_src(lock, "S:/Lock/Unlocked.bin");
}

bool UnlockSlide::isArmed() const{
	return state == ChordArming;
}

void UnlockSlide::setOnArmTimeout(std::function<void()> cb){
	onArmTimeout = std::move(cb);
}

void UnlockSlide::loop(uint32_t dt){
	float speed  = 1.0f;
	float target = 1.0f;
	switch(state){
		case Slide:        speed = slideSpeed;          target = 1.0f;             break;
		case Shake:        speed = shakeSpeed;          target = 1.0f;             break;
		case ChordArming:  speed = armSpeed;            target = chordHalfTarget;  break;
		case ChordSliding: speed = chordCompleteSpeed;  target = 1.0f;             break;
		default:
			return;
	}

	t = std::min(target, t + speed * (float) dt / 1000000.0f);

	repos();

	// ChordArming pauses at the halfway target waiting for completeChord.
	// Drive the timeout from here so a stale arm cannot strand the screen.
	if(state == ChordArming){
		if(millis() - armedAt >= ChordWindowMs){
			auto cb = onArmTimeout;
			reset();
			if(cb) cb();
		}
		return;
	}

	if(t >= target){
		LoopManager::removeListener(this);

		auto oldState = state;
		state = Idle;

		if(oldState == Slide || oldState == ChordSliding){
			unlocked = true;
			onDone();
		}
	}
}

void UnlockSlide::repos(){
	if(state == Shake){
		float t = this->t * 4;

		float x;
		static constexpr float fullDist = shakeHalfDist * 2;
		if(t < .5f){
			float t2 = t * 2.0f;
			x = shakeHalfDist * t2;
		}else if(t < 1.5f){
			float t2 = t - .5f;
			x = shakeHalfDist - fullDist * t2;
		}else if(t < 2.5f){
			float t2 = t - 1.5f;
			x = -shakeHalfDist + fullDist * t2;
		}else if(t < 3.5f){
			float t2 = t - 2.5f;
			x = shakeHalfDist - fullDist * t2;
		}else if(t < 4.f){
			float t2 = (t - 3.5f) * 2.f;
			x = -shakeHalfDist + shakeHalfDist * t2;
		}else{
			x = 0;
		}

		lv_obj_set_x(lock, round(x));
	}else{
		lv_obj_set_x(lock, round(t * (float) (lv_obj_get_width(obj) - 4)));
	}
}
