#include "PhoneVirtualPet.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneVirtualPet.h"

// MAKERphone retro palette - kept identical to every other Phone*
// widget so the virtual pet screen slots in beside PhoneCalculator
// (S60), PhoneAlarmClock (S124), PhoneTimers (S125), PhoneCurrency
// Converter (S126), PhoneUnitConverter (S127) and PhoneWorldClock
// (S128) without a visual seam.
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple

// =====================================================================
// S129 — PhoneVirtualPet (screen)
//
// Layout (160x128):
//   y=0..10    PhoneStatusBar (signal + clock + battery)
//   y=12..20   "YOUR PET" caption (pixelbasic7, cyan)
//   y=24..52   pet face (pixelbasic16, mood-driven emoticon)
//   y=58..96   three stat rows: tag (pixelbasic7) + bar (StatBarW x
//              StatBarH). Rows are 12 px tall, evenly spaced.
//   y=100..108 age caption (pixelbasic7, cream)
//   y=110..117 transient hint (pixelbasic7, accent)
//   y=118..128 PhoneSoftKeyBar
// =====================================================================

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kFaceY        = 28;
static constexpr lv_coord_t kStatRowY0    = 60;
static constexpr lv_coord_t kStatRowH     = 12;
static constexpr lv_coord_t kStatTagX     = 6;
static constexpr lv_coord_t kStatTagW     = 24;
static constexpr lv_coord_t kStatBarX     = 32;
static constexpr lv_coord_t kAgeY         = 98;
static constexpr lv_coord_t kHintY        = 108;

// ---------- ctor / dtor ---------------------------------------------

PhoneVirtualPet::PhoneVirtualPet()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  faceLabel(nullptr),
		  statTagHunger(nullptr),
		  statBarHunger(nullptr),
		  statFillHunger(nullptr),
		  statTagHappy(nullptr),
		  statBarHappy(nullptr),
		  statFillHappy(nullptr),
		  statTagEnergy(nullptr),
		  statBarEnergy(nullptr),
		  statFillEnergy(nullptr),
		  ageLabel(nullptr),
		  hintLabel(nullptr) {

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern as every other Phone* utility screen.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, 160);
	lv_obj_set_pos(captionLabel, 0, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "YOUR PET");

	buildHud();

	// Bottom soft-key bar; populated by refreshSoftKeys().
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("FEED");
	softKeys->setRight("BACK");
}

PhoneVirtualPet::~PhoneVirtualPet() {
	stopTickTimer();
}

// ---------- build helpers -------------------------------------------

