#include "PhoneDiagScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneLvglCost.h"
#include "../Services/PhoneIdleDim.h"

// MAKERphone retro palette - inlined per the established pattern in
// this codebase (see PhoneAboutScreen.cpp, PhoneFirmwareInfoScreen.cpp).
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan caption
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream value
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple section label

// Body geometry mirrors PhoneAboutScreen so the two read-only diag
// pages feel like the same "family" of pages (same column widths,
// same group height, same y-offsets for the label-value pair).
static constexpr lv_coord_t kBodyX           = 4;
static constexpr lv_coord_t kBodyW           = 152;
static constexpr lv_coord_t kGroupTopY       = 24;
static constexpr lv_coord_t kGroupHeight     = 20;
static constexpr lv_coord_t kLabelOffset     = 0;
static constexpr lv_coord_t kValueOffset     = 9;

PhoneDiagScreen::PhoneDiagScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  cadenceLabel(nullptr),
		  cadenceValue(nullptr),
		  peakLabel(nullptr),
		  peakValue(nullptr),
		  dimLabel(nullptr),
		  dimValue(nullptr),
		  heapLabel(nullptr),
		  heapValue(nullptr),
		  tickTimer(nullptr) {

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildBody();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");

	refreshLiveFields();
}

PhoneDiagScreen::~PhoneDiagScreen() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneDiagScreen::onStart() {
	Input::getInstance()->addListener(this);

	if(tickTimer == nullptr) {
		tickTimer = lv_timer_create(onTickTimer, kRefreshPeriodMs, this);
	}
	refreshLiveFields();
}

void PhoneDiagScreen::onStop() {
	Input::getInstance()->removeListener(this);
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneDiagScreen::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "DIAG");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneDiagScreen::buildBody() {
	auto makePair = [this](lv_obj_t** outLabel, lv_obj_t** outValue,
						   const char* labelText, lv_coord_t rowIdx) {
		const lv_coord_t y = kGroupTopY + rowIdx * kGroupHeight;

		lv_obj_t* lab = lv_label_create(obj);
		lv_obj_set_style_text_font(lab, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lab, MP_LABEL_DIM, 0);
		lv_label_set_text(lab, labelText);
		lv_obj_set_pos(lab, kBodyX, y + kLabelOffset);

		lv_obj_t* val = lv_label_create(obj);
		lv_obj_set_style_text_font(val, &pixelbasic7, 0);
		lv_obj_set_style_text_color(val, MP_TEXT, 0);
		lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
		lv_obj_set_width(val, kBodyW);
		lv_label_set_text(val, "");
		lv_obj_set_pos(val, kBodyX, y + kValueOffset);

		*outLabel = lab;
		*outValue = val;
	};

	makePair(&cadenceLabel, &cadenceValue, "LOOP CADENCE", 0);
	makePair(&peakLabel,    &peakValue,    "PEAK / TOTAL", 1);
	makePair(&dimLabel,     &dimValue,     "IDLE DIM",     2);
	makePair(&heapLabel,    &heapValue,    "HEAP",         3);
}

void PhoneDiagScreen::refreshLiveFields() {
	// LOOP CADENCE: "60 fps  avg 16678 us"
	if(cadenceValue != nullptr) {
		const uint16_t fps = LvglCost.loopsPerSec();
		const uint32_t avgUs = LvglCost.avgUs();
		char buf[40];
		snprintf(buf, sizeof(buf), "%u fps  avg %lu us",
				 (unsigned) fps, (unsigned long) avgUs);
		lv_label_set_text(cadenceValue, buf);
	}

	// PEAK / TOTAL: "peak 23410 us   1234567 ticks"
	if(peakValue != nullptr) {
		const uint32_t peakUs = LvglCost.peakUs();
		const uint32_t totalLoops = LvglCost.totalLoopsSinceBoot();
		char buf[48];
		// Trim total loops to a 7-digit cap so the label width stays
		// deterministic on devices that have been up for hours -
		// 9999999 ticks at ~60 fps is roughly 46 hours of uptime,
		// which is well past any realistic single-run debug session.
		uint32_t totalCapped = totalLoops > 9999999u ? 9999999u : totalLoops;
		snprintf(buf, sizeof(buf), "peak %lu us  %lu t",
				 (unsigned long) peakUs, (unsigned long) totalCapped);
		lv_label_set_text(peakValue, buf);
	}

	// IDLE DIM: "BRIGHT  bl 192   idle 00:00:04"
	if(dimValue != nullptr) {
		const PhoneIdleDim::Stage stage = IdleDim.stage();
		const uint8_t bl = IdleDim.lastAppliedBrightness();
		const uint32_t idleMs = IdleDim.msSinceActivity();
		char idleBuf[10];
		formatIdleClock(idleMs, idleBuf, sizeof(idleBuf));
		char buf[48];
		snprintf(buf, sizeof(buf), "%s bl %u %s",
				 formatStage((uint8_t) stage),
				 (unsigned) bl,
				 idleBuf);
		lv_label_set_text(dimValue, buf);
	}

	// HEAP: "142 KB free"
	if(heapValue != nullptr) {
		const uint32_t kb = (uint32_t)(ESP.getFreeHeap() / 1024u);
		char buf[24];
		snprintf(buf, sizeof(buf), "%lu KB free", (unsigned long) kb);
		lv_label_set_text(heapValue, buf);
	}
}

void PhoneDiagScreen::onTickTimer(lv_timer_t* timer) {
	auto* self = static_cast<PhoneDiagScreen*>(timer->user_data);
	if(self == nullptr) return;
	self->refreshLiveFields();
}

void PhoneDiagScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_BACK:
		case BTN_ENTER:
			if(softKeys) softKeys->flashRight();
			pop();
			break;
		default:
			break;
	}
}

const char* PhoneDiagScreen::formatStage(uint8_t stageU8) {
	switch(stageU8) {
		case (uint8_t) PhoneIdleDim::Stage::Bright:  return "BRIGHT";
		case (uint8_t) PhoneIdleDim::Stage::Dim:     return "DIM";
		case (uint8_t) PhoneIdleDim::Stage::DeepDim: return "DEEP";
		default:                                     return "?";
	}
}

void PhoneDiagScreen::formatIdleClock(uint32_t ms, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	uint32_t seconds = ms / 1000u;
	const uint32_t maxSec = 99u * 3600u + 59u * 60u + 59u;  // 99:59:59
	if(seconds > maxSec) seconds = maxSec;
	const uint32_t h = seconds / 3600u;
	const uint32_t m = (seconds / 60u) % 60u;
	const uint32_t s = seconds % 60u;
	snprintf(out, outLen, "%02u:%02u:%02u",
			 (unsigned) h, (unsigned) m, (unsigned) s);
}
