#include "PhoneMainMenu.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <vector>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneMenuGrid.h"
#include "../Fonts/font.h"
#include "../Interface/PhoneTransitions.h"

// MAKERphone retro palette - kept consistent with the other phone widgets so
// the caption that lives under the grid feels visually part of the same tile
// system. These are intentionally inlined here (rather than living in a
// shared header) because every Phone* widget already does the same thing -
// it is the established pattern in this codebase.
#define MP_ACCENT      lv_color_make(255, 140, 30)    // sunset orange caption
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple sub-caption

PhoneMainMenu::PhoneMainMenu()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  grid(nullptr),
		  caption(nullptr){

	// Full-screen container, no scrollbars, no padding - every child below
	// either uses IGNORE_LAYOUT and pins its own (x, y) on the 160x128
	// display, or is an LVGL primitive (caption label) that we anchor
	// manually. Same pattern PhoneHomeScreen uses; keeps the screen body
	// truly transparent over the synthwave wallpaper.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The grid,
	// status bar, soft keys and caption all overlay it without any opacity
	// gymnastics on the parent. Matches PhoneHomeScreen exactly.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	// Centerpiece grid (built BEFORE the caption so refreshCaption() can
	// query the grid's initial cursor when it is wired up).
	buildGrid();

	// Big focused-app caption that lives between the grid's bottom edge and
	// the soft-key bar. Updates on every cursor move via the grid's
	// onSelectionChanged callback wired up in buildGrid().
	buildCaption();
	refreshCaption();

	// Bottom: feature-phone soft-keys. SELECT (left) is what the host
	// (S20) routes per-icon; BACK (right) returns to the parent screen.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("SELECT");
	softKeys->setRight("BACK");

	// S22: enable long-press detection on BTN_0 (quick-dial) and BTN_BACK
	// (lock device). Same 600 ms threshold as PhoneHomeScreen so the
	// gesture feels identical between the home screen and the main menu -
	// muscle-memory matters on a feature-phone.
	setButtonHoldTime(BTN_0, 600);
	setButtonHoldTime(BTN_BACK, 600);
}

PhoneMainMenu::~PhoneMainMenu() {
	// Children (wallpaper, statusBar, grid, softKeys, caption) are all
	// parented to obj - LVGL frees them recursively when the screen's
	// obj is destroyed by the LVScreen base destructor. Nothing manual.
}

void PhoneMainMenu::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneMainMenu::onStop() {
	Input::getInstance()->removeListener(this);
}

void PhoneMainMenu::buildGrid() {
	// Hard-coded S19 layout: the seven roadmap apps in the documented order.
	// Captions are short (max 6 chars) so they fit cleanly in a 36 px tile
	// at pixelbasic7. Mail icon is intentionally not used here - that lives
	// inside the messaging app and would duplicate Messages on the menu.
	const std::vector<PhoneMenuGrid::Entry> entries = {
		{ PhoneIconTile::Icon::Phone,    "PHONE"   },
		{ PhoneIconTile::Icon::Messages, "MSGS"    },
		{ PhoneIconTile::Icon::Contacts, "CONT"    },
		{ PhoneIconTile::Icon::Music,    "MUSIC"   },
		{ PhoneIconTile::Icon::Camera,   "CAM"     },
		{ PhoneIconTile::Icon::Games,    "GAMES"   },
		{ PhoneIconTile::Icon::Settings, "SETT"    },
	};

	grid = new PhoneMenuGrid(obj, entries, GridCols);

	// PhoneMenuGrid auto-sizes its container to (4 * 36 + 3 * 2 + 2 * 1) =
	// 152 px wide and (rows * 36 + (rows - 1) * 2 + 2 * 1) = 76 px tall for
	// the 4x2 layout. We pin it just under the status bar (y = 11) and
	// horizontally center it: (160 - 152) / 2 = 4 px from the left edge.
	// Hard-coding the offsets keeps the screen layout deterministic across
	// LVGL flex re-flows (the grid uses IGNORE_LAYOUT, see PhoneMenuGrid.h).
	const lv_coord_t gridX = (160 - 152) / 2;
	const lv_coord_t gridY = 11;
	lv_obj_set_pos(grid->getLvObj(), gridX, gridY);

	// Wire the cursor-change callback so the big focused-app caption under
	// the grid stays in sync with the highlighted tile. The lambda captures
	// `this` so it can call refreshCaption(); std::function is what
	// PhoneMenuGrid::SelectionChangedCb already expects.
	grid->setOnSelectionChanged([this](uint8_t /*prev*/, uint8_t /*curr*/){
		this->refreshCaption();
	});
}

