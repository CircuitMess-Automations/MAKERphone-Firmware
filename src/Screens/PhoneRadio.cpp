#include "PhoneRadio.h"

#include <stdio.h>
#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette — inlined per the established pattern in this
// codebase (see PhoneMusicPlayer.cpp / PhoneMainMenu.cpp / PhoneDialerScreen.cpp).
// Cyan headlines the focused frequency, sunset orange marks the active
// station and the dial cursor, dim purple draws the inactive scale, warm
// cream renders the tagline.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// =====================================================================
// Pre-canned looping melodies, one per station. Frequencies clamped to
// the 200..2200 Hz comfortable piezo band (matches PhoneMusicLibrary).
// All eight loop forever — until the user tunes away or hits STOP.
// =====================================================================

// 0. Retro 88 — synthwave throwback (slow A-minor pulse arp).
static const PhoneRingtoneEngine::Note kStation0Notes[] = {
	{ 220, 180 },  // A3
	{ 440, 180 },  // A4
	{ 659, 180 },  // E5
	{ 880, 240 },  // A5
	{   0,  60 },
	{ 784, 180 },  // G5
	{ 659, 180 },  // E5
	{ 523, 240 },  // C5
	{   0,  80 },
};
static const PhoneRingtoneEngine::Melody kStation0 = {
	kStation0Notes,
	(uint16_t)(sizeof(kStation0Notes) / sizeof(kStation0Notes[0])),
	15, true, "Retro 88"
};

// 1. Beat FM — drum-style bass pulse with rim hits.
static const PhoneRingtoneEngine::Note kStation1Notes[] = {
	{ 220,  80 }, {   0,  60 },
	{ 220,  80 }, {   0,  60 },
	{ 1760, 50 }, {   0, 100 },
	{ 220,  80 }, {   0,  60 },
	{ 220,  80 }, {   0,  60 },
	{ 1760, 50 }, {   0, 200 },
};
static const PhoneRingtoneEngine::Melody kStation1 = {
	kStation1Notes,
	(uint16_t)(sizeof(kStation1Notes) / sizeof(kStation1Notes[0])),
	0, true, "Beat FM"
};

// 2. Arcade 95 — bright 8-bit hit flourish.
static const PhoneRingtoneEngine::Note kStation2Notes[] = {
	{ 523, 100 },  // C5
	{ 659, 100 },  // E5
	{ 784, 100 },  // G5
	{ 1046, 100 }, // C6
	{ 1318, 200 }, // E6
	{   0,  60 },
	{ 1046, 100 },
	{ 784, 100 },
	{ 659, 100 },
	{ 523, 200 },
	{   0, 200 },
};
static const PhoneRingtoneEngine::Melody kStation2 = {
	kStation2Notes,
	(uint16_t)(sizeof(kStation2Notes) / sizeof(kStation2Notes[0])),
	10, true, "Arcade 95"
};

// 3. Neon 99 — synthpop hook over a walking bass.
static const PhoneRingtoneEngine::Note kStation3Notes[] = {
	{ 659, 160 }, // E5
	{ 880, 160 }, // A5
	{ 988, 160 }, // B5
	{ 1046, 240 },// C6
	{   0,  40 },
	{ 880, 160 },
	{ 784, 160 }, // G5
	{ 659, 240 },
	{   0,  40 },
	{ 587, 160 }, // D5
	{ 659, 160 },
	{ 784, 240 },
	{   0, 120 },
};
static const PhoneRingtoneEngine::Melody kStation3 = {
	kStation3Notes,
	(uint16_t)(sizeof(kStation3Notes) / sizeof(kStation3Notes[0])),
	18, true, "Neon 99"
};

// 4. Crystal FM — slow, dreamy chillwave.
static const PhoneRingtoneEngine::Note kStation4Notes[] = {
	{ 523, 320 }, // C5
	{ 659, 320 }, // E5
	{ 784, 320 }, // G5
	{ 1046, 480 },// C6
	{   0,  80 },
	{ 880, 320 }, // A5
	{ 698, 320 }, // F5
	{ 523, 480 }, // C5
	{   0, 200 },
};
static const PhoneRingtoneEngine::Melody kStation4 = {
	kStation4Notes,
	(uint16_t)(sizeof(kStation4Notes) / sizeof(kStation4Notes[0])),
	30, true, "Crystal FM"
};

