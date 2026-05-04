#include "PhoneProfileScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <Audio/Piezo.h>
#include <Notes.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneSoundScreen.cpp / PhoneHapticsScreen.cpp /
// PhoneBrightnessScreen.cpp). Cyan for the caption, sunset orange for
// the saved-state checkmark dot, warm cream for option names, dim
// purple for descriptions / hint text and the cursor highlight rect.
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

namespace {
struct OptionDef {
	PhoneProfileScreen::Profile profile;
	const char* name;       // bold (cream) - e.g. GENERAL
	const char* desc;       // dim (purple) - e.g. Ring + buzz
};

// Order in the visible list. General first (most-permissive) so the
// cursor reads top-to-bottom from "factory default phone" downward
// through Silent / Meeting (progressively more restrictive) and then
// up again into Outdoor (loudest) and Headset (audio-routed) -- the
// Sony-Ericsson menu order every feature-phone of the era used.
const OptionDef kOptions[] = {
	{ PhoneProfileScreen::Profile::General, "GENERAL", "Ring + buzz"   },
	{ PhoneProfileScreen::Profile::Silent,  "SILENT",  "No sound"      },
	{ PhoneProfileScreen::Profile::Meeting, "MEETING", "Vibrate only"  },
	{ PhoneProfileScreen::Profile::Outdoor, "OUTDOOR", "Loud + buzz"   },
	{ PhoneProfileScreen::Profile::Headset, "HEADSET", "Ring, no buzz" },
};
constexpr uint8_t kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);
static_assert(kOptionCount == PhoneProfileScreen::ProfileCount,
			  "kOptions must match PhoneProfileScreen::ProfileCount");

// Geometry inside the list container. Matches PhoneSoundScreen so
// the sibling pages read identically: dot in the leftmost column,
// bold name in the middle, dim description on the right. The name
// column is widened (kColDescX = 70) because the new five-name
// vocabulary is up to 7 chars long ("GENERAL", "OUTDOOR", "MEETING",
// "HEADSET") -- 6 px wider than the S52 vocabulary.
constexpr lv_coord_t kListX        = 4;
constexpr lv_coord_t kColDotX      = 6;        // x of the saved-state dot
constexpr lv_coord_t kColNameX     = 18;       // x of the option name
constexpr lv_coord_t kColDescX     = 70;       // x of the description
constexpr lv_coord_t kDotSize      = 6;        // dot diameter

// Read the persisted profile and clamp it into the [0..4] range. We
// clamp defensively because a first boot after the new phoneProfile
// field was added will read 0xFF on devices that already had the
// blob shrunk by an NVS-resize, and we want the screen to land on a
// sensible default rather than crash on the ProfileCount bound.
PhoneProfileScreen::Profile loadSavedProfile() {
	uint8_t raw = Settings.get().phoneProfile;
	if(raw > static_cast<uint8_t>(PhoneProfileScreen::Profile::Headset)) {
		// Out-of-range reading: derive intent from the legacy
		// `sound` flag. sound == true -> General (the post-reset
		// "ringer on" intent), sound == false -> Silent. This keeps
		// a freshly-flashed device internally consistent with
		// whatever the user previously configured via the legacy
		// PhoneSoundScreen / SettingsScreen path.
		return Settings.get().sound
		           ? PhoneProfileScreen::Profile::General
		           : PhoneProfileScreen::Profile::Silent;
	}
	return static_cast<PhoneProfileScreen::Profile>(raw);
}
} // namespace

// ----- public static helpers -------------------------------------------

uint8_t PhoneProfileScreen::legacySoundProfile(Profile p) {
	// Map the five-state phone profile down to the three-state
	// soundProfile byte that BuzzerService / PhoneRingtoneEngine /
	// SettingsScreen / PhoneSoundScreen still read directly:
	//
	//   Silent          -> 0 (Mute)
	//   Meeting         -> 1 (Vibrate)
	//   General/Outdoor/Headset -> 2 (Loud)
	switch(p) {
		case Profile::Silent:  return 0;
		case Profile::Meeting: return 1;
		case Profile::General:
		case Profile::Outdoor:
		case Profile::Headset:
		default:               return 2;
	}
}