void PhoneVirtualPet::buildHud() {
	// Pet face. pixelbasic16 keeps the emoticon legible on the
	// 160x128 panel; centred horizontally so a longer face string
	// (e.g. "( zZz )") still reads symmetrically.
	faceLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(faceLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(faceLabel, MP_TEXT, 0);
	lv_obj_set_width(faceLabel, 160);
	lv_obj_set_pos(faceLabel, 0, kFaceY);
	lv_obj_set_style_text_align(faceLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(faceLabel, "( . _ . )");

	// Three stat rows, each (tag + bar background + bar fill). The
	// fill object is a separate child so the percentage update is
	// just a width set on the fill, not a redraw of the background.
	struct Row {
		lv_coord_t y;
		const char* tag;
		lv_color_t  fillColor;
		lv_obj_t**  outTag;
		lv_obj_t**  outBar;
		lv_obj_t**  outFill;
	};

	const Row rows[3] = {
		{ (lv_coord_t)(kStatRowY0 + 0 * kStatRowH), "HUN", MP_ACCENT,
		  &statTagHunger, &statBarHunger, &statFillHunger },
		{ (lv_coord_t)(kStatRowY0 + 1 * kStatRowH), "HAP", MP_HIGHLIGHT,
		  &statTagHappy,  &statBarHappy,  &statFillHappy  },
		{ (lv_coord_t)(kStatRowY0 + 2 * kStatRowH), "ENG", MP_TEXT,
		  &statTagEnergy, &statBarEnergy, &statFillEnergy },
	};

	for(uint8_t i = 0; i < 3; ++i) {
		const Row& r = rows[i];

		lv_obj_t* tag = lv_label_create(obj);
		lv_obj_set_style_text_font(tag, &pixelbasic7, 0);
		lv_obj_set_style_text_color(tag, MP_LABEL_DIM, 0);
		lv_obj_set_width(tag, kStatTagW);
		lv_obj_set_pos(tag, kStatTagX, (lv_coord_t)(r.y + 1));
		lv_label_set_text(tag, r.tag);
		*r.outTag = tag;

		// Bar background — flat dim purple rectangle. We keep a 1 px
		// border in MP_LABEL_DIM so the empty bar still reads as a
		// "container" when the stat is at zero.
		lv_obj_t* bar = lv_obj_create(obj);
		lv_obj_remove_style_all(bar);
		lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(bar, MP_DIM, 0);
		lv_obj_set_style_border_width(bar, 1, 0);
		lv_obj_set_style_border_color(bar, MP_LABEL_DIM, 0);
		lv_obj_set_style_radius(bar, 0, 0);
		lv_obj_set_size(bar, StatBarW, StatBarH);
		lv_obj_set_pos(bar, kStatBarX, r.y);
		lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
		*r.outBar = bar;

		lv_obj_t* fill = lv_obj_create(bar);
		lv_obj_remove_style_all(fill);
		lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(fill, r.fillColor, 0);
		lv_obj_set_style_radius(fill, 0, 0);
		lv_obj_set_pos(fill, 0, 0);
		lv_obj_set_size(fill, 0, StatBarH);   // refreshed at start
		lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
		*r.outFill = fill;
	}

	// Age caption.
	ageLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(ageLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(ageLabel, MP_TEXT, 0);
	lv_obj_set_width(ageLabel, 160);
	lv_obj_set_pos(ageLabel, 0, kAgeY);
	lv_obj_set_style_text_align(ageLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(ageLabel, "AGE 0d 00h 00m");

	// Transient hint caption — empty by default; `pushHint()` writes
	// a short string for ResetHintMs (or whatever the caller asked
	// for) and refreshAgeAndHint() clears it once the deadline lapses.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_ACCENT, 0);
	lv_obj_set_width(hintLabel, 160);
	lv_obj_set_pos(hintLabel, 0, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "");
}

// ---------- lifecycle -----------------------------------------------

void PhoneVirtualPet::onStart() {
	Input::getInstance()->addListener(this);
	refreshAll();
	startTickTimer();
}

void PhoneVirtualPet::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ---------- repaint -------------------------------------------------

void PhoneVirtualPet::refreshAll() {
	refreshFace();
	refreshStats();
	refreshAgeAndHint();
	refreshSoftKeys();
}

void PhoneVirtualPet::refreshFace() {
	if(faceLabel == nullptr) return;

	// Pick the emoticon string from the live mood. All five fit
	// comfortably inside the 160 px line at pixelbasic16.
	const char* face;
	lv_color_t  col;
	switch(Pet.mood()) {
		case PhoneVirtualPetService::Mood::Asleep:
			face = "( zZz )";
			col  = MP_LABEL_DIM;
			break;
		case PhoneVirtualPetService::Mood::Hungry:
			face = "( T_T )";
			col  = MP_ACCENT;
			break;
		case PhoneVirtualPetService::Mood::Sad:
			face = "( ;_; )";
			col  = MP_LABEL_DIM;
			break;
		case PhoneVirtualPetService::Mood::Tired:
			face = "( -_- )";
			col  = MP_TEXT;
			break;
		case PhoneVirtualPetService::Mood::Happy:
		default:
			face = "( ^_^ )";
			col  = MP_HIGHLIGHT;
			break;
	}
	lv_label_set_text(faceLabel, face);
	lv_obj_set_style_text_color(faceLabel, col, 0);
}

void PhoneVirtualPet::refreshStats() {
	struct Slot {
		lv_obj_t* fill;
		uint8_t   value;
	};
	const Slot slots[3] = {
		{ statFillHunger, Pet.hunger()    },
		{ statFillHappy,  Pet.happiness() },
		{ statFillEnergy, Pet.energy()    },
	};

	for(uint8_t i = 0; i < 3; ++i) {
		if(slots[i].fill == nullptr) continue;

		// Bar fill width: value/100 of the inner bar width, accounting
		// for the 1 px border on each side of the background. We give
		// the fill a minimum width of 1 px when value > 0 so a "1%"
		// stat is still visually distinguishable from "0%".
		const uint16_t innerW = StatBarW > 2 ? (uint16_t)(StatBarW - 2) : 0;
		uint16_t w = (uint16_t)(((uint32_t)slots[i].value * innerW) / 100UL);
		if(slots[i].value > 0 && w == 0) w = 1;

		lv_obj_set_size(slots[i].fill, w, (lv_coord_t)(StatBarH - 2));
		lv_obj_set_pos (slots[i].fill, 1, 1);
	}
}

void PhoneVirtualPet::refreshAgeAndHint() {
	if(ageLabel != nullptr) {
		uint16_t d = 0; uint8_t h = 0; uint8_t m = 0;
		PhoneVirtualPetService::formatAge(Pet.ageMinutes(), d, h, m);
		char buf[32];
		snprintf(buf, sizeof(buf), "AGE %ud %02uh %02um",
		         (unsigned)d, (unsigned)h, (unsigned)m);
		lv_label_set_text(ageLabel, buf);
	}

	if(hintLabel != nullptr && hintExpireAtMs != 0) {
		if((uint32_t)millis() >= hintExpireAtMs) {
			lv_label_set_text(hintLabel, "");
			hintExpireAtMs = 0;
		}
	}
}

void PhoneVirtualPet::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	// Left softkey is "FEED" by default. While the pet is asleep the
	// left softkey becomes "WAKE" so the user understands the dialer
	// 1/2/3 keys are gated until the pet is awake again. Right is
	// always "BACK" — the screen has only one navigation route out.
	if(Pet.isSleeping()) {
		softKeys->setLeft("WAKE");
	} else {
		softKeys->setLeft("FEED");
	}
	softKeys->setRight("BACK");
}

void PhoneVirtualPet::pushHint(const char* text, uint32_t durMs) {
	if(hintLabel == nullptr) return;
	if(text == nullptr)      text = "";
	lv_label_set_text(hintLabel, text);
	hintExpireAtMs = (uint32_t)millis() + durMs;
}

// ---------- LVGL timers ---------------------------------------------

void PhoneVirtualPet::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneVirtualPet::onTickStatic,
	                            TickPeriodMs, this);
}

void PhoneVirtualPet::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneVirtualPet::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneVirtualPet*>(timer->user_data);
	if(self == nullptr) return;
	// Cheap: the live state lives in the service, the screen just
	// re-reads it. Stat bars / age / mood face are updated; soft keys
	// only need to refresh on a sleep-state change but doing it once
	// a second is still O(1) and avoids a cross-event subscription.
	self->refreshAll();
}

