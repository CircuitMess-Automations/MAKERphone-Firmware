#include "PhoneEqualizerVisualiser.h"
#include "../Services/PhoneRingtoneEngine.h"

// MAKERphone retro palette — kept identical with every other Phone* widget
// so the visualiser reads as part of the same coherent device UI when it
// drops into PhoneMusicPlayer (and any future audio screen). Inlined per
// the established pattern (see PhoneSignalIcon.cpp / PhoneStatusBar.cpp).
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple background
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange (tallest bars)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan (medium bars)
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple (baseline + idle)

PhoneEqualizerVisualiser::PhoneEqualizerVisualiser(lv_obj_t* parent)
		: LVObject(parent) {
	// Anchor regardless of parent layout. Same flag pattern every other
	// Phone* atom uses — drop the widget into a flex / grid screen and
	// pin its position with lv_obj_set_pos() / lv_obj_set_align().
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, WidgetWidth, WidgetHeight);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

	for(uint8_t i = 0; i < BarCount; ++i){
		heights[i] = MinHeight;
		targets[i] = MinHeight;
	}

	buildBars();
	redrawBars();

	// Self-driving timer: 60 ms tick is fast enough that the bars feel
	// responsive without burning frame-budget on a 160x128 panel. The
	// timer is torn down in the destructor so a popped screen never
	// leaks a callback into freed memory.
	tickTimer = lv_timer_create(tickCb, TickPeriodMs, this);
}

PhoneEqualizerVisualiser::~PhoneEqualizerVisualiser() {
	if(tickTimer != nullptr){
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
	// `bars[]` are children of `obj` — LVGL frees them recursively when
	// `obj` is destroyed by the LVObject base destructor. Nothing manual.
}

void PhoneEqualizerVisualiser::setActive(bool on) {
	active = on;
	// We do NOT eagerly set heights here — letting the chase loop ease
	// the bars to the new target reads more natural than a sudden snap
	// to baseline when the user pauses.
}

// ---------------------------------------------------------------------------
// builders
// ---------------------------------------------------------------------------

void PhoneEqualizerVisualiser::buildBars() {
	uint16_t x = 0;
	for(uint8_t i = 0; i < BarCount; ++i){
		lv_obj_t* b = lv_obj_create(obj);
		lv_obj_remove_style_all(b);
		lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(b, BarWidth, MinHeight);
		// Bottom-align: each bar sits at y = WidgetHeight - height. The
		// initial frame is a 1 px stripe at the bottom of the widget.
		lv_obj_set_pos(b, x, WidgetHeight - MinHeight);
		lv_obj_set_style_radius(b, 1, 0);
		lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(b, MP_DIM, 0);
		lv_obj_set_style_border_width(b, 0, 0);
		lv_obj_set_style_pad_all(b, 0, 0);
		bars[i] = b;
		x += BarWidth + BarGap;
	}
}

// ---------------------------------------------------------------------------
// chase / draw
// ---------------------------------------------------------------------------

void PhoneEqualizerVisualiser::redrawBars() {
	const uint8_t topTier = (uint8_t)((WidgetHeight * 2) / 3);
	const uint8_t midTier = (uint8_t)(WidgetHeight / 3);

	for(uint8_t i = 0; i < BarCount; ++i){
		uint8_t h = heights[i];
		if(h < MinHeight)         h = MinHeight;
		if(h > WidgetHeight)      h = WidgetHeight;

		// LVGL trims a child to its requested size — set width too in
		// case a previous frame zeroed it during a clamp.
		lv_obj_set_size(bars[i], BarWidth, h);
		lv_obj_set_y(bars[i], (lv_coord_t)(WidgetHeight - h));

		lv_color_t c;
		if(!active){
			c = MP_DIM;
		}else if(h >= topTier){
			c = MP_ACCENT;
		}else if(h >= midTier){
			c = MP_HIGHLIGHT;
		}else{
			c = MP_DIM;
		}
		lv_obj_set_style_bg_color(bars[i], c, 0);
	}
}

void PhoneEqualizerVisualiser::computeTargets() {
	const bool engineLive = active && Ringtone.isPlaying();
	if(!engineLive){
		// Idle / paused: bars decay all the way down to the baseline.
		for(uint8_t i = 0; i < BarCount; ++i){
			targets[i] = MinHeight;
		}
		return;
	}

	const uint16_t freq = Ringtone.currentFreq();
	if(freq == 0){
		// Mid-melody rest or inter-note gap: bars settle just above the
		// baseline so the visualiser feels "warm" rather than dead silent.
		for(uint8_t i = 0; i < BarCount; ++i){
			targets[i] = (uint8_t)(MinHeight + 1);
		}
		return;
	}

	// Map the active note frequency to a per-bar pseudo-spectrum height.
	// Lower bars (i = 0..2) react more slowly to keep "bass" feel; upper
	// bars (i >= 4) wobble more aggressively so the visual reads like a
	// classic 7-band equalizer rather than a single uniform blob.
	const uint8_t span = (uint8_t)(WidgetHeight - MinHeight);
	for(uint8_t i = 0; i < BarCount; ++i){
		const uint32_t mix = (uint32_t) freq * (uint32_t)(i + 3)
							 + (uint32_t) tickCounter * 17UL
							 + (uint32_t) i * 41UL;
		const uint8_t pseudo = (uint8_t)((mix >> 4) & 0x7F);  // 0..127
		uint16_t h = (uint16_t) MinHeight
					 + ((uint16_t) pseudo * (uint16_t) span) / 127U;

		// Bias: bass bars cap a touch lower, treble bars get a bonus —
		// keeps the visual silhouette interesting even on a single-tone
		// melody where every bar would otherwise share the same target.
		if(i <= 1){
			if(h > MinHeight + 1) h -= 1;
		}else if(i >= 5){
			h += 1;
			if(h > WidgetHeight) h = WidgetHeight;
		}

		targets[i] = (uint8_t) h;
	}
}

void PhoneEqualizerVisualiser::tickCb(lv_timer_t* timer) {
	auto* self = static_cast<PhoneEqualizerVisualiser*>(timer->user_data);
	if(self == nullptr) return;

	self->tickCounter++;
	self->computeTargets();

	// Asymmetric easing: bars snap up quickly (note onset feels punchy)
	// and fall slowly (gravity / VU-meter feel).
	bool changed = false;
	for(uint8_t i = 0; i < PhoneEqualizerVisualiser::BarCount; ++i){
		const uint8_t cur = self->heights[i];
		const uint8_t tgt = self->targets[i];
		if(cur < tgt){
			// Half the gap, plus 1 — guarantees we always reach the
			// target rather than asymptote forever.
			uint16_t step = (uint16_t)(tgt - cur);
			step = (step + 1) / 2;
			if(step < 1) step = 1;
			uint16_t next = (uint16_t) cur + step;
			if(next > tgt) next = tgt;
			self->heights[i] = (uint8_t) next;
			changed = true;
		}else if(cur > tgt){
			// Linear decay — 1 px per tick. At 60 ms tick that's
			// 14 px / 60 ms ≈ ~840 ms of visible fall from the top
			// to the baseline, which reads as "musical decay" rather
			// than an instant snap-off when a melody ends.
			self->heights[i] = (uint8_t)(cur - 1);
			changed = true;
		}
	}

	if(changed) self->redrawBars();
}
