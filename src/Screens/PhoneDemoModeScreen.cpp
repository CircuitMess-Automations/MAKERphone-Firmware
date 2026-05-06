#include "PhoneDemoModeScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

/*
 * S200 - PhoneDemoModeScreen
 *
 * See PhoneDemoModeScreen.h for the design rationale. Implementation is
 * deliberately small: a static slide deck in this .cpp, three static
 * labels that get rewritten on every advance, an lv_timer that ticks
 * every kSlidePeriodMs ms, and an any-key-exit input handler.
 */

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneAboutScreen.cpp / PhoneSettingsScreen.cpp). Cyan for
// the caption (informational), warm cream for the title + sub-lines (the
// payload the camera will photograph), dim purple for the accent line +
// progress dots + exit hint (the chrome around the headline content).
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan caption
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream body
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple chrome
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (active dot)

// Body geometry.
static constexpr lv_coord_t kTitleY    = 32;
static constexpr lv_coord_t kSub1Y     = 56;
static constexpr lv_coord_t kSub2Y     = 66;
static constexpr lv_coord_t kAccentY   = 84;
static constexpr lv_coord_t kDotsY     = 100;
static constexpr lv_coord_t kHintY     = 110;

namespace {

// One row per slide. Kept tiny + static so adding / reordering is a
// single edit and the table costs zero RAM (lives in flash).
struct Slide {
	const char* title;
	const char* sub1;
	const char* sub2;
	const char* accent;
};

const Slide kSlides[PhoneDemoModeScreen::kSlideCount] = {
	{ "MAKERPHONE 2.0", "retro feature phone",  "for the maker era",      "press any key to exit" },
	{ "CALL & MESSAGE", "LoRa peers across",    "200 m of open sky",      "S28 - end to end calls" },
	{ "16 GAMES",       "Snake Pong Air Hockey","2048 Tetris and more",   "S71-S100 phase N" },
	{ "AUDIO STUDIO",   "ringtones composer",   "music and beat maker",   "S121-S196 phase R/U" },
	{ "10 THEMES",      "Synthwave Nokia",      "Cyberpunk Vapor Pixel",  "S101-S119 phase O" },
	{ "ALARMS & TIMER", "wake by composition",  "stopwatch and calendar", "S60-S65 phase L" },
	{ "VIRTUAL PET",    "feed and play",        "watch it level up",      "S173 phase T" },
	{ "PERSONALISE",    "owner name accent",    "wallpaper home layout",  "S144-S188 phase R/T" },
	{ "MADE BY YOU",    "open source firmware", "S01-S200 - 200 sessions","github.com/CircuitMess" },
};

// Format the progress-dot row into the supplied buffer. `outLen` must be
// at least 2 * kSlideCount + 1 chars (one glyph per dot + a separator
// between each pair + trailing NUL). Active dot is rendered as '#', the
// rest as '.', so a single label colour can carry the row visually -
// the orange-coloured active dot effect is faked by surrounding the
// active glyph with brackets when rendered through pixelbasic7.
void formatProgressDots(uint8_t activeIdx, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(outLen < (size_t) (2 * PhoneDemoModeScreen::kSlideCount + 1)) {
		// Defensive: caller passed too small a buffer. Emit an empty
		// string rather than scribbling beyond the bounds.
		out[0] = '\0';
		return;
	}
	size_t wp = 0;
	for(uint8_t i = 0; i < PhoneDemoModeScreen::kSlideCount; ++i) {
		out[wp++] = (i == activeIdx) ? '*' : '.';
		if(i + 1 < PhoneDemoModeScreen::kSlideCount) {
			out[wp++] = ' ';
		}
	}
	out[wp] = '\0';
}

} // namespace

PhoneDemoModeScreen::PhoneDemoModeScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  titleLabel(nullptr),
		  sub1Label(nullptr),
		  sub2Label(nullptr),
		  accentLabel(nullptr),
		  dotsLabel(nullptr),
		  hintLabel(nullptr),
		  slideTimer(nullptr),
		  slideIdx(0) {

	// Full-screen container, no scrollbars, no inner padding. Same
	// blank-canvas pattern PhoneAboutScreen / PhoneSettingsScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so every label overlays it.
	// The demo screen still feels like part of the MAKERphone family
	// rather than a debug splash.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Standard signal | clock | battery bar so the user always knows
	// the device is alive while the slideshow is rolling - real devices
	// keep showing time/signal/battery during the demo.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildBody();
	buildHint();

	// Soft-key bar: only the right key is meaningful (EXIT). The left
	// key stays blank rather than wiring something inert - the demo
	// has no user-tweakable state, so there is nothing for an OPEN
	// or SAVE softkey to do.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("EXIT");

	// Initial paint of the slide content so the user sees real text
	// immediately on push (rather than blanks until the first 3 s tick).
	renderSlide();
}

PhoneDemoModeScreen::~PhoneDemoModeScreen() {
	// Defensive: tear down the slide timer if onStop() did not (e.g.
	// the screen is destroyed without ever being started). All other
	// children are parented to obj and freed by the base destructor.
	if(slideTimer != nullptr) {
		lv_timer_del(slideTimer);
		slideTimer = nullptr;
	}
}