// ---------- input ---------------------------------------------------

void PhoneVirtualPet::buttonPressed(uint i) {
	switch(i) {
		case BTN_1:
		case BTN_L:
		case BTN_ENTER:
			// FEED. While asleep the dialer "1" cannot feed -- the
			// screen nudges the user to wake the pet first.
			if(softKeys) softKeys->flashLeft();
			if(Pet.isSleeping()) {
				pushHint("WAKE FIRST (3)", ResetHintMs);
			} else {
				Pet.feed();
				refreshAll();
			}
			break;

		case BTN_2:
			// PLAY. Same wake-first rule as feed.
			if(Pet.isSleeping()) {
				pushHint("WAKE FIRST (3)", ResetHintMs);
			} else {
				Pet.play();
				refreshAll();
			}
			break;

		case BTN_3:
			// SLEEP / WAKE toggle.
			Pet.wakeOrSleep();
			refreshAll();
			break;

		case BTN_4:
			// RESET — short-press shows a hint, a long-press performs
			// the wipe. The reset itself happens in buttonHeld()
			// which sets resetLongFired so the matching release does
			// not double-fire a hint.
			resetLongFired = false;
			pushHint("HOLD 4 TO WIPE", ResetHintMs);
			break;

		case BTN_R:
			// Right bumper = same as the right softkey ("BACK"). No
			// long-press distinction needed on this screen.
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneVirtualPet::buttonReleased(uint i) {
	switch(i) {
		case BTN_BACK:
			if(!backLongFired) {
				pop();
			}
			backLongFired = false;
			break;

		case BTN_4:
			// Short-press 4 already pushed the hint in buttonPressed.
			// A long-press 4 performs the actual wipe in buttonHeld
			// and we just clear the latch here.
			resetLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneVirtualPet::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK is the same as a short tap -- exit. The
			// flag suppresses the matching short-press fire-back on
			// release.
			backLongFired = true;
			pop();
			break;

		case BTN_4:
			// Long-press 4 = confirm reset. The pet is wiped to
			// {age=0, all stats 100, awake} and persisted. We push a
			// transient hint so the user gets visible confirmation
			// instead of just a silent stat jump.
			resetLongFired = true;
			Pet.reset();
			pushHint("PET RESET", ResetHintMs);
			refreshAll();
			break;

		default:
			break;
	}
}
