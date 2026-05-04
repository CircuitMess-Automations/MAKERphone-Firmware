#include "PhoneSpeedDialScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <string.h>
#include <stdio.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Storage/Storage.h"
#include "../Storage/PhoneContacts.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneSettingsScreen.cpp / PhoneCallHistory.cpp /
// PhoneOperatorScreen.cpp). The speed-dial screen uses cyan for the
// list-mode caption (informational), sunset orange for the pick-mode
// caption (live edit), warm cream for the row labels, dim purple for
// the digit prefix + chevron + "(unset)" placeholder, and the muted-
// purple translucent cursor rect for the focused row.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (pick caption + chevron on focus)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan (list caption)
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (row label, real name)
#define MP_DIM          lv_color_make( 70,  56, 100)   // muted purple (cursor highlight)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple (digit prefix, "(unset)" placeholder)

// List-area geometry. The two child containers (digitContainer +
// pickContainer) sit at the same x/y/width and just toggle visibility
// on mode change so the cursor highlight rect lives inside whichever
// view is active. kListX = 4 leaves a comfortable 4 px margin on each
// side; kColNumX / kColNameX leave room for the digit prefix on the
// left of every digit row.
static constexpr lv_coord_t kListX     = 4;
static constexpr lv_coord_t kColNumX   = 4;            // digit ("1".."9") column
static constexpr lv_coord_t kColNameX  = 18;           // contact-name column
static constexpr lv_coord_t kColPickX  = 6;            // pick-list label column

// Placeholder shown in a digit row whose Settings.speedDial[d] is 0.
// Kept as a single inline literal so the readable look matches the
// "(Clear)" label the pick view uses for the clear-slot sentinel.
static const char* const kUnsetText = "(unset)";
static const char* const kClearText = "(Clear)";

// Caption strings for the two modes. Cached as locals because the
// pick-mode caption is rebuilt with the slot number ("SLOT 3 - PICK
// CONTACT") on every entry into pick mode.
static const char* const kListCaption = "SPEED DIAL";

PhoneSpeedDialScreen::PhoneSpeedDialScreen()
		: LVScreen() {

	// Zero the per-row visible-window arrays so a partial constructor
	// abort (an LVGL alloc failure mid-build) leaves nullptrs rather
	// than dangling reads in refreshPickRows() / refreshDigitHighlight().
	for(uint8_t i = 0; i < SlotCount; ++i) {
		digitRows[i].numLabel  = nullptr;
		digitRows[i].nameLabel = nullptr;
		digitRows[i].y         = 0;
	}
	for(uint8_t i = 0; i < PickVisibleRows; ++i) {
		pickRows[i].label = nullptr;
		pickRows[i].y     = 0;
	}

	// Full-screen container, no scrollbars, no inner padding - same
	// blank-canvas pattern every other Phone* screen uses. Children
	// below are anchored manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the rest of the screen
	// reads on top of the synthwave gradient like every other Phase-D /
	// Phase-J screen.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery bar (10 px) so the user
	// always knows the device is alive while editing speed-dial slots.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildDigitView();
	buildPickView();

	// Bottom: feature-phone soft-keys. EDIT in list mode opens the
	// pick view for the focused slot; PICK in pick mode commits the
	// focused contact + persists. BACK pops back to settings (list
	// mode) or cancels the pick (pick mode).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("EDIT");
	softKeys->setRight("BACK");

	// Open in list mode with the cursor on slot 1 (the first editable
	// digit). Mirror the rest of the SYSTEM-cluster screens that
	// always boot to a defined cursor position.
	enterListMode();
}

PhoneSpeedDialScreen::~PhoneSpeedDialScreen() {
	// All children (wallpaper, statusBar, softKeys, captionLabel,
	// digitContainer + its children, pickContainer + its children)
	// are parented to obj - LVGL frees them recursively when the
	// screen's obj is destroyed by the LVScreen base destructor.
	// Nothing manual.
}

void PhoneSpeedDialScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneSpeedDialScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders ---------------------------------------------------------

void PhoneSpeedDialScreen::buildCaption() {
	// Caption above the list. Colour and text both flip on mode change
	// (cyan + "SPEED DIAL" in list mode; sunset orange + "SLOT N -
	// PICK CONTACT" in pick mode) -- refreshCaption() owns the swap so
	// the constructor only has to set the typography + initial align.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, kListCaption);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneSpeedDialScreen::buildDigitView() {
	// Container hosting the highlight rect + 9 digit rows. Geometry is
	// fixed: 9 rows * DigitRowH = 90 px tall, sitting inside the 98 px
	// window between ListY and the soft-key bar at y = 118.
	const lv_coord_t totalH = SlotCount * DigitRowH;

	digitContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(digitContainer);
	lv_obj_set_size(digitContainer, ListW, totalH);
	lv_obj_set_pos(digitContainer, kListX, ListY);
	lv_obj_set_scrollbar_mode(digitContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(digitContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(digitContainer, 0, 0);
	lv_obj_set_style_bg_opa(digitContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(digitContainer, 0, 0);

	// Cursor highlight - same translucent muted-purple rect every
	// other phone-style list screen uses, sized to one DigitRowH so
	// the focused row reads as selected without dominating the column.
	digitHighlight = lv_obj_create(digitContainer);
	lv_obj_remove_style_all(digitHighlight);
	lv_obj_set_size(digitHighlight, ListW, DigitRowH);
	lv_obj_set_pos(digitHighlight, 0, 0);
	lv_obj_set_scrollbar_mode(digitHighlight, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(digitHighlight, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(digitHighlight, 2, 0);
	lv_obj_set_style_bg_color(digitHighlight, MP_DIM, 0);
	lv_obj_set_style_bg_opa(digitHighlight, LV_OPA_70, 0);
	lv_obj_set_style_border_color(digitHighlight, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_opa(digitHighlight, LV_OPA_50, 0);
	lv_obj_set_style_border_width(digitHighlight, 1, 0);

	// Build the 9 rows. Each row carries a digit label (dim purple,
	// "1".."9") in the left column and a contact-name label (warm
	// cream when assigned, dim purple "(unset)" when empty) in the
	// right column. Both use pixelbasic7 so the row reads cleanly at
	// the 10 px row height.
	lv_coord_t y = 0;
	for(uint8_t i = 0; i < SlotCount; ++i) {
		DigitRow& r = digitRows[i];
		r.y = y;

		r.numLabel = lv_label_create(digitContainer);
		lv_obj_set_style_text_font(r.numLabel, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.numLabel, MP_LABEL_DIM, 0);
		// (i + 1) so the row labels read "1".."9" rather than "0".."8".
		// Slot 0 is reserved (S22 quick-dial gesture) and not editable
		// from this screen, so the digit array intentionally starts
		// at 1.
		char numBuf[4];
		snprintf(numBuf, sizeof(numBuf), "%u", (unsigned) (i + 1));
		lv_label_set_text(r.numLabel, numBuf);
		lv_obj_set_pos(r.numLabel, kColNumX, y + 1);

		r.nameLabel = lv_label_create(digitContainer);
		lv_obj_set_style_text_font(r.nameLabel, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.nameLabel, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(r.nameLabel, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r.nameLabel, ListW - kColNameX - 4);
		lv_label_set_text(r.nameLabel, kUnsetText);
		lv_obj_set_pos(r.nameLabel, kColNameX, y + 1);

		y += DigitRowH;
	}

	// Seed the rows with the persisted slot contents so the screen
	// reads correctly the moment it opens, even on a freshly-flashed
	// device with all slots zeroed out (which renders as 9 "(unset)"
	// rows, the factory-default look).
	refreshAllDigitRows();
}

void PhoneSpeedDialScreen::buildPickView() {
	// Container hosting the highlight rect + PickVisibleRows label
	// rows. Geometry is fixed: PickVisibleRows * PickRowH = 88 px tall,
	// sitting inside the same 98 px window the digit view occupies.
	const lv_coord_t totalH = PickVisibleRows * PickRowH;

	pickContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(pickContainer);
	lv_obj_set_size(pickContainer, ListW, totalH);
	lv_obj_set_pos(pickContainer, kListX, ListY);
	lv_obj_set_scrollbar_mode(pickContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(pickContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(pickContainer, 0, 0);
	lv_obj_set_style_bg_opa(pickContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(pickContainer, 0, 0);
	lv_obj_add_flag(pickContainer, LV_OBJ_FLAG_HIDDEN);

	// Cursor highlight (one PickRowH tall) -- same look as the digit
	// view's highlight so the muscle memory carries between modes.
	pickHighlight = lv_obj_create(pickContainer);
	lv_obj_remove_style_all(pickHighlight);
	lv_obj_set_size(pickHighlight, ListW, PickRowH);
	lv_obj_set_pos(pickHighlight, 0, 0);
	lv_obj_set_scrollbar_mode(pickHighlight, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(pickHighlight, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(pickHighlight, 2, 0);
	lv_obj_set_style_bg_color(pickHighlight, MP_DIM, 0);
	lv_obj_set_style_bg_opa(pickHighlight, LV_OPA_70, 0);
	lv_obj_set_style_border_color(pickHighlight, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(pickHighlight, LV_OPA_50, 0);
	lv_obj_set_style_border_width(pickHighlight, 1, 0);

	// Build the visible-window labels once. The labels' text is
	// rebuilt by refreshPickRows() each time the window scrolls or
	// the underlying entries change.
	lv_coord_t y = 0;
	for(uint8_t i = 0; i < PickVisibleRows; ++i) {
		PickRow& r = pickRows[i];
		r.y = y;

		r.label = lv_label_create(pickContainer);
		lv_obj_set_style_text_font(r.label, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.label, MP_TEXT, 0);
		lv_label_set_long_mode(r.label, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r.label, ListW - kColPickX - 4);
		lv_label_set_text(r.label, "");
		lv_obj_set_pos(r.label, kColPickX, y + 2);

		y += PickRowH;
	}
}

// ----- digit view repaint ----------------------------------------------

void PhoneSpeedDialScreen::refreshDigitRow(uint8_t slotIdx) {
	if(slotIdx >= SlotCount) return;
	DigitRow& r = digitRows[slotIdx];
	if(r.nameLabel == nullptr) return;

	// Slot 0 is reserved -- the row layout starts at slot 1, so the
	// underlying Settings index is (slotIdx + 1).
	const uint8_t digit = slotIdx + 1;
	const UID_t uid     = Settings.get().speedDial[digit];

	if(uid == 0) {
		// Empty slot -- dim "(unset)" placeholder so the row still
		// reads as "this is editable, just nothing here yet".
		lv_label_set_text(r.nameLabel, kUnsetText);
		lv_obj_set_style_text_color(r.nameLabel, MP_LABEL_DIM, 0);
		return;
	}

	// Look up a friendly display name. PhoneContacts::displayNameOf
	// handles the override-first-then-Friend-broadcast-then-fallback
	// lookup transparently so a user-edited contact name shows up
	// immediately. If the peer is no longer in the Friends repo the
	// helper returns the placeholder "Contact" string -- we keep it
	// rather than collapsing back to "(unset)" because the slot is
	// in fact still assigned (Settings.speedDial[d] != 0); a user
	// who wants to clear it can always EDIT -> "(Clear)".
	const char* name = PhoneContacts::displayNameOf(uid);
	if(name == nullptr || name[0] == '\0') name = "Contact";
	lv_label_set_text(r.nameLabel, name);
	lv_obj_set_style_text_color(r.nameLabel, MP_TEXT, 0);
}

void PhoneSpeedDialScreen::refreshAllDigitRows() {
	for(uint8_t i = 0; i < SlotCount; ++i) refreshDigitRow(i);
}

void PhoneSpeedDialScreen::refreshDigitHighlight() {
	if(digitHighlight == nullptr) return;
	if(cursor >= SlotCount) cursor = 0;
	lv_obj_set_y(digitHighlight, digitRows[cursor].y);

	// Bias the focused row's digit label toward the sunset-orange
	// accent so the cursor reads as a proper "you are here" indicator
	// even before the user moves it -- the rest of the digit prefixes
	// stay dim purple. Cheap (one style call per row) and matches the
	// PhoneSettingsScreen chevron-recolour pattern.
	for(uint8_t i = 0; i < SlotCount; ++i) {
		if(digitRows[i].numLabel == nullptr) continue;
		lv_obj_set_style_text_color(digitRows[i].numLabel,
									(i == cursor) ? MP_ACCENT : MP_LABEL_DIM,
									0);
	}
}

// ----- pick view repaint -----------------------------------------------

void PhoneSpeedDialScreen::rebuildPickEntries() {
	// Always reseed from scratch -- this is called every time the
	// user enters pick mode, so a freshly-paired peer shows up in the
	// list without needing a screen reload.
	pickEntries.clear();

	// Sentinel "(Clear)" entry first so cursor index 0 always means
	// "wipe this slot". Carries uid = 0 so commitPickedSlot() can
	// recognise it without a separate flag.
	{
		PickEntry e;
		e.uid = 0;
		strncpy(e.name, kClearText, sizeof(e.name) - 1);
		e.name[sizeof(e.name) - 1] = '\0';
		pickEntries.push_back(e);
	}

	// Walk the Friends repo, skipping the device's own efuse-mac id
	// (legacy convention -- the user never wants to "speed dial"
	// themselves). PhoneContacts::displayNameOf returns the user-edited
	// override when set, falling back to the broadcast nickname and
	// then the literal "Contact" placeholder.
	const UID_t selfUid = (UID_t) ESP.getEfuseMac();
	for(UID_t uid : Storage.Friends.all()) {
		if(uid == selfUid) continue;
		if(uid == 0)       continue;
		if(pickEntries.size() >= MaxPickEntries) break;

		PickEntry e;
		e.uid = uid;
		const char* name = PhoneContacts::displayNameOf(uid);
		if(name == nullptr || name[0] == '\0') name = "Contact";
		strncpy(e.name, name, sizeof(e.name) - 1);
		e.name[sizeof(e.name) - 1] = '\0';
		pickEntries.push_back(e);
	}

	// Reset the visible window + cursor. Default cursor of 0 lands on
	// "(Clear)" which is intentional: it's the most common destructive
	// action we want easy access to, and the user's first arrow press
	// puts them on the first real contact.
	pickWindowTop = 0;
	cursor        = 0;
}

void PhoneSpeedDialScreen::refreshPickRows() {
	const uint8_t total = (uint8_t) pickEntries.size();
	for(uint8_t i = 0; i < PickVisibleRows; ++i) {
		PickRow& r = pickRows[i];
		if(r.label == nullptr) continue;

		const uint8_t entryIdx = pickWindowTop + i;
		if(entryIdx >= total) {
			// Past the end of the data -- blank the row out so the
			// short list does not leak stale text from a previous
			// rebuild.
			lv_label_set_text(r.label, "");
			lv_obj_add_flag(r.label, LV_OBJ_FLAG_HIDDEN);
			continue;
		}

		lv_obj_clear_flag(r.label, LV_OBJ_FLAG_HIDDEN);

		const PickEntry& e = pickEntries[entryIdx];
		lv_label_set_text(r.label, e.name);

		// "(Clear)" sentinel renders dim purple so it visually reads
		// as a destructive / placeholder action; real contacts stay
		// warm cream.
		lv_obj_set_style_text_color(r.label,
									(e.uid == 0) ? MP_LABEL_DIM : MP_TEXT,
									0);
	}
}

void PhoneSpeedDialScreen::refreshPickHighlight() {
	if(pickHighlight == nullptr) return;

	const uint8_t total = (uint8_t) pickEntries.size();
	if(total == 0) {
		// Defensive: rebuild always inserts at least the "(Clear)"
		// sentinel so this path is unreachable today, but keep the
		// guard so a future refactor that empties the list does not
		// crash on a divide-by-zero / OOB highlight position.
		lv_obj_set_y(pickHighlight, 0);
		return;
	}
	if(cursor >= total) cursor = total - 1;

	// Auto-scroll so the cursor stays inside the visible window.
	if(cursor < pickWindowTop) {
		pickWindowTop = cursor;
		refreshPickRows();
	} else if(cursor >= pickWindowTop + PickVisibleRows) {
		pickWindowTop = cursor - (PickVisibleRows - 1);
		refreshPickRows();
	}

	const uint8_t visibleIdx = cursor - pickWindowTop;
	lv_obj_set_y(pickHighlight, pickRows[visibleIdx].y);
}

// ----- caption + softkey labels ----------------------------------------

void PhoneSpeedDialScreen::refreshCaption() {
	if(captionLabel == nullptr) return;
	if(mode == Mode::List) {
		lv_label_set_text(captionLabel, kListCaption);
		lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	} else {
		// "SLOT 3 - PICK CONTACT" reads at a glance which slot the
		// user is editing; sunset orange flags the screen as "live
		// edit" so it visually pops above the otherwise calm cyan
		// caption the rest of the SYSTEM-cluster screens use.
		char buf[32];
		snprintf(buf, sizeof(buf), "SLOT %u - PICK CONTACT",
				 (unsigned) editingDigit);
		lv_label_set_text(captionLabel, buf);
		lv_obj_set_style_text_color(captionLabel, MP_ACCENT, 0);
	}
}

void PhoneSpeedDialScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	if(mode == Mode::List) {
		softKeys->setLeft("EDIT");
		softKeys->setRight("BACK");
	} else {
		softKeys->setLeft("PICK");
		softKeys->setRight("BACK");
	}
}

// ----- mode + cursor ---------------------------------------------------

void PhoneSpeedDialScreen::enterListMode() {
	mode   = Mode::List;
	cursor = (cursor < SlotCount) ? cursor : 0;

	if(digitContainer != nullptr) lv_obj_clear_flag(digitContainer, LV_OBJ_FLAG_HIDDEN);
	if(pickContainer  != nullptr) lv_obj_add_flag(pickContainer, LV_OBJ_FLAG_HIDDEN);

	// Refresh the rows in case the user came back from a commit -- the
	// just-edited slot needs to show its new name immediately.
	refreshAllDigitRows();
	refreshDigitHighlight();
	refreshCaption();
	refreshSoftKeys();
}

void PhoneSpeedDialScreen::enterPickMode(uint8_t digit) {
	if(digit < 1 || digit > SlotCount) return;
	editingDigit = digit;
	mode         = Mode::Pick;

	if(digitContainer != nullptr) lv_obj_add_flag(digitContainer, LV_OBJ_FLAG_HIDDEN);
	if(pickContainer  != nullptr) lv_obj_clear_flag(pickContainer, LV_OBJ_FLAG_HIDDEN);

	rebuildPickEntries();
	refreshPickRows();
	refreshPickHighlight();
	refreshCaption();
	refreshSoftKeys();
}

void PhoneSpeedDialScreen::moveCursorBy(int8_t delta) {
	const uint8_t total = (mode == Mode::List)
		? SlotCount
		: (uint8_t) pickEntries.size();
	if(total == 0) return;

	int16_t next = static_cast<int16_t>(cursor) + delta;
	while(next < 0)                            next += total;
	while(next >= static_cast<int16_t>(total)) next -= total;
	cursor = static_cast<uint8_t>(next);

	if(mode == Mode::List) refreshDigitHighlight();
	else                   refreshPickHighlight();
}

void PhoneSpeedDialScreen::jumpCursorToDigit(uint8_t digit) {
	// In list mode, BTN_1..BTN_9 should jump the cursor to the
	// matching slot. Slot 0 is reserved so BTN_0 is intentionally
	// not handled here -- it falls through to the screen's default
	// no-op for unknown buttons.
	if(mode != Mode::List) return;
	if(digit < 1 || digit > SlotCount) return;
	cursor = digit - 1;
	refreshDigitHighlight();
}

// ----- commit ----------------------------------------------------------

void PhoneSpeedDialScreen::commitPickedSlot() {
	if(mode != Mode::Pick) return;
	if(pickEntries.empty()) {
		// Defensive -- should never happen because rebuildPickEntries
		// always inserts the "(Clear)" sentinel. Bail without mutation
		// rather than risk indexing past the end of the buffer.
		enterListMode();
		return;
	}

	const uint8_t safeCursor = (cursor < pickEntries.size())
		? cursor
		: (uint8_t) (pickEntries.size() - 1);
	const PickEntry& e = pickEntries[safeCursor];

	// Mutate Settings + persist in one shot. The S151 gesture half on
	// PhoneHomeScreen reads Settings.speedDial[digit] live on every
	// long-press, so the new assignment is in effect from the next
	// homescreen press without any extra wiring.
	Settings.get().speedDial[editingDigit] = e.uid;
	Settings.store();

	// Land back in list mode focused on the slot we just edited so
	// the user can immediately see the result of the commit and chain
	// another EDIT into a different slot if they want.
	cursor = editingDigit - 1;
	enterListMode();
}

// ----- input -----------------------------------------------------------

void PhoneSpeedDialScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_2:
		case BTN_LEFT:
			moveCursorBy(-1);
			return;

		case BTN_8:
		case BTN_RIGHT:
			moveCursorBy(+1);
			return;

		case BTN_ENTER:
			if(mode == Mode::List) {
				if(softKeys) softKeys->flashLeft();
				// Slot 0 is reserved -- digit row index 0 corresponds
				// to slot 1, so we add 1 here to map cursor to the
				// editable slot range.
				enterPickMode(cursor + 1);
			} else {
				if(softKeys) softKeys->flashLeft();
				commitPickedSlot();
			}
			return;

		case BTN_BACK:
			if(mode == Mode::Pick) {
				// Cancel -- return to list mode without persisting,
				// and put the cursor back on the slot we were editing
				// so the user can try again with a different contact.
				if(softKeys) softKeys->flashRight();
				cursor = editingDigit - 1;
				enterListMode();
			} else {
				// List mode -- pop back to PhoneSettingsScreen. The
				// settings screen restores its own cursor so the user
				// lands back on the "Speed dial" row they came from.
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		default:
			break;
	}

	// In list mode, BTN_1..BTN_9 jump the cursor to the matching slot
	// so the user can leap straight to slot 7 without arrow-walking.
	// BTN_0 is intentionally not handled (slot 0 is reserved). Pick
	// mode ignores digit keys entirely -- the contact list is
	// alphabetic, not digit-keyed.
	if(mode == Mode::List) {
		if(i == BTN_1) jumpCursorToDigit(1);
		else if(i == BTN_2) jumpCursorToDigit(2);
		else if(i == BTN_3) jumpCursorToDigit(3);
		else if(i == BTN_4) jumpCursorToDigit(4);
		else if(i == BTN_5) jumpCursorToDigit(5);
		else if(i == BTN_6) jumpCursorToDigit(6);
		else if(i == BTN_7) jumpCursorToDigit(7);
		else if(i == BTN_8) jumpCursorToDigit(8);
		else if(i == BTN_9) jumpCursorToDigit(9);
	}
}
