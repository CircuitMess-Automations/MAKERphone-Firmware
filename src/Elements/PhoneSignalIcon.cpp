#include "PhoneSignalIcon.h"

// MAKERphone retro palette - kept identical with every other Phone* widget
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid, PhoneDialerKey, PhoneDialerPad,
// PhonePixelAvatar, PhoneChatBubble) so a screen that mixes any of them
// reads as one coherent device UI. Duplicated rather than centralised at
// this small scale - if the palette ever moves out of the widgets, every
// widget moves together.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple background
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange ("no signal" slash)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (active bars)
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (inactive bars)

PhoneSignalIcon::PhoneSignalIcon(lv_obj_t* parent) : LVObject(parent){
	// Anchor regardless of parent layout. Same flag pattern as the other
	// Phone* atoms so callers can drop the icon into a flex / grid screen
	// and place it explicitly with lv_obj_set_pos() / lv_obj_set_align().
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, IconWidth, IconHeight);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);

	buildBars();
	buildSlash();

	// Fresh boot: searching for service. As soon as LoRa pairs the host
	// screen calls setLevel() / setRSSI() and the cycle stops.
	startScan();
}

PhoneSignalIcon::~PhoneSignalIcon(){
	// Cancel the scan timer first so it can't fire on a freed `this`
	// after we go away. The bar / slash lv_objs are children of `obj`
	// and tear themselves down via LVGL's normal child cleanup.
	stopScan();
}

// ----- builders -----

void PhoneSignalIcon::buildBars(){
	// 4 vertical bars, 2 px wide each, 1 px gap between them. Heights
	// 3 / 5 / 7 / 9 px - same proportions as the static cluster inside
	// PhoneStatusBar so the two read as the same family.
	const uint8_t heights[BarCount] = { 3, 5, 7, 9 };
	uint8_t x = 0;
	for(uint8_t i = 0; i < BarCount; i++){
		lv_obj_t* b = lv_obj_create(obj);
		lv_obj_remove_style_all(b);
		lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(b, 2, heights[i]);
		// Bottom-align bars at y = 9 (1 px above the 10 px footprint, so
		// the "no signal" slash has a clean row to live in).
		lv_obj_set_pos(b, x, 9 - heights[i]);
		lv_obj_set_style_radius(b, 0, 0);
		lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(b, MP_DIM, 0);
		bars[i] = b;
		x += 3;
	}
}

void PhoneSignalIcon::buildSlash(){
	// Single 1 px sunset-orange slash drawn under the bars when level is 0
	// and the icon is not scanning. Hidden by default; redrawBars() flips
	// it on/off depending on state.
	slash = lv_obj_create(obj);
	lv_obj_remove_style_all(slash);
	lv_obj_clear_flag(slash, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(slash, IconWidth, 1);
	lv_obj_set_pos(slash, 0, IconHeight - 1);
	lv_obj_set_style_radius(slash, 0, 0);
	lv_obj_set_style_bg_color(slash, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(slash, LV_OPA_COVER, 0);
	lv_obj_add_flag(slash, LV_OBJ_FLAG_HIDDEN);
}

// ----- redraw helpers -----

void PhoneSignalIcon::redrawBars(uint8_t activeBars){
	if(activeBars > BarCount) activeBars = BarCount;
	for(uint8_t i = 0; i < BarCount; i++){
		const bool active = (i < activeBars);
		lv_obj_set_style_bg_color(bars[i], active ? MP_HIGHLIGHT : MP_DIM, 0);
	}

	// "No signal" indicator. Visible only when we have a confirmed zero
	// level (i.e. not when scanning where activeBars also briefly = 0).
	if(slash != nullptr){
		const bool showSlash = (!scanning) && (level == 0);
		if(showSlash){
			lv_obj_clear_flag(slash, LV_OBJ_FLAG_HIDDEN);
		}else{
			lv_obj_add_flag(slash, LV_OBJ_FLAG_HIDDEN);
		}
	}
}

// ----- scan animation -----

void PhoneSignalIcon::startScan(){
	if(scanning && scanTimer != nullptr) return;
	scanning = true;
	scanStep = 0;
	// Show the first frame (1 bar) immediately so there's no perceptible
	// gap between "construct" and "first scan tick".
	redrawBars(1);
	if(scanTimer == nullptr){
		scanTimer = lv_timer_create(scanTimerCb, ScanIntervalMs, this);
	}
}

void PhoneSignalIcon::stopScan(){
	scanning = false;
	if(scanTimer != nullptr){
		lv_timer_del(scanTimer);
		scanTimer = nullptr;
	}
}

void PhoneSignalIcon::scanTimerCb(lv_timer_t* timer){
	auto* self = static_cast<PhoneSignalIcon*>(timer->user_data);
	if(self == nullptr || !self->scanning) return;
	// Cycle 1 -> 2 -> 3 -> 4 -> 1 -> ...  (skip 0 so the icon never
	// flickers fully empty during search, which would read as "broken"
	// rather than "looking for service").
	self->scanStep = (uint8_t)((self->scanStep + 1) % BarCount);
	self->redrawBars((uint8_t)(self->scanStep + 1));
}

// ----- public API -----

void PhoneSignalIcon::setLevel(uint8_t bars){
	if(bars > BarCount) bars = BarCount;
	level = bars;
	if(scanning) stopScan();
	redrawBars(level);
}

void PhoneSignalIcon::setScanning(bool on){
	if(on){
		startScan();
	}else{
		stopScan();
		// When scanning stops without a fresh setLevel(), redraw the
		// last known level so the icon doesn't get "stuck" on whatever
		// the last scan frame happened to be.
		redrawBars(level);
	}
}

void PhoneSignalIcon::setRSSI(int rssi){
	// RSSI thresholds tuned for LLCC68 LoRa at SF7 BW125 - typical of
	// the Chatter radio. -50 dBm and stronger is "right next to me",
	// -110 dBm and weaker is "barely there". Mapping is generous on
	// purpose - feature-phone signal indicators historically err on
	// the side of "looks fine" until link is genuinely poor.
	uint8_t bars;
	if(rssi >= -65)       bars = 4;
	else if(rssi >= -80)  bars = 3;
	else if(rssi >= -95)  bars = 2;
	else if(rssi >= -110) bars = 1;
	else                  bars = 0;
	setLevel(bars);
}
