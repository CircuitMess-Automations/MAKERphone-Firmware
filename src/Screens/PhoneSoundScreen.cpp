#include "PhoneSoundScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <Audio/Piezo.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneBrightnessScreen.cpp, PhoneSettingsScreen.cpp). Cyan
// for the caption (informational), sunset orange for the saved-profile
// checkmark dot + focused-row chevron, warm cream for option names, dim
// purple for descriptions / hint text and the cursor highlight rectangle.
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

namespace {
struct OptionDef {
	PhoneSoundScreen::Profile profile;
	const char* name;       // bold (cream)
	const char* desc;       // dim (purple)
};

// Order in the visible list. Mute first (most-restrictive) -> Loud last
// (most-permissive) so the cursor reads top-to-bottom as "louder".
const OptionDef kOptions[] = {
	{ PhoneSoundScreen::Profile::Mute,    "MUTE",    "Silent"          },
	{ PhoneSoundScreen::Profile::Vibrate, "VIBRATE", "Buzz only"       },
	{ PhoneSoundScreen::Profile::Loud,    "LOUD",    "Ringer + keys"   },
};
constexpr uint8_t kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);
static_assert(kOptionCount == PhoneSoundScreen::ProfileCount,
			  "kOptions must match PhoneSoundScreen::ProfileCount");

// Geometry inside the list container. The dot lives in the leftmost
// 12 px column, the bold name in the next ~50 px, and the dim
// description fills the rest.
constexpr lv_coord_t kListX        = 4;
constexpr lv_coord_t kColDotX      = 6;        // x of the saved-state dot
constexpr lv_coord_t kColNameX     = 18;       // x of the option name label
constexpr lv_coord_t kColDescX     = 64;       // x of the description label
constexpr lv_coord_t kDotSize      = 6;        // dot diameter

// Read the persisted profile and clamp it into the [0..2] range. We
// clamp defensively because a NVS-resize wipe (or the very first boot
// after the new field was added) could leave the byte at 0xFF.
PhoneSoundScreen::Profile loadSavedProfile() {
	uint8_t raw = Settings.get().soundProfile;
	if(raw > static_cast<uint8_t>(PhoneSoundScreen::Profile::Loud)) {
		// Out-of-range reading: derive intent from the legacy `sound`
		// flag. sound == true -> Loud, sound == false -> Mute. This
		// keeps a freshly-flashed device internally consistent with
		// whatever the user previously configured via the legacy
		// SettingsScreen sound switch.
		return Settings.get().sound ? PhoneSoundScreen::Profile::Loud
									: PhoneSoundScreen::Profile::Mute;
	}
	return static_cast<PhoneSoundScreen::Profile>(raw);
}
} // namespace

PhoneSoundScreen::PhoneSoundScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  listContainer(nullptr),
		  highlight(nullptr),
		  hintLabel(nullptr),
		  initialProfile(Profile::Mute) {

	for(uint8_t i = 0; i < ProfileCount; ++i) {
		rows[i].dotObj   = nullptr;
		rows[i].nameObj  = nullptr;
		rows[i].descObj  = nullptr;
		rows[i].y        = 0;
		rows[i].profile  = static_cast<Profile>(i);
	}

	// Snapshot of the profile the screen opened with. BACK reverts to
	// this value (both runtime preview AND the live Piezo mute state)
	// so the user can browse different rows without committing.
	initialProfile = loadSavedProfile();
	cursor         = static_cast<uint8_t>(initialProfile);

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneBrightnessScreen / PhoneSettingsScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the rest of the screen
	// reads on top of the synthwave gradient.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery bar.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildListContainer();
	buildList();
	buildHint();

	// Bottom: SAVE / BACK soft-keys, matching PhoneBrightnessScreen.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("SAVE");
	softKeys->setRight("BACK");

	// Initial paint: cursor sits on the saved profile, checkmark dot
	// lights up on the same row, and we live-preview the audio stack
	// to match. The preview is a no-op for Mute / Vibrate (just sets
	// Piezo mute), so opening the screen does not unexpectedly buzz.
	refreshHighlight();
	refreshCheckmarks(initialProfile);
	applyPreview(initialProfile);
}

PhoneSoundScreen::~PhoneSoundScreen() {
	// Children (wallpaper, statusBar, softKeys, labels, dots) are all
	// parented to obj - LVGL frees them recursively when the screen's
	// obj is destroyed by the LVScreen base destructor.
}

void PhoneSoundScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneSoundScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders --------------------------------------------------------

void PhoneSoundScreen::buildCaption() {
	// "SOUND" caption in pixelbasic7 cyan, just under the status bar.
	// Same anchor pattern PhoneBrightnessScreen / PhoneSettingsScreen
	// use so every Phase-J page reads as a family.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "SOUND");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneSoundScreen::buildListContainer() {
	const lv_coord_t totalH = ProfileCount * RowH;

	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, ListW, totalH);
	lv_obj_set_pos(listContainer, kListX, ListY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(listContainer, 0, 0);

	// Cursor highlight - a single rounded rect sitting BEHIND the rows.
	// Created first so its z-order is below the row labels. Muted purple
	// at ~70% opacity reads as "selected" without screaming over the
	// wallpaper.
	highlight = lv_obj_create(listContainer);
	lv_obj_remove_style_all(highlight);
	lv_obj_set_size(highlight, ListW, RowH);
	lv_obj_set_pos(highlight, 0, 0);
	lv_obj_set_scrollbar_mode(highlight, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(highlight, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(highlight, 2, 0);
	lv_obj_set_style_bg_color(highlight, MP_DIM, 0);
	lv_obj_set_style_bg_opa(highlight, LV_OPA_70, 0);
	lv_obj_set_style_border_color(highlight, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_opa(highlight, LV_OPA_50, 0);
	lv_obj_set_style_border_width(highlight, 1, 0);
}

void PhoneSoundScreen::buildList() {
	for(uint8_t i = 0; i < ProfileCount; ++i) {
		const OptionDef& def = kOptions[i];
		Row& r = rows[i];

		r.profile = def.profile;
		r.y       = i * RowH;

		// Saved-state dot. A small rounded square that lights up
		// (sunset orange) on whichever row matches the *currently
		// committed* profile. Idle rows show a hollow muted-purple
		// outline so the column is visually consistent.
		r.dotObj = lv_obj_create(listContainer);
		lv_obj_remove_style_all(r.dotObj);
		lv_obj_set_size(r.dotObj, kDotSize, kDotSize);
		lv_obj_set_pos(r.dotObj, kColDotX, r.y + (RowH - kDotSize) / 2);
		lv_obj_set_scrollbar_mode(r.dotObj, LV_SCROLLBAR_MODE_OFF);
		lv_obj_clear_flag(r.dotObj, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_radius(r.dotObj, kDotSize / 2, 0);
		lv_obj_set_style_border_color(r.dotObj, MP_LABEL_DIM, 0);
		lv_obj_set_style_border_width(r.dotObj, 1, 0);
		lv_obj_set_style_bg_opa(r.dotObj, LV_OPA_TRANSP, 0);

		// Bold-ish name in the warm-cream "row label" tone. We can't
		// truly bold the pixelbasic7 font without a second weight asset
		// (and we want code-only / zero-SPIFFS), so the name simply
		// gets the cream color while the description below uses the
		// dim purple - same visual hierarchy PhoneCallHistory uses.
		r.nameObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.nameObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.nameObj, MP_TEXT, 0);
		lv_label_set_text(r.nameObj, def.name);
		lv_obj_set_pos(r.nameObj, kColNameX, r.y + (RowH - 7) / 2);

		// Description. Right of the name, dim purple, gives the user
		// a one-word hint about what each profile actually does.
		r.descObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.descObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.descObj, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(r.descObj, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r.descObj, ListW - kColDescX - 4);
		lv_label_set_text(r.descObj, def.desc);
		lv_obj_set_pos(r.descObj, kColDescX, r.y + (RowH - 7) / 2);
	}
}

void PhoneSoundScreen::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "UP / DOWN to choose");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 100);
}

// ----- live updates ----------------------------------------------------

void PhoneSoundScreen::refreshHighlight() {
	if(highlight == nullptr) return;
	if(cursor >= ProfileCount) cursor = 0;

	// Slide the cursor rect to the focused row's Y. Snappy (no
	// animation) - feature-phone navigation should feel instantaneous.
	lv_obj_set_y(highlight, rows[cursor].y);
}