// 5. Power 104 — fast high-energy techno bassline + pings.
static const PhoneRingtoneEngine::Note kStation5Notes[] = {
	{ 220,  60 }, {   0,  20 },
	{ 220,  60 }, {   0,  20 },
	{ 1568, 50 }, {   0,  20 },  // G6 ping
	{ 220,  60 }, {   0,  20 },
	{ 1760, 50 }, {   0,  20 },  // A6 ping
	{ 220,  60 }, {   0,  20 },
	{ 1976, 50 }, {   0,  20 },  // B6 ping
	{ 220,  60 }, {   0,  20 },
	{ 2093, 80 }, {   0, 100 },  // C7 ping (long)
};
static const PhoneRingtoneEngine::Melody kStation5 = {
	kStation5Notes,
	(uint16_t)(sizeof(kStation5Notes) / sizeof(kStation5Notes[0])),
	0, true, "Power 104"
};

// 6. Drift 106 — slow lo-fi cruise melody (descending).
static const PhoneRingtoneEngine::Note kStation6Notes[] = {
	{ 880, 240 }, // A5
	{ 784, 240 }, // G5
	{ 698, 240 }, // F5
	{ 659, 240 }, // E5
	{ 587, 240 }, // D5
	{ 523, 360 }, // C5
	{   0, 240 },
};
static const PhoneRingtoneEngine::Melody kStation6 = {
	kStation6Notes,
	(uint16_t)(sizeof(kStation6Notes) / sizeof(kStation6Notes[0])),
	25, true, "Drift 106"
};

// 7. Static 108 — between-stations static. Pseudo-random short clicks
// across a wide frequency span, plus deliberate rests so the listener
// hears a "hiss / crackle" rather than a melody.
static const PhoneRingtoneEngine::Note kStation7Notes[] = {
	{  300,  20 }, {    0,  40 },
	{ 1900,  20 }, {    0,  60 },
	{  500,  20 }, {    0,  40 },
	{ 1200,  20 }, {    0,  80 },
	{  800,  20 }, {    0,  40 },
	{ 1700,  20 }, {    0,  60 },
	{  400,  20 }, {    0,  40 },
	{ 2100,  20 }, {    0, 120 },
};
static const PhoneRingtoneEngine::Melody kStation7 = {
	kStation7Notes,
	(uint16_t)(sizeof(kStation7Notes) / sizeof(kStation7Notes[0])),
	0, true, "Static 108"
};

// =====================================================================
// Station table — index-aligned with the docstring above. Frequencies
// are stored as MHz * 10 (uint16_t) so we can pretty-print "%u.%u FM"
// with no float math, mirroring the rest of the codebase's "deci-X"
// pattern (see Settings.brightness, BatteryService percentage, etc.).
// =====================================================================
struct StationDef {
	uint16_t freqDeci;     // e.g. 885 == 88.5 MHz
	const char* name;      // call-sign
	const char* tagline;   // one-line description
	const PhoneRingtoneEngine::Melody* melody;
};

static const StationDef kStations[PhoneRadio::StationCount] = {
	{  885, "RETRO 88",    "Synthwave Classics", &kStation0 },
	{  923, "BEAT FM",     "Drum & Pulse",       &kStation1 },
	{  957, "ARCADE 95",   "8-bit Hits",         &kStation2 },
	{  991, "NEON 99",     "Synthpop Hour",      &kStation3 },
	{ 1015, "CRYSTAL FM",  "Chillwave",          &kStation4 },
	{ 1043, "POWER 104",   "High Energy",        &kStation5 },
	{ 1067, "DRIFT 106",   "Lo-Fi Cruise",       &kStation6 },
	{ 1080, "STATIC 108",  "Pure Noise",         &kStation7 },
};

