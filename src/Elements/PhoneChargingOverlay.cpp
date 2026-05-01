#include "PhoneChargingOverlay.h"
#include "../Fonts/font.h"
#include <Loop/LoopManager.h>
#include <Chatter.h>

// MAKERphone retro palette - kept identical with every other Phone* widget
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid, PhoneDialerKey, PhoneDialerPad,
// PhonePixelAvatar, PhoneChatBubble, PhoneSignalIcon, PhoneBatteryIcon,
// PhoneNotificationToast, PhoneLockHint, PhoneNotificationPreview).
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (bolt bright)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (border, percent label)
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (chip bg, bolt shadow)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream (Charging caption)


PhoneChargingOverlay::PhoneChargingOverlay(lv_obj_t* parent) : LVObject(parent){
	for(uint8_t i = 0; i < 3; i++){
		boltSegments[i] = nullptr;
		boltOutline[i]  = nullptr;
	}
	for(uint8_t i = 0; i < TrendSamples; i++){
		trendBuf[i] = 0;
	}

	// Anchor independently of any flex/column layout the host screen uses.
	// Positioned by the host with set_align/set_pos after construction.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, ChipWidth, ChipHeight);

	// Transparent slab - the synthwave wallpaper / homescreen wallpaper
	// shows through the rounded background of the inner pill.
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);

	buildPill();
	buildBolt();
	buildLabels();

	// Default state: hidden until the host (or auto-detect) flips it on.
	applyVisibility(false);
	redrawBolt();

	LoopManager::addListener(this);
}

PhoneChargingOverlay::~PhoneChargingOverlay(){
	LoopManager::removeListener(this);
}

// ----- builders -----