void PhoneSoundScreen::refreshCheckmarks(Profile saved) {
	for(uint8_t i = 0; i < ProfileCount; ++i) {
		if(rows[i].dotObj == nullptr) continue;
		const bool active = (rows[i].profile == saved);
		if(active) {
			// Filled sunset-orange dot for the saved profile.
			lv_obj_set_style_bg_color(rows[i].dotObj, MP_ACCENT, 0);
			lv_obj_set_style_bg_opa(rows[i].dotObj, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(rows[i].dotObj, MP_ACCENT, 0);
		} else {
			// Hollow dim outline for unselected profiles.
			lv_obj_set_style_bg_opa(rows[i].dotObj, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_color(rows[i].dotObj, MP_LABEL_DIM, 0);
		}
	}
}

void PhoneSoundScreen::moveCursorBy(int8_t delta) {
	if(ProfileCount == 0) return;
	int16_t next = static_cast<int16_t>(cursor) + delta;
	// Clamp rather than wrap - 3 rows is short enough that hard stops
	// at the ends feel cleaner than wrapping around.
	if(next < 0)                                       next = 0;
	if(next >= static_cast<int16_t>(ProfileCount))     next = ProfileCount - 1;
	if(static_cast<uint8_t>(next) == cursor) return;
	cursor = static_cast<uint8_t>(next);
	refreshHighlight();
	applyPreview(getFocusedProfile());
}

void PhoneSoundScreen::applyPreview(Profile p) {
	// The Piezo abstraction has setMute() (used by the legacy
	// SettingsScreen sound toggle) and tone(freq, ms). We use those
	// to give the user a quick audible confirmation of the chosen
	// profile without persisting anything yet.
	switch(p) {
		case Profile::Loud:
			// Unmute and play a single short C5 click - same tone the
			// legacy SettingsScreen used as its "sound on" preview, so
			// the audible feedback matches user expectation.
			Piezo.setMute(false);
			Piezo.tone(NOTE_C5, 25);
			break;
		case Profile::Vibrate:
		case Profile::Mute:
		default:
			// Hardware has no vibration motor, so Vibrate is silent
			// today. Both Mute and Vibrate just silence the piezo.
			Piezo.setMute(true);
			break;
	}
}

PhoneSoundScreen::Profile PhoneSoundScreen::getFocusedProfile() const {
	if(cursor >= ProfileCount) return Profile::Mute;
	return rows[cursor].profile;
}

// ----- save / cancel ---------------------------------------------------

void PhoneSoundScreen::saveAndExit() {
	const Profile chosen = getFocusedProfile();

	// Persist BOTH fields atomically. soundProfile is the new finer-
	// grained intent; sound stays the back-compat boolean every other
	// service still reads (BuzzerService, PhoneRingtoneEngine,
	// SettingsScreen). Loud -> sound on; Mute / Vibrate -> sound off.
	Settings.get().soundProfile = static_cast<uint8_t>(chosen);
	Settings.get().sound        = (chosen == Profile::Loud);
	Settings.store();

	// Re-sync Piezo mute to the persisted intent. setMute(false) when
	// Loud, setMute(true) when Mute / Vibrate. This guarantees the
	// post-save state matches what callers of Piezo.tone() will hear.
	Piezo.setMute(!Settings.get().sound);

	// Update the on-screen checkmark before popping so any animated
	// pop transition shows the new committed state - tiny detail, but
	// it makes the action feel decisive.
	refreshCheckmarks(chosen);

	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneSoundScreen::cancelAndExit() {
	// Revert the live Piezo mute setting back to the persisted-on-entry
	// profile. We don't touch Settings.get() because, since saveAndExit
	// is the only writer, the persisted value is already the entry
	// value if we got here without saving.
	applyPreview(initialProfile);

	if(softKeys) softKeys->flashRight();
	pop();
}

// ----- input handling --------------------------------------------------

void PhoneSoundScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_2:
		case BTN_4:
			// "Up" in the option list. LEFT mirrors UP for users who
			// drive the menu with the d-pad halves of the keypad.
			moveCursorBy(-1);
			break;

		case BTN_RIGHT:
		case BTN_6:
		case BTN_8:
			moveCursorBy(+1);
			break;

		case BTN_ENTER:
			saveAndExit();
			break;

		case BTN_BACK:
			cancelAndExit();
			break;

		default:
			break;
	}
}