// =====================================================================
// Static accessors — handy for unit-test introspection and anybody
// who wants to read the station table without owning a screen.
// =====================================================================

const char* PhoneRadio::stationName(uint8_t index) {
	return kStations[index % StationCount].name;
}

const char* PhoneRadio::stationTagline(uint8_t index) {
	return kStations[index % StationCount].tagline;
}

uint16_t PhoneRadio::stationFreqDeci(uint8_t index) {
	return kStations[index % StationCount].freqDeci;
}

const PhoneRingtoneEngine::Melody* PhoneRadio::stationMelody(uint8_t index) {
	return kStations[index % StationCount].melody;
}

// =====================================================================
// Lifecycle
// =====================================================================

PhoneRadio::PhoneRadio()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  scaleLeftLabel(nullptr),
		  scaleRightLabel(nullptr),
		  dialTrack(nullptr),
		  dialCursor(nullptr),
		  freqLabel(nullptr),
		  stationLabel(nullptr),
		  taglineLabel(nullptr),
		  statusPill(nullptr),
		  statusLabel(nullptr),
		  stationIndex(3),   // start tuned to NEON 99 — the "default" station
		  playing(false) {

	for(uint8_t i = 0; i < StationCount; ++i){
		dialTicks[i] = nullptr;
	}

	// Full-screen container, no scrollbars, no inner padding — same blank
	// canvas pattern PhoneMusicPlayer / PhoneSoftKeyToneScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildDial();
	buildReadout();
	buildStatus();

	// Bottom: feature-phone soft-keys. Left toggles play/stop based on
	// the engine state; right is the universal EXIT.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->set("PLAY", "EXIT");

	// Initial UI state — tuned to NEON 99, paused, cursor pinned over
	// the right tick. We do NOT auto-play on construction so the screen
	// does not start emitting tones the moment it is pushed; the user
	// has to press PLAY (or ENTER) to go on air.
	refreshFrequency();
	refreshStation();
	refreshCursor();
	refreshStatus();
}

PhoneRadio::~PhoneRadio() {
	// Defensive: stop any outstanding ringtone playback. The onStop()
	// hook does the same thing, but covering both paths keeps us safe
	// if the screen is destroyed while never started.
	if(playing){
		Ringtone.stop();
		playing = false;
	}
	// Children (wallpaper, statusBar, softKeys, labels, ticks) are all
	// parented to obj — LVGL frees them recursively when the screen's
	// obj is destroyed by the LVScreen base destructor. Nothing manual.
}

void PhoneRadio::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneRadio::onStop() {
	Input::getInstance()->removeListener(this);

	// Leaving the screen always silences the piezo so the radio can
	// never leak a buzzing tone into a parent screen.
	if(playing){
		Ringtone.stop();
		playing = false;
	}
}

// =====================================================================
// Builders
// =====================================================================

void PhoneRadio::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "FM RADIO");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, CaptionY);
}

