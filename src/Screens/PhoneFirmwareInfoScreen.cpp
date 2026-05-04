#include "PhoneFirmwareInfoScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "PhoneAboutScreen.h" // kFirmwareVersion -- single source of truth

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneAboutScreen.cpp / PhoneImeiRevealScreen.cpp). Cyan
// for the caption (informational), warm cream for the value cells (the
// payload the user actually wants to read), dim purple for the small
// section labels above each value -- exactly the layout PhoneAboutScreen
// uses, deliberately so the two diagnostics pages read as siblings.
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan caption
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream value
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple label

// Body geometry. Mirrors PhoneAboutScreen exactly: each label/value pair
// occupies a 20 px tall group (9 px label row + 9 px value row + 2 px
// gap), and we lay four of them out starting at y = 24. y = 24..104 fits
// comfortably between the status bar at top and the soft-key bar at
// y = 118.
static constexpr lv_coord_t kBodyX           = 4;
static constexpr lv_coord_t kBodyW           = 152;
static constexpr lv_coord_t kGroupTopY       = 24;
static constexpr lv_coord_t kGroupHeight     = 20;   // 9 + 9 + 2
static constexpr lv_coord_t kLabelOffset     = 0;
static constexpr lv_coord_t kValueOffset     = 9;

PhoneFirmwareInfoScreen::PhoneFirmwareInfoScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  modelLabel(nullptr),
		  modelValue(nullptr),
		  buildLabel(nullptr),
		  buildValue(nullptr),
		  timeLabel(nullptr),
		  timeValue(nullptr),
		  hardwareLabel(nullptr),
		  hardwareValue(nullptr) {

	// Full-screen container, no scrollbars, no inner padding - same blank
	// canvas pattern PhoneAboutScreen / PhoneImeiRevealScreen / etc. use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the labels overlay it
	// cleanly. Even an Easter-egg reveal should feel like part of the
	// MAKERphone family rather than a raw debug terminal.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Standard signal | clock | battery bar -- keeps the device alive
	// at a glance while the user is reading the reveal.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildBody();

	// Single-action softkey bar: only BACK is meaningful. Leave the
	// left softkey blank (the page is read-only) so the bar stays
	// uncluttered and the user is not invited to do anything other
	// than dismiss.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");
}

PhoneFirmwareInfoScreen::~PhoneFirmwareInfoScreen() {
	// Children (wallpaper, statusBar, softKeys, labels) are all parented
	// to obj - LVGL frees them recursively when the LVScreen base
	// destructor tears down obj. Nothing manual to do here.
}

void PhoneFirmwareInfoScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneFirmwareInfoScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders --------------------------------------------------------

void PhoneFirmwareInfoScreen::buildCaption() {
	// "FW INFO" caption in pixelbasic7 cyan, just under the status bar.
	// Same anchor pattern PhoneAboutScreen uses for "ABOUT" and
	// PhoneImeiRevealScreen uses for "IMEI". We keep the caption short
	// (7 chars + space) so it does not crowd the centred title strip on
	// a 160 px display.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "FW INFO");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneFirmwareInfoScreen::buildBody() {
	// Helper-style local: build a single label+value group at the given
	// row index. Implemented inline rather than as a method so the
	// header stays uncluttered -- this is the only place that needs it,
	// and PhoneAboutScreen uses the same in-file pattern.
	auto makePair = [this](lv_obj_t** outLabel, lv_obj_t** outValue,
						   const char* labelText, const char* valueText,
						   lv_coord_t rowIdx) {
		const lv_coord_t y = kGroupTopY + rowIdx * kGroupHeight;

		lv_obj_t* lab = lv_label_create(obj);
		lv_obj_set_style_text_font(lab, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lab, MP_LABEL_DIM, 0);
		lv_label_set_text(lab, labelText);
		lv_obj_set_pos(lab, kBodyX, y + kLabelOffset);

		lv_obj_t* val = lv_label_create(obj);
		lv_obj_set_style_text_font(val, &pixelbasic7, 0);
		lv_obj_set_style_text_color(val, MP_TEXT, 0);
		// LV_LABEL_LONG_DOT mirrors PhoneAboutScreen: if a future build
		// bumps the firmware string past the 152 px column the label
		// truncates with an ellipsis rather than wrapping into the next
		// group, which would destroy the four-row layout.
		lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
		lv_obj_set_width(val, kBodyW);
		lv_label_set_text(val, valueText);
		lv_obj_set_pos(val, kBodyX, y + kValueOffset);

		*outLabel = lab;
		*outValue = val;
	};

	// Group 1: MODEL -- the firmware "name + version" string. Pulled
	// from PhoneAboutScreen::kFirmwareVersion so the two screens agree
	// on the version label without us having to keep two constants in
	// sync.
	makePair(&modelLabel,    &modelValue,    "MODEL",    PhoneAboutScreen::kFirmwareVersion, 0);

	// Group 2: BUILD -- compile-time __DATE__ macro. Standard C macro
	// expanded by the preprocessor into a literal like "May  4 2026"
	// (note the two spaces for single-digit days; the "Mmm dd yyyy"
	// format is mandated by the C standard so we do not bother
	// reformatting it).
	makePair(&buildLabel,    &buildValue,    "BUILD",    __DATE__,                            1);

	// Group 3: TIME -- compile-time __TIME__ macro, expanded into
	// "HH:MM:SS". This pairs with BUILD to give the user a unique
	// "fingerprint" for the firmware they're running, which is what
	// the original Sony-Ericsson info screen used to differentiate
	// otherwise-identically-versioned releases.
	makePair(&timeLabel,     &timeValue,     "TIME",     __TIME__,                            2);

	// Group 4: HARDWARE -- which board this firmware is running on.
	// The MAKERphone has only ever shipped on Chatter rev. A; if a
	// future board ever appears, this is the one place to bump.
	makePair(&hardwareLabel, &hardwareValue, "HARDWARE", kHardwareRevision,                   3);
}

// ----- input -----------------------------------------------------------

void PhoneFirmwareInfoScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_BACK:
		case BTN_ENTER:
		case BTN_RIGHT:
			// Flash the BACK softkey for tactile feedback then pop. The
			// FW info page has no commit/discard distinction so ENTER
			// behaves the same as BACK -- a friendly second way out.
			// BTN_RIGHT is also accepted because PhoneDialerScreen treats
			// it as the "BACK" softkey (the Chatter d-pad doubles as
			// soft-key buttons), and we want the same muscle memory to
			// dismiss the reveal screen.
			if(softKeys) softKeys->flashRight();
			pop();
			break;
		default:
			break;
	}
}