void PhoneMainMenu::buildCaption() {
	// One pixelbasic7 label centered horizontally between the grid's bottom
	// edge (y = 11 + 76 = 87) and the soft-key bar (y = 118). That leaves
	// a 31 px tall band; the label sits in the middle (y = 100, baseline
	// approx). The caption is sunset orange so it pops over the synthwave
	// background without competing with the cyan clock above it.
	caption = lv_label_create(obj);
	lv_obj_set_style_text_font(caption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(caption, MP_ACCENT, 0);
	lv_label_set_text(caption, "");
	lv_obj_set_align(caption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(caption, 100);

	// Make sure the caption draws on top of the grid (LVGL z-order is
	// child-creation order; we built the grid first so the caption is
	// already above it, but explicitly moving it foreground future-proofs
	// this against a later re-ordering of the constructor).
	lv_obj_move_foreground(caption);
}

void PhoneMainMenu::refreshCaption() {
	if(caption == nullptr) return;
	if(grid == nullptr){
		lv_label_set_text(caption, "");
		return;
	}
	lv_label_set_text(caption, iconName(grid->getSelectedIcon()));
}

const char* PhoneMainMenu::iconName(PhoneIconTile::Icon icon) {
	// Pretty, full-width app names rendered in the focus caption. Kept in
	// uppercase to match the feature-phone aesthetic (the soft-key labels
	// are uppercase too). Note: these are *bigger* than the per-tile labels
	// because the user just navigated here and wants quick confirmation.
	switch(icon){
		case PhoneIconTile::Icon::Phone:    return "PHONE";
		case PhoneIconTile::Icon::Messages: return "MESSAGES";
		case PhoneIconTile::Icon::Contacts: return "CONTACTS";
		case PhoneIconTile::Icon::Music:    return "MUSIC";
		case PhoneIconTile::Icon::Camera:   return "CAMERA";
		case PhoneIconTile::Icon::Games:    return "GAMES";
		case PhoneIconTile::Icon::Settings: return "SETTINGS";
		case PhoneIconTile::Icon::Mail:     return "MAIL";
	}
	return "";
}

void PhoneMainMenu::setOnSelect(SoftKeyHandler cb) {
	selectCb = cb;
}

void PhoneMainMenu::setOnBack(SoftKeyHandler cb) {
	backCb = cb;
}

void PhoneMainMenu::setOnQuickDial(SoftKeyHandler cb) {
	quickDialCb = cb;
}

void PhoneMainMenu::setOnLockHold(SoftKeyHandler cb) {
	lockHoldCb = cb;
}

PhoneIconTile::Icon PhoneMainMenu::getSelectedIcon() const {
	if(grid == nullptr) return PhoneIconTile::Icon::Phone;
	return grid->getSelectedIcon();
}

uint8_t PhoneMainMenu::getSelectedIndex() const {
	if(grid == nullptr) return 0;
	return grid->getCursor();
}

void PhoneMainMenu::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneMainMenu::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneMainMenu::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneMainMenu::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

void PhoneMainMenu::buttonPressed(uint i) {
	// Navigation primitives map directly onto PhoneMenuGrid::moveCursor:
	//   BTN_LEFT  / BTN_4 -> dx = -1 (also wraps to previous row's last col)
	//   BTN_RIGHT / BTN_6 -> dx = +1
	//   BTN_2 (north)     -> dy = -1
	//   BTN_8 (south)     -> dy = +1
	// We accept both the dedicated direction buttons and their numpad
	// equivalents so the user can drive the menu with either hand position.
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			if(grid) grid->moveCursor(-1, 0);
			break;

		case BTN_RIGHT:
		case BTN_6:
			if(grid) grid->moveCursor(+1, 0);
			break;

		case BTN_2:
			if(grid) grid->moveCursor(0, -1);
			break;

		case BTN_8:
			if(grid) grid->moveCursor(0, +1);
			break;

		case BTN_ENTER:
			// SELECT - flash the left softkey label (S21) so the user gets
			// a visible "click" cue; then dispatch to the host-supplied
			// handler. The handler reads getSelectedIcon() to decide where
			// to go. If no handler is wired (S19 default) we silently no-op
			// so the screen is still navigable for visual / hardware testing.
			if(softKeys) softKeys->flashLeft();
			if(selectCb) selectCb(this);
			break;

		case BTN_0:
			// S22: a *short* press of 0 on the menu has no semantic action
			// today (the menu is grid-navigated, not number-keyed). We
			// still listen for the press so the long-press detection that
			// the input service emits later actually fires - InputListener
			// will only call buttonHeld() for keys whose press was seen.
			zeroLongFired = false;
			break;

		case BTN_BACK:
			// S22: defer the actual short-press BACK action to
			// buttonReleased so it does not double-fire when a long-press
			// already locked the device. The flash still happens here so
			// the user sees an immediate "click" cue for any press, even
			// one that turns into a long-press.
			backLongFired = false;
			if(softKeys) softKeys->flashRight();
			break;

		default:
			break;
	}
}

void PhoneMainMenu::buttonReleased(uint i) {
	// S22: short-press dispatch for BTN_BACK so a long-press that already
	// fired (and locked the device) does not also pop back to the home
	// screen on key release. BTN_0 short-press is a no-op on this screen
	// for now, but we still clear the long-fired flag for cleanliness.
	switch(i){
		case BTN_BACK:
			if(!backLongFired){
				if(backCb){
					backCb(this);
				}else{
					// S66: route through PhoneTransitions so the menu->home
					// pop uses the Drill gesture pop direction. Mirror of
					// IntroScreen home->menu push (also Drill) so the SE-
					// style flick stays consistent on both edges.
					PhoneTransitions::pop(this, PhoneTransition::Drill);
				}
			}
			backLongFired = false;
			break;

		case BTN_0:
			zeroLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneMainMenu::buttonHeld(uint i) {
	// S22: long-press shortcuts. Same wiring as PhoneHomeScreen so the
	// gestures feel identical from either screen:
	//   - Hold 0    -> quick-dial (host-supplied callback).
	//   - Hold Back -> lock the device (host-supplied callback). The
	//                  short-press BACK is then suppressed in
	//                  buttonReleased() via backLongFired.
	switch(i){
		case BTN_0:
			zeroLongFired = true;
			if(softKeys) softKeys->flashLeft();
			if(quickDialCb) quickDialCb(this);
			break;

		case BTN_BACK:
			backLongFired = true;
			// Right softkey is already flashed by the press path; we do
			// not flash again here so we don't double-blink it.
			if(lockHoldCb) lockHoldCb(this);
			break;

		default:
			break;
	}
}