void PhoneDemoModeScreen::onStart() {
	Input::getInstance()->addListener(this);

	// Start the slide-advance tick. Idempotent: if a previous onStart()
	// left a timer running we reuse it rather than stacking a second
	// one. lv_timer_create pins user_data to `this` so the static
	// callback can route back to the correct instance.
	if(slideTimer == nullptr) {
		slideTimer = lv_timer_create(onSlideTick, kSlidePeriodMs, this);
	}
	// Repaint immediately so a screen that re-enters from onStart()
	// after a long detach does not show stale content for one tick.
	renderSlide();
}

void PhoneDemoModeScreen::onStop() {
	Input::getInstance()->removeListener(this);
	if(slideTimer != nullptr) {
		lv_timer_del(slideTimer);
		slideTimer = nullptr;
	}
}

// ----- builders --------------------------------------------------------

void PhoneDemoModeScreen::buildCaption() {
	// "DEMO MODE" caption in pixelbasic7 cyan, just under the status
	// bar - same anchor pattern PhoneSettingsScreen / PhoneAboutScreen
	// use for their captions.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "DEMO MODE");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneDemoModeScreen::buildBody() {
	// Title (pixelbasic16 cream) - the headline glyph the camera will
	// photograph. Centered horizontally; the y-offset places it inside
	// the body band right under the caption.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(titleLabel, MP_TEXT, 0);
	lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(titleLabel, 152);
	lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(titleLabel, "");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, kTitleY);

	// Sub-line 1 (pixelbasic7 cream).
	sub1Label = lv_label_create(obj);
	lv_obj_set_style_text_font(sub1Label, &pixelbasic7, 0);
	lv_obj_set_style_text_color(sub1Label, MP_TEXT, 0);
	lv_obj_set_width(sub1Label, 152);
	lv_obj_set_style_text_align(sub1Label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(sub1Label, "");
	lv_obj_set_align(sub1Label, LV_ALIGN_TOP_MID);
	lv_obj_set_y(sub1Label, kSub1Y);

	// Sub-line 2 (pixelbasic7 cream).
	sub2Label = lv_label_create(obj);
	lv_obj_set_style_text_font(sub2Label, &pixelbasic7, 0);
	lv_obj_set_style_text_color(sub2Label, MP_TEXT, 0);
	lv_obj_set_width(sub2Label, 152);
	lv_obj_set_style_text_align(sub2Label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(sub2Label, "");
	lv_obj_set_align(sub2Label, LV_ALIGN_TOP_MID);
	lv_obj_set_y(sub2Label, kSub2Y);

	// Accent line (pixelbasic7 dim purple) - the small "session range"
	// breadcrumb under the body that ties each slide back to the
	// roadmap so a developer watching the marketing video can match a
	// feature name to the sessions that delivered it.
	accentLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(accentLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(accentLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(accentLabel, 152);
	lv_obj_set_style_text_align(accentLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(accentLabel, "");
	lv_obj_set_align(accentLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(accentLabel, kAccentY);

	// Progress-dot row (pixelbasic7 sunset orange) - one glyph per slide,
	// active slide rendered as '*', the rest as '.'. Centred so the row
	// stays visually balanced as the active dot walks left-to-right.
	dotsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(dotsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(dotsLabel, MP_ACCENT, 0);
	lv_label_set_text(dotsLabel, "");
	lv_obj_set_align(dotsLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(dotsLabel, kDotsY);
}

void PhoneDemoModeScreen::buildHint() {
	// "ANY KEY: EXIT" hint in dim purple, sitting just above the soft-
	// key bar. The same hint is duplicated on the right softkey label
	// ("EXIT") so a viewer who only catches one of the two cues still
	// understands the affordance.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "ANY KEY: EXIT");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, kHintY);
}

// ----- slide rendering -------------------------------------------------

void PhoneDemoModeScreen::renderSlide() {
	// Defensive index clamp - the modulo-advance in advanceSlide() keeps
	// slideIdx in range, but a future caller that pokes the field
	// directly should not crash on an out-of-range index.
	const uint8_t idx = (slideIdx < kSlideCount) ? slideIdx : 0;
	const Slide& s = kSlides[idx];

	if(titleLabel  != nullptr) lv_label_set_text(titleLabel,  s.title);
	if(sub1Label   != nullptr) lv_label_set_text(sub1Label,   s.sub1);
	if(sub2Label   != nullptr) lv_label_set_text(sub2Label,   s.sub2);
	if(accentLabel != nullptr) lv_label_set_text(accentLabel, s.accent);

	if(dotsLabel != nullptr) {
		// One glyph per dot + a single space separator + trailing NUL.
		// kSlideCount = 9 -> 9 * 2 + 1 = 19 chars max, round up to 24.
		char buf[32];
		formatProgressDots(idx, buf, sizeof(buf));
		lv_label_set_text(dotsLabel, buf);
	}
}

void PhoneDemoModeScreen::advanceSlide() {
	slideIdx = (uint8_t) ((slideIdx + 1) % kSlideCount);
	renderSlide();
}

void PhoneDemoModeScreen::onSlideTick(lv_timer_t* timer) {
	if(timer == nullptr) return;
	auto* self = static_cast<PhoneDemoModeScreen*>(timer->user_data);
	if(self == nullptr) return;
	self->advanceSlide();
}

// ----- input -----------------------------------------------------------

void PhoneDemoModeScreen::buttonPressed(uint /*i*/) {
	// Any button exits. The contract is "any key" rather than "BACK
	// only" because the screen is meant to be operated by a third
	// party in front of a camera who may not know the key map, and
	// because there are no other meaningful actions on this screen.
	if(softKeys != nullptr) {
		softKeys->flashRight();
	}
	pop();
}