void PhoneChargingOverlay::buildPill(){
	// Rounded rectangle backdrop. Semi-transparent so the wallpaper still
	// reads through but the labels sit on a dimmed slab for contrast.
	pill = lv_obj_create(obj);
	lv_obj_remove_style_all(pill);
	lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(pill, ChipWidth, ChipHeight);
	lv_obj_set_pos(pill, 0, 0);
	lv_obj_set_style_radius(pill, 4, 0);
	lv_obj_set_style_bg_color(pill, MP_DIM, 0);
	lv_obj_set_style_bg_opa(pill, LV_OPA_70, 0);
	lv_obj_set_style_border_color(pill, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(pill, 1, 0);
	lv_obj_set_style_border_opa(pill, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(pill, 0, 0);
}

void PhoneChargingOverlay::buildBolt(){
	// Three stacked rectangles approximating an angular lightning bolt.
	// The "outline" rectangles are 1 px larger and offset 1 px down/right
	// to give the bolt a tiny shadow plate that reads as embossing on
	// the dim pill bg. Both layers live as children of `pill` so their
	// positions are relative to the pill's inner edge.
	//
	//      .##.            <- top diagonal:    (3,2) 4x2
	//      .##.            <- mid step:        (4,3) 3x2
	//      .##.            <- bottom diagonal: (2,5) 4x2
	//
	// Total 9x9 bolt occupying x=2..10, y=2..10 inside the 13 px tall pill.
	struct Seg { int16_t x; int16_t y; int16_t w; int16_t h; };
	const Seg bolt[3] = {
		{ 5, 2, 3, 3 },
		{ 4, 4, 3, 3 },
		{ 3, 6, 3, 4 }
	};

	for(uint8_t i = 0; i < 3; i++){
		// Shadow plate - sits 1 px down/right of the bright segment.
		boltOutline[i] = lv_obj_create(pill);
		lv_obj_remove_style_all(boltOutline[i]);
		lv_obj_clear_flag(boltOutline[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(boltOutline[i], bolt[i].w, bolt[i].h);
		lv_obj_set_pos(boltOutline[i], bolt[i].x + 1, bolt[i].y + 1);
		lv_obj_set_style_radius(boltOutline[i], 0, 0);
		lv_obj_set_style_bg_color(boltOutline[i], MP_BG_DARK, 0);
		lv_obj_set_style_bg_opa(boltOutline[i], LV_OPA_60, 0);

		// Bright segment - colour cycles between MP_ACCENT and MP_HIGHLIGHT.
		boltSegments[i] = lv_obj_create(pill);
		lv_obj_remove_style_all(boltSegments[i]);
		lv_obj_clear_flag(boltSegments[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(boltSegments[i], bolt[i].w, bolt[i].h);
		lv_obj_set_pos(boltSegments[i], bolt[i].x, bolt[i].y);
		lv_obj_set_style_radius(boltSegments[i], 0, 0);
		lv_obj_set_style_bg_color(boltSegments[i], MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(boltSegments[i], LV_OPA_COVER, 0);
	}
}

void PhoneChargingOverlay::buildLabels(){
	// "Charging" caption sits to the right of the bolt. Pixelbasic7 cream
	// with full opacity. Anchored explicitly with set_pos so the label
	// alignment is independent of the pill's flex defaults.
	labelTitle = lv_label_create(pill);
	lv_label_set_text(labelTitle, "Charging");
	lv_obj_set_style_text_font(labelTitle, &pixelbasic7, 0);
	lv_obj_set_style_text_color(labelTitle, MP_TEXT, 0);
	lv_obj_set_style_text_opa(labelTitle, LV_OPA_COVER, 0);
	lv_obj_set_pos(labelTitle, 16, 3);

	// Percentage label is right-aligned within the pill. Cyan to match the
	// border so the eye reads "this number belongs to the chip".
	labelPercent = lv_label_create(pill);
	lv_label_set_text(labelPercent, "--%");
	lv_obj_set_style_text_font(labelPercent, &pixelbasic7, 0);
	lv_obj_set_style_text_color(labelPercent, MP_HIGHLIGHT, 0);
	lv_obj_set_style_text_opa(labelPercent, LV_OPA_COVER, 0);
	lv_obj_set_align(labelPercent, LV_ALIGN_RIGHT_MID);
	lv_obj_set_x(labelPercent, -4);
}

// ----- redraw helpers -----

void PhoneChargingOverlay::redrawBolt(){
	const lv_color_t bright = boltBright ? MP_HIGHLIGHT : MP_ACCENT;
	for(uint8_t i = 0; i < 3; i++){
		if(!boltSegments[i]) continue;
		lv_obj_set_style_bg_color(boltSegments[i], bright, 0);
	}
}

void PhoneChargingOverlay::refreshPercentLabel(){
	if(!labelPercent) return;

	uint8_t p = Battery.getPercentage();
	if(p > 100) p = 100;
	if(p == lastPercent) return;
	lastPercent = p;

	char buf[8];
	snprintf(buf, sizeof(buf), "%u%%", (unsigned) p);
	lv_label_set_text(labelPercent, buf);
}

void PhoneChargingOverlay::applyVisibility(bool show){
	if(show){
		lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	}
}

// ----- public API -----

void PhoneChargingOverlay::setCharging(bool on){
	if(charging == on){
		// Even on no-op, refresh the manual guard window if the caller
		// is asserting "off" so the auto-detect path can't immediately
		// override the host's intent.
		if(!on){
			manualGuardUntil = millis() + ManualGuardMs;
		}
		return;
	}

	charging = on;
	manualGuardUntil = millis() + ManualGuardMs;

	if(on){
		// Reset the bolt animation so the chip starts on a fresh frame
		// instead of inheriting whatever the previous run finished on.
		boltBright = false;
		lastBoltMs = millis();
		// Force the percent label to repaint on the next loop tick.
		lastPercent = 0xFF;
		redrawBolt();
		refreshPercentLabel();
	}

	applyVisibility(on);
}

void PhoneChargingOverlay::setAutoDetect(bool on){
	autoDetect = on;
	if(!on){
		// Drop the trend buffer so the next enable starts from a clean
		// sample window.
		trendCount = 0;
		trendHead  = 0;
	}else{
		// Seed the timing so we don't sample on the very first loop
		// tick with a tiny dt against millis()=0.
		lastSampleMs = millis();
		lastRiseMs   = lastSampleMs;
	}
}

// ----- LoopListener -----

void PhoneChargingOverlay::loop(uint micros){
	const uint32_t now = millis();

	// Bolt animation: cycle the segment colour every BoltStepMs while
	// the chip is visible. Skip the redraw work entirely while hidden
	// to keep the idle screen calm.
	if(charging){
		if(now - lastBoltMs >= BoltStepMs){
			lastBoltMs = now;
			boltBright = !boltBright;
			redrawBolt();
		}
		if(now - lastPercentMs >= PercentRefreshMs){
			lastPercentMs = now;
			refreshPercentLabel();
		}
	}

	// Auto-detect heuristic: sample voltage every TrendSampleMs and
	// flip charging state based on the rolling slope. Always runs
	// regardless of visibility so the chip can come ON automatically.
	if(autoDetect){
		if(now - lastSampleMs >= TrendSampleMs){
			lastSampleMs = now;
			sampleTrend(now);

			const bool guard = (now < manualGuardUntil);
			if(guard) return;

			const bool trendCharging = evaluateTrend();
			if(trendCharging){
				lastRiseMs = now;
				if(!charging){
					charging = true;
					boltBright = false;
					lastBoltMs = now;
					lastPercent = 0xFF;
					redrawBolt();
					refreshPercentLabel();
					applyVisibility(true);
				}
			}else{
				// Trend has gone flat or negative. Leave the chip on for
				// AutoDetectStopMs of "no rise" before hiding so brief
				// dips during a charge cycle don't blink the chip off.
				if(charging && (now - lastRiseMs) >= AutoDetectStopMs){
					charging = false;
					applyVisibility(false);
				}
			}
		}
	}
}

// ----- voltage trend ring buffer -----

void PhoneChargingOverlay::sampleTrend(uint32_t /*nowMs*/){
	const uint16_t v = Battery.getVoltage();
	trendBuf[trendHead] = v;
	trendHead = (uint8_t)((trendHead + 1) % TrendSamples);
	if(trendCount < TrendSamples) trendCount++;
}

bool PhoneChargingOverlay::evaluateTrend() const {
	// Need a full window to commit. A partial window can only ever
	// flip the chip OFF (handled by the AutoDetectStopMs path); the
	// commitment to ON deliberately requires confidence.
	if(trendCount < TrendSamples) return false;

	// Oldest sample is the one that lives at trendHead (after wrap),
	// newest is one slot back. Walk both sides to compute slope.
	const uint8_t newestIdx = (uint8_t)((trendHead + TrendSamples - 1) % TrendSamples);
	const uint8_t oldestIdx = trendHead; // ring buffer wraps to oldest

	const int32_t newest = (int32_t) trendBuf[newestIdx];
	const int32_t oldest = (int32_t) trendBuf[oldestIdx];
	const int32_t delta  = newest - oldest;

	return delta >= AutoDetectMv;
}
