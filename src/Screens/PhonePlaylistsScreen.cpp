#include "PhonePlaylistsScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Interface/PhoneTransitions.h"
#include "../Services/PhoneMusicPlaylists.h"
#include "PhoneMusicPlayer.h"
#include "PhoneRadio.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneSoftKeyToneScreen.cpp / PhoneMusicPlayer.cpp). Cyan
// for the caption + active dot, sunset orange for the focused-row count
// pip, warm cream for option names, dim purple for descriptions / hint
// and the cursor highlight rectangle.
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

namespace {
// Geometry inside the list container. Mirrors PhoneSoftKeyToneScreen so
// the two list screens read as the same widget family. Name column starts
// 14 px from the left edge (after a small leading dot) and the count
// column is right-aligned via fixed pixel offsets so the visual rhythm
// matches even when a playlist's track count is single- vs. double-digit.
constexpr lv_coord_t kListX     = 4;
constexpr lv_coord_t kColDotX   = 4;
constexpr lv_coord_t kColNameX  = 14;
constexpr lv_coord_t kColCountX = 102;   // " 4 tracks" / "10 tracks" right-side caption
constexpr lv_coord_t kDotSize   = 6;
} // namespace

PhonePlaylistsScreen::PhonePlaylistsScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  listContainer(nullptr),
		  highlight(nullptr),
		  hintLabel(nullptr),
		  cursor(0) {

	for(uint8_t i = 0; i < RowCount; ++i) {
		rows[i].dotObj   = nullptr;
		rows[i].nameObj  = nullptr;
		rows[i].countObj = nullptr;
		rows[i].y        = 0;
	}

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneMusicPlayer / PhoneSoftKeyToneScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildListContainer();
	buildList();
	buildHint();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->set("PLAY", "BACK");

	// S195 — discoverability hint for the FM radio shortcut. The "5"
	// numpad key launches PhoneRadio (eight pre-canned looping melody
	// stations). Picked BTN_5 because it is the natural "extras" key on
	// a feature-phone numpad and is already free on this screen.
	lv_obj_t* radioHint = lv_label_create(obj);
	lv_obj_set_style_text_font(radioHint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(radioHint, MP_LABEL_DIM, 0);
	lv_label_set_text(radioHint, "5: FM RADIO");
	lv_obj_set_align(radioHint, LV_ALIGN_TOP_MID);
	lv_obj_set_y(radioHint, 110);

	refreshHighlight();
	refreshFocusedHint();
}

PhonePlaylistsScreen::~PhonePlaylistsScreen() {
	// Children parented to obj - LVGL frees recursively via the LVScreen
	// base destructor. Nothing manual.
}

void PhonePlaylistsScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhonePlaylistsScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// =========================================================================
// builders
// =========================================================================

void PhonePlaylistsScreen::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "PLAYLISTS");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhonePlaylistsScreen::buildListContainer() {
	const lv_coord_t totalH = RowCount * RowH;

	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, ListW, totalH);
	lv_obj_set_pos(listContainer, kListX, ListY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(listContainer, 0, 0);

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

void PhonePlaylistsScreen::buildList() {
	for(uint8_t i = 0; i < RowCount; ++i) {
		Row& r = rows[i];
		r.y = i * RowH;

		// Lead-in dot — tinted accent so the eye locks onto where the
		// playlist column starts even when the highlight is on a
		// different row.
		r.dotObj = lv_obj_create(listContainer);
		lv_obj_remove_style_all(r.dotObj);
		lv_obj_set_size(r.dotObj, kDotSize, kDotSize);
		lv_obj_set_pos(r.dotObj, kColDotX, r.y + (RowH - kDotSize) / 2);
		lv_obj_set_scrollbar_mode(r.dotObj, LV_SCROLLBAR_MODE_OFF);
		lv_obj_clear_flag(r.dotObj, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_radius(r.dotObj, kDotSize / 2, 0);
		lv_obj_set_style_bg_color(r.dotObj, MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(r.dotObj, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(r.dotObj, MP_ACCENT, 0);
		lv_obj_set_style_border_width(r.dotObj, 1, 0);

		r.nameObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.nameObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.nameObj, MP_TEXT, 0);
		lv_label_set_long_mode(r.nameObj, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r.nameObj, kColCountX - kColNameX - 2);
		lv_label_set_text(r.nameObj, PhoneMusicPlaylists::nameOf(i));
		lv_obj_set_pos(r.nameObj, kColNameX, r.y + (RowH - 7) / 2);

		r.countObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.countObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.countObj, MP_LABEL_DIM, 0);
		char buf[16];
		const uint8_t n = PhoneMusicPlaylists::trackCount(i);
		// Singular vs plural so "1 track" reads naturally if a future
		// playlist ships with a single tune. The four built-in lists
		// always have 3+ tracks today, but the formatting is cheap.
		snprintf(buf, sizeof(buf), "%u track%s",
				 (unsigned) n, (n == 1) ? "" : "s");
		lv_label_set_text(r.countObj, buf);
		lv_obj_set_pos(r.countObj, kColCountX, r.y + (RowH - 7) / 2);
	}
}

void PhonePlaylistsScreen::buildHint() {
	// Per-row caption strip directly under the list. Mirrors the
	// "tip-of-the-day" / hint pattern used elsewhere — gives the focused
	// playlist a tiny human-readable byline ("Slow / dreamy") so the
	// user has more context than just the title before they hit PLAY.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(hintLabel, 152);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 98);
}

// =========================================================================
// live updates
// =========================================================================

void PhonePlaylistsScreen::refreshHighlight() {
	if(highlight == nullptr) return;
	if(cursor >= RowCount) cursor = 0;
	lv_obj_set_y(highlight, rows[cursor].y);
}

void PhonePlaylistsScreen::refreshFocusedHint() {
	if(hintLabel == nullptr) return;
	const char* cap = PhoneMusicPlaylists::captionOf(cursor);
	lv_label_set_text(hintLabel, (cap != nullptr) ? cap : "");
}

// =========================================================================
// navigation + launch
// =========================================================================

void PhonePlaylistsScreen::stepBy(int8_t delta) {
	if(RowCount == 0) return;
	int16_t next = (int16_t) cursor + (int16_t) delta;
	if(next < 0) next = 0;
	if(next >= (int16_t) RowCount) next = RowCount - 1;
	if((uint8_t) next == cursor) return;
	cursor = (uint8_t) next;
	refreshHighlight();
	refreshFocusedHint();
}

void PhonePlaylistsScreen::launchSelection() {
	const uint8_t id = cursor;

	// Resolve the playlist into the const Melody* const* shape the
	// player wants. The pointer is stable for the firmware's lifetime
	// because PhoneMusicPlaylists backs each list with static storage,
	// so handing it to the player without a copy is safe.
	const PhoneRingtoneEngine::Melody* const* tracks =
			PhoneMusicPlaylists::tracks(id);
	const uint8_t count = PhoneMusicPlaylists::trackCount(id);
	const char*   name  = PhoneMusicPlaylists::nameOf(id);

	auto* player = new PhoneMusicPlayer();
	if(tracks != nullptr && count > 0){
		player->setTracks(tracks, count);
	}
	player->setPlaylistName(name);

	// Same drill-style transition every other main-menu app launch
	// uses so the playlist→player push reads as part of the SE
	// horizontal-flick navigation language the rest of the firmware
	// is built around.
	if(softKeys) softKeys->flashLeft();
	PhoneTransitions::push(this, player, PhoneTransition::Drill);
}

// =========================================================================
// input
// =========================================================================

void PhonePlaylistsScreen::buttonPressed(uint i) {
	switch(i) {
		// LEFT bumpers + 2/4 numpad walk the cursor up. We treat both
		// horizontal and vertical bumpers as "step up" because the list
		// is short (4 rows) and a single axis is plenty.
		case BTN_LEFT:
		case BTN_2:
		case BTN_4:
			stepBy(-1);
			break;

		case BTN_RIGHT:
		case BTN_6:
		case BTN_8:
			stepBy(+1);
			break;

		case BTN_ENTER:
			launchSelection();
			break;

		// S195 — bumper-style shortcut to PhoneRadio. The fake FM dial
		// is a sibling to the playlist player rather than a child of
		// any one playlist, so we expose it from the picker rather
		// than from inside PhoneMusicPlayer (which already owns L/R).
		case BTN_5: {
			auto* radio = new PhoneRadio();
			if(softKeys) softKeys->flashLeft();
			PhoneTransitions::push(this, radio, PhoneTransition::Drill);
			break;
		}

		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		default:
			break;
	}
}