void PhoneRadio::buildDial() {
	// "88" / "108" scale labels at the dial endpoints.
	scaleLeftLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(scaleLeftLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(scaleLeftLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(scaleLeftLabel, "88");
	lv_obj_set_pos(scaleLeftLabel, DialLeft - 10, DialY - 5);

	scaleRightLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(scaleRightLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(scaleRightLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(scaleRightLabel, "108");
	lv_obj_set_pos(scaleRightLabel, DialRight + 1, DialY - 5);

	// Horizontal dial backbone — 1 px tall dim line spanning the band.
	dialTrack = lv_obj_create(obj);
	lv_obj_remove_style_all(dialTrack);
	lv_obj_set_size(dialTrack, DialRight - DialLeft + 1, 1);
	lv_obj_set_pos(dialTrack, DialLeft, DialY + 3);
	lv_obj_clear_flag(dialTrack, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(dialTrack, MP_DIM, 0);
	lv_obj_set_style_bg_opa(dialTrack, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(dialTrack, 0, 0);

	// 8 tick marks evenly spaced along the dial. Each tick is a thin
	// 1 x 4 sunset-orange rectangle so the eye can read the dial as a
	// proper analog scale even when the cursor is on a different tick.
	const lv_coord_t span = DialRight - DialLeft;
	for(uint8_t i = 0; i < StationCount; ++i){
		dialTicks[i] = lv_obj_create(obj);
		lv_obj_remove_style_all(dialTicks[i]);
		lv_obj_set_size(dialTicks[i], 1, 4);
		const lv_coord_t x = DialLeft + (lv_coord_t)((int32_t) span * i / (StationCount - 1));
		lv_obj_set_pos(dialTicks[i], x, DialY + 1);
		lv_obj_clear_flag(dialTicks[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_bg_color(dialTicks[i], MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(dialTicks[i], LV_OPA_70, 0);
		lv_obj_set_style_border_width(dialTicks[i], 0, 0);
	}

	// Tuning cursor — a small bright sunset-orange "needle" sliding
	// along the dial. Tall enough to span the tick row + horizontal
	// line so the eye reads it as a snap-to-tick indicator.
	dialCursor = lv_obj_create(obj);
	lv_obj_remove_style_all(dialCursor);
	lv_obj_set_size(dialCursor, 3, 8);
	lv_obj_set_pos(dialCursor, DialLeft, DialY);
	lv_obj_clear_flag(dialCursor, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(dialCursor, 1, 0);
	lv_obj_set_style_bg_color(dialCursor, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(dialCursor, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(dialCursor, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(dialCursor, 1, 0);
	lv_obj_set_style_border_opa(dialCursor, LV_OPA_70, 0);
	lv_obj_move_foreground(dialCursor);
}

void PhoneRadio::buildReadout() {
	// Big cyan "99.1 FM" frequency. pixelbasic16 gives the screen its
	// "you tuned to a station" headline.
	freqLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(freqLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(freqLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(freqLabel, "");
	lv_obj_set_align(freqLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(freqLabel, FreqY);

	// Sunset-orange call-sign under the frequency.
	stationLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(stationLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(stationLabel, MP_ACCENT, 0);
	lv_label_set_text(stationLabel, "");
	lv_obj_set_align(stationLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(stationLabel, StationY);

	// Warm-cream tagline under the call-sign.
	taglineLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(taglineLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(taglineLabel, MP_TEXT, 0);
	lv_label_set_text(taglineLabel, "");
	lv_obj_set_align(taglineLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(taglineLabel, TaglineY);
}

void PhoneRadio::buildStatus() {
	// "[ ON AIR ]" / "[ TUNED ]" pill — sits between the readout and
	// the soft-key bar. The pill background swaps colour based on
	// `playing` (cyan when on-air, dim purple when paused) so the
	// state reads at a glance.
	statusPill = lv_obj_create(obj);
	lv_obj_remove_style_all(statusPill);
	lv_obj_set_size(statusPill, 70, 12);
	lv_obj_clear_flag(statusPill, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(statusPill, 6, 0);
	lv_obj_set_style_bg_color(statusPill, MP_DIM, 0);
	lv_obj_set_style_bg_opa(statusPill, LV_OPA_70, 0);
	lv_obj_set_style_border_color(statusPill, MP_LABEL_DIM, 0);
	lv_obj_set_style_border_width(statusPill, 1, 0);
	lv_obj_set_style_border_opa(statusPill, LV_OPA_70, 0);
	lv_obj_set_align(statusPill, LV_ALIGN_TOP_MID);
	lv_obj_set_y(statusPill, PillY);

	statusLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(statusLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(statusLabel, MP_TEXT, 0);
	lv_label_set_text(statusLabel, "TUNED");
	lv_obj_set_align(statusLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(statusLabel, PillY + 2);
	lv_obj_move_foreground(statusLabel);
}

// =====================================================================
// Live updates
// =====================================================================

void PhoneRadio::refreshFrequency() {
	if(freqLabel == nullptr) return;
	const uint16_t deci = stationFreqDeci(stationIndex);
	char buf[16];
	snprintf(buf, sizeof(buf), "%u.%u FM",
			 (unsigned)(deci / 10), (unsigned)(deci % 10));
	lv_label_set_text(freqLabel, buf);
}

void PhoneRadio::refreshStation() {
	if(stationLabel != nullptr){
		lv_label_set_text(stationLabel, stationName(stationIndex));
	}
	if(taglineLabel != nullptr){
		lv_label_set_text(taglineLabel, stationTagline(stationIndex));
	}
}

void PhoneRadio::refreshCursor() {
	if(dialCursor == nullptr) return;
	const lv_coord_t span = DialRight - DialLeft;
	const lv_coord_t x = DialLeft +
			(lv_coord_t)((int32_t) span * stationIndex / (StationCount - 1));
	// Cursor is 3 px wide so we centre it over the tick.
	lv_obj_set_x(dialCursor, x - 1);
}

void PhoneRadio::refreshStatus() {
	if(statusPill == nullptr || statusLabel == nullptr) return;
	if(playing){
		lv_label_set_text(statusLabel, "ON AIR");
		lv_obj_set_style_bg_color(statusPill, MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(statusPill, LV_OPA_70, 0);
		lv_obj_set_style_border_color(statusPill, MP_HIGHLIGHT, 0);
		lv_obj_set_style_text_color(statusLabel, MP_BG_DARK, 0);
	} else {
		lv_label_set_text(statusLabel, "TUNED");
		lv_obj_set_style_bg_color(statusPill, MP_DIM, 0);
		lv_obj_set_style_bg_opa(statusPill, LV_OPA_70, 0);
		lv_obj_set_style_border_color(statusPill, MP_LABEL_DIM, 0);
		lv_obj_set_style_text_color(statusLabel, MP_TEXT, 0);
	}
	if(softKeys != nullptr){
		softKeys->setLeft(playing ? "STOP" : "PLAY");
	}
}

// =====================================================================
// Public control
// =====================================================================

void PhoneRadio::tuneTo(uint8_t index) {
	if(StationCount == 0) return;
	const uint8_t newIdx = index % StationCount;
	if(newIdx == stationIndex && playing) return;
	stationIndex = newIdx;
	refreshFrequency();
	refreshStation();
	refreshCursor();
	if(playing){
		// Re-engage the engine on the new station immediately.
		const PhoneRingtoneEngine::Melody* m = stationMelody(stationIndex);
		if(m != nullptr){
			Ringtone.stop();
			Ringtone.play(*m);
		}
	}
}

void PhoneRadio::play() {
	startPlayback();
}

void PhoneRadio::stop() {
	stopPlayback();
}

void PhoneRadio::togglePlay() {
	if(playing) stopPlayback();
	else        startPlayback();
}

void PhoneRadio::next() {
	if(StationCount == 0) return;
	if(stationIndex + 1 >= StationCount) return;  // clamp at top
	tuneTo(stationIndex + 1);
}

void PhoneRadio::prev() {
	if(StationCount == 0) return;
	if(stationIndex == 0) return;  // clamp at bottom
	tuneTo(stationIndex - 1);
}

// =====================================================================
// Internal control
// =====================================================================

void PhoneRadio::startPlayback() {
	const PhoneRingtoneEngine::Melody* m = stationMelody(stationIndex);
	if(m == nullptr) return;
	Ringtone.play(*m);
	playing = true;
	refreshStatus();
}

void PhoneRadio::stopPlayback() {
	if(playing){
		Ringtone.stop();
		playing = false;
	}
	refreshStatus();
}

// =====================================================================
// Input
// =====================================================================

void PhoneRadio::buttonPressed(uint i) {
	switch(i){
		case BTN_LEFT:
		case BTN_4:
		case BTN_L:
			prev();
			break;

		case BTN_RIGHT:
		case BTN_6:
		case BTN_R:
			next();
			break;

		case BTN_ENTER:
			if(softKeys) softKeys->flashLeft();
			togglePlay();
			break;

		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			stopPlayback();
			pop();
			break;

		default:
			break;
	}
}