bool PhoneProfileScreen::legacySoundOn(Profile p) {
	// Loud profiles (General / Outdoor / Headset) light the legacy
	// `sound` boolean; the silent two (Silent / Meeting) clear it.
	// BuzzerService.cpp already gates every key tone on this flag,
	// so this single mapping is enough to keep all existing audible
	// behaviour aligned with the new five-name surface.
	switch(p) {
		case Profile::General:
		case Profile::Outdoor:
		case Profile::Headset:
			return true;
		case Profile::Silent:
		case Profile::Meeting:
		default:
			return false;
	}
}

PhoneProfileScreen::PhoneProfileScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  listContainer(nullptr),
		  highlight(nullptr),
		  hintLabel(nullptr),
		  initialProfile(Profile::General) {

	for(uint8_t i = 0; i < ProfileCount; ++i) {
		rows[i].dotObj   = nullptr;
		rows[i].nameObj  = nullptr;
		rows[i].descObj  = nullptr;
		rows[i].y        = 0;
		rows[i].profile  = static_cast<Profile>(i);
	}

	// Snapshot the persisted profile. BACK reverts to this value
	// (both runtime preview AND the live Piezo mute state) so the
	// user can browse different rows without committing.
	initialProfile = loadSavedProfile();
	cursor         = static_cast<uint8_t>(initialProfile);

	// Full-screen container, no scrollbars, no padding - same blank-
	// canvas pattern PhoneSoundScreen / PhoneBrightnessScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the rest of the
	// screen reads on top of the synthwave gradient.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery bar.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildListContainer();
	buildList();
	buildHint();

	// Bottom: SAVE / BACK soft-keys, matching PhoneSoundScreen.
	// S67 - the LEFT caption is dirty-aware: "" while the focused row
	// matches the saved profile, "SAVE" once the user has moved the
	// cursor onto a different option. RIGHT mirrors that state with
	// "BACK" / "CANCEL" so the affordance reads correctly at a glance.
	softKeys = new PhoneSoftKeyBar(obj);
	refreshSoftKeys();

	// Initial paint: cursor sits on the saved profile, checkmark dot
	// lights up on the same row, and we live-preview the audio stack
	// to match. The preview is a no-op for Silent / Meeting (just
	// sets Piezo mute), so opening the screen does not unexpectedly
	// buzz.
	refreshHighlight();
	refreshCheckmarks(initialProfile);
	applyPreview(initialProfile);
}

PhoneProfileScreen::~PhoneProfileScreen() {
	// Children (wallpaper, statusBar, softKeys, labels, dots) are all
	// parented to obj - LVGL frees them recursively when the screen's
	// obj is destroyed by the LVScreen base destructor.
}

void PhoneProfileScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneProfileScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders --------------------------------------------------------

void PhoneProfileScreen::buildCaption() {
	// "PROFILE" caption in pixelbasic7 cyan, just under the status
	// bar. Same anchor pattern PhoneSoundScreen / PhoneBrightnessScreen
	// use so every Phase-J / Phase-R page reads as a family.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "PROFILE");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneProfileScreen::buildListContainer() {
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

	// Cursor highlight - a single rounded rect sitting BEHIND the
	// rows. Created first so its z-order is below the row labels.
	// Muted purple at ~70 % opacity reads as "selected" without
	// screaming over the wallpaper.
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

void PhoneProfileScreen::buildList() {
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

		// Bold-ish name in the warm-cream row label tone. We can't
		// truly bold pixelbasic7 without a second weight asset (and
		// we want code-only / zero-SPIFFS), so the name simply gets
		// the cream colour while the description below uses the dim
		// purple - same visual hierarchy PhoneSoundScreen uses.
		r.nameObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.nameObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.nameObj, MP_TEXT, 0);
		lv_label_set_text(r.nameObj, def.name);
		lv_obj_set_pos(r.nameObj, kColNameX, r.y + (RowH - 7) / 2);

		// Description. Right of the name, dim purple, gives the
		// user a one-phrase hint about what each profile actually
		// does. Long-mode DOT clips gracefully if a future
		// translation overflows the column.
		r.descObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.descObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.descObj, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(r.descObj, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r.descObj, ListW - kColDescX - 4);
		lv_label_set_text(r.descObj, def.desc);
		lv_obj_set_pos(r.descObj, kColDescX, r.y + (RowH - 7) / 2);
	}
}

void PhoneProfileScreen::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "UP / DOWN to choose");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 100);
}

// ----- live updates ----------------------------------------------------

void PhoneProfileScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	const bool dirty = (getFocusedProfile() != initialProfile);
	// LEFT only shows SAVE when there is something to save - the
	// pristine state hides the slot so the bar reads as a single-
	// action BACK page (matches PhoneSoundScreen / PhoneAboutScreen
	// convention).
	softKeys->set(dirty ? "SAVE"   : "",
	              dirty ? "CANCEL" : "BACK");
}

void PhoneProfileScreen::refreshHighlight() {
	if(highlight == nullptr) return;
	if(cursor >= ProfileCount) cursor = 0;

	// Slide the cursor rect to the focused row's Y. Snappy (no
	// animation) - feature-phone navigation should feel
	// instantaneous.
	lv_obj_set_y(highlight, rows[cursor].y);
}

void PhoneProfileScreen::refreshCheckmarks(Profile saved) {
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

void PhoneProfileScreen::moveCursorBy(int8_t delta) {
	if(ProfileCount == 0) return;
	int16_t next = static_cast<int16_t>(cursor) + delta;
	// Clamp rather than wrap - five rows is short enough that hard
	// stops at the ends feel cleaner than wrapping around (matches
	// PhoneSoundScreen).
	if(next < 0)                                       next = 0;
	if(next >= static_cast<int16_t>(ProfileCount))     next = ProfileCount - 1;
	if(static_cast<uint8_t>(next) == cursor) return;
	cursor = static_cast<uint8_t>(next);
	refreshHighlight();
	refreshSoftKeys();
	applyPreview(getFocusedProfile());
}

void PhoneProfileScreen::applyPreview(Profile p) {
	// The Piezo abstraction has setMute() (used by the legacy
	// PhoneSoundScreen sound toggle) and tone(freq, ms). We use those
	// to give the user a quick audible confirmation of the chosen
	// profile without persisting anything yet.
	if(legacySoundOn(p)) {
		// A "rings" profile (General / Outdoor / Headset). Unmute
		// and play a single short C5 click - same tone PhoneSoundScreen
		// uses as its "sound on" preview, so the audible feedback
		// matches user expectation across the two screens.
		Piezo.setMute(false);
		Piezo.tone(NOTE_C5, 25);
	} else {
		// Hardware has no vibration motor today, so Silent and
		// Meeting both just silence the piezo. The visual + state
		// distinction between the two profiles is preserved so a
		// future hardware revision with a real vibration motor can
		// light up on Meeting without touching the screen layer.
		Piezo.setMute(true);
	}
}

PhoneProfileScreen::Profile PhoneProfileScreen::getFocusedProfile() const {
	if(cursor >= ProfileCount) return Profile::General;
	return rows[cursor].profile;
}

// ----- save / cancel ---------------------------------------------------

void PhoneProfileScreen::saveAndExit() {
	const Profile chosen = getFocusedProfile();

	// Persist BOTH the new five-state phoneProfile AND the legacy
	// three-state soundProfile + bool sound atomically. The legacy
	// fields stay the source of truth every other service still
	// reads (BuzzerService, PhoneRingtoneEngine, SettingsScreen,
	// PhoneSoundScreen as a back-compat picker) so nothing churns.
	Settings.get().phoneProfile = static_cast<uint8_t>(chosen);
	Settings.get().soundProfile = legacySoundProfile(chosen);
	Settings.get().sound        = legacySoundOn(chosen);
	Settings.store();

	// Re-sync Piezo mute to the persisted intent. setMute(false)
	// for "rings" profiles, setMute(true) for Silent / Meeting.
	// This guarantees the post-save state matches what callers of
	// Piezo.tone() will hear.
	Piezo.setMute(!Settings.get().sound);

	// Update the on-screen checkmark before popping so any animated
	// pop transition shows the new committed state - tiny detail,
	// but it makes the action feel decisive.
	refreshCheckmarks(chosen);

	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneProfileScreen::cancelAndExit() {
	// Revert the live Piezo mute setting back to the persisted-on-
	// entry profile. We don't touch Settings.get() because, since
	// saveAndExit is the only writer, the persisted value is
	// already the entry value if we got here without saving.
	applyPreview(initialProfile);

	if(softKeys) softKeys->flashRight();
	pop();
}

// ----- input handling --------------------------------------------------

void PhoneProfileScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_2:
		case BTN_4:
			// "Up" in the option list. LEFT mirrors UP for users
			// who drive the menu with the d-pad halves of the
			// keypad.
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
