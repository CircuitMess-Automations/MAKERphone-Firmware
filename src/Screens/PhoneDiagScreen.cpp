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
#include "../Services/PhoneSystemTones.h"

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
		  chimesSummary(nullptr),
		  tickTimer(nullptr) {

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildBody();
	buildChimesSummary();

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

// S245 - single-line chime catalogue footer line. First on-screen
// consumer of the structural PhoneSystemTones accessors that the
// S232 / S240 / S241 / S243 sessions added (durationMs, peakFreqHz,
// troughFreqHz, audibleNoteCount). The diag page already groups
// every read-only "what is the firmware doing right now" surface
// in one place; the chime catalogue is part of that picture (it's
// what every Save / Error / SmsReceived feedback chirp the user
// hears at runtime is sourced from), so anchoring a one-line
// summary just above the soft-key bar lets a developer triaging
// audio behaviour see the catalogue's pitch envelope at a glance
// without standing up the long-foreshadowed "Settings -> Sounds
// -> System chimes" picker. The catalogue does NOT change at
// runtime -- kMelodies is const data baked into flash -- so the
// line is computed ONCE on screen entry inside buildChimesSummary
// rather than re-derived on every refresh tick.
//
// Output format: "<N> chimes  <trough>-<peak>Hz" (e.g.
// "18 chimes  220-1568Hz"). The trough and peak are the catalogue-
// wide minimum / maximum across every audible (non-rest) note in
// every chime, derived by walking PhoneSystemTones::count() ids
// and aggregating the per-id PhoneSystemTones::troughFreqHz(id)
// and PhoneSystemTones::peakFreqHz(id) results. Both per-id
// accessors already skip rest-encoded freq == 0 entries, so the
// catalogue-wide aggregate inherits the same rest-aware semantics
// without re-walking the const Melody* pointers here.
//
// Defensive fallbacks:
//   - count() == 0  : prints "0 chimes" with no Hz range.
//   - all-rests cat : prints "<N> chimes" with no Hz range
//                     (peak / trough accumulators stayed at 0).
// Both are currently impossible (the v1 catalogue ships 18 chimes
// today, none of which use rests) but the formatter falls back
// cleanly in either case so a future v2 catalogue change can't
// regress the diag page.
//
// Layout: pixelbasic7 (7 px tall) on a single line, anchored at
// y = 106 with bodyX = 4 / bodyW = 152. The soft-key bar starts
// at y = 118 (10 px tall), so the footer sits in the previously-
// unused 14 px gap between the HEAP value (bottom ~y = 100) and
// the soft-key bar with ~5 px clear margin on either side. Color
// is MP_LABEL_DIM (the muted-purple section-label tone the body
// labels already use) so the line reads as caption-weight rather
// than competing with the cream-colored live-readout values.
void PhoneDiagScreen::buildChimesSummary() {
	const uint8_t cnt = PhoneSystemTones::count();

	// Walk the catalogue once, aggregate global trough / peak. Both
	// per-id accessors skip rest-encoded freq == 0 entries already.
	uint16_t globalPeak = 0;
	uint16_t globalTrough = 0;
	bool haveTrough = false;
	for(uint8_t i = 0; i < cnt; ++i) {
		const uint16_t pk = PhoneSystemTones::peakFreqHz(i);
		const uint16_t tr = PhoneSystemTones::troughFreqHz(i);
		if(pk != 0 && pk > globalPeak) globalPeak = pk;
		if(tr != 0) {
			if(!haveTrough || tr < globalTrough) {
				globalTrough = tr;
				haveTrough = true;
			}
		}
	}

	char buf[40];
	if(cnt == 0) {
		snprintf(buf, sizeof(buf), "0 chimes");
	} else if(haveTrough && globalPeak != 0) {
		snprintf(buf, sizeof(buf), "%u chimes  %u-%uHz",
				 (unsigned) cnt,
				 (unsigned) globalTrough,
				 (unsigned) globalPeak);
	} else {
		// All-rests catalogue (currently impossible) - print the
		// count without a Hz range so the line is still meaningful.
		snprintf(buf, sizeof(buf), "%u chimes", (unsigned) cnt);
	}

	chimesSummary = lv_label_create(obj);
	lv_obj_set_style_text_font(chimesSummary, &pixelbasic7, 0);
	lv_obj_set_style_text_color(chimesSummary, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(chimesSummary, LV_LABEL_LONG_DOT);
	lv_obj_set_width(chimesSummary, kBodyW);
	lv_label_set_text(chimesSummary, buf);
	lv_obj_set_pos(chimesSummary, kBodyX, 106);
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
