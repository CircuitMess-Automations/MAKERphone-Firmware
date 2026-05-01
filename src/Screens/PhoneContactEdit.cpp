#include "PhoneContactEdit.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhonePixelAvatar.h"
#include "../Elements/PhoneT9Input.h"
#include "../Fonts/font.h"
#include "../Storage/PhoneContacts.h"

// MAKERphone retro palette - kept identical to every other Phone* widget
// so the editor reads visually as part of the same family. Inlined here
// per the established pattern (see PhoneContactDetail.cpp etc.).
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// 160 x 128 layout. Vertical budget:
//   y = 0..9    PhoneStatusBar (10 px)
//   y = 11..40  PhoneT9Input   (30 px = Height + HelpHeight)
//   y = 42..49  "AVATAR" header (pixelbasic7, ~8 px tall)
//   y = 51..82  avatar row 0   (32 px)
//   y = 84..115 avatar row 1   (32 px, 1 px gutter)
//   y = 118..127 PhoneSoftKeyBar (10 px)
// Two-pixel slack between row 1 and the soft-key bar prevents the bar
// from clipping the bottom of the avatars.
//
// The grid is 4 columns x 2 rows. With 32 px avatars and 2 px column
// gutters a row is 4*32 + 3*2 = 134 px wide; centre that in the 160 px
// display - leftX = (160 - 134) / 2 = 13.
//
// The "EDIT CONTACT" header that the docstring sketches is omitted in
// favour of the AVATAR section header so the form fits inside the
// budget without overlapping the soft-key bar; the screen identity is
// still legible from the form contents alone.
static constexpr lv_coord_t kT9InputY        = 11;
static constexpr lv_coord_t kAvatarHeaderY   = 42;
static constexpr lv_coord_t kAvatarGridX     = 13;
static constexpr lv_coord_t kAvatarGridY0    = 51;
static constexpr lv_coord_t kAvatarRowGap    = 1;
static constexpr lv_coord_t kAvatarGridY1    = kAvatarGridY0 + 32 + kAvatarRowGap;
static constexpr lv_coord_t kAvatarColGap    = 2;
static constexpr lv_coord_t kAvatarSize      = 32;

// Same long-press cadence the rest of the MAKERphone shell uses for the
// "hold BACK" gesture, so muscle memory transfers cleanly between Phase
// D / F screens.
static constexpr uint32_t kBackHoldMs = 600;

// Eight curated PhonePixelAvatar seeds. Chosen so the resulting designs
// look meaningfully different to each other across the 4x2 grid (skin,
// hair, mouth, eye combos all rotate). The PhonePixelAvatar mix function
// is deterministic in seed, so these eight entries always render the
// same eight characters on every device.
const uint8_t PhoneContactEdit::kAvatarSeeds[PhoneContactEdit::AvatarCount] = {
		7, 41, 73, 109, 137, 173, 209, 241
};

PhoneContactEdit::PhoneContactEdit(UID_t inUid,
								   const char* inName,
								   uint8_t inAvatarSeed)
		: LVScreen(),
		  uid(inUid) {
	initSeedsFrom(inAvatarSeed);
	buildLayout();
	buildNameInput(inName);
	refreshFocus();
}

PhoneContactEdit::PhoneContactEdit(UID_t inUid)
		: PhoneContactEdit(inUid,
						   PhoneContacts::displayNameOf(inUid),
						   PhoneContacts::avatarSeedOf(inUid)) {
	// Delegating ctor.
}

PhoneContactEdit::~PhoneContactEdit() {
	// All children are parented to obj; LVScreen's destructor frees obj
	// and LVGL recursively frees the lv_obj_t tree underneath. The
	// PhoneT9Input destructor also cancels its commit + caret timers,
	// so a tear-down mid-cycle does not leak callbacks pointing into
	// freed memory.
}

void PhoneContactEdit::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneContactEdit::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- helpers -----

void PhoneContactEdit::initSeedsFrom(uint8_t initialSeed) {
	// Default the picker to the curated eight seeds.
	for(uint8_t i = 0; i < AvatarCount; ++i) {
		seeds[i] = kAvatarSeeds[i];
	}

	// If the contact's existing seed is already in the table, select
	// that slot; otherwise replace slot 0 with the existing seed so
	// the user does not lose their current avatar by opening the
	// editor.
	int8_t found = -1;
	for(uint8_t i = 0; i < AvatarCount; ++i) {
		if(seeds[i] == initialSeed) {
			found = (int8_t) i;
			break;
		}
	}
	if(found >= 0) {
		selectedAvatar = (uint8_t) found;
	}else{
		seeds[0] = initialSeed;
		selectedAvatar = 0;
	}
}

// ----- builders -----

void PhoneContactEdit::buildLayout() {
	// Full-screen container, blank canvas - same pattern every Phone*
	// screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of LVGL's z-order so every other element
	// overlays it cleanly. Same z-order as PhoneContactDetail.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	buildHeaders();
	buildAvatarGrid();

	// Bottom: feature-phone soft-keys.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("SAVE");
	softKeys->setRight("BACK");

	// Long-press detection on BTN_BACK so the user can hold it to bail
	// out to the homescreen. Short-press still fires the BACK softkey.
	setButtonHoldTime(BTN_BACK, kBackHoldMs);
}

void PhoneContactEdit::buildHeaders() {
	// "AVATAR" section header sits between the T9 input and the
	// avatar grid. Cream when AVATAR is the active focus zone, dim
	// otherwise - refreshFocus() flips the colour.
	avatarSectionHdr = lv_label_create(obj);
	lv_obj_set_style_text_font(avatarSectionHdr, &pixelbasic7, 0);
	lv_obj_set_style_text_color(avatarSectionHdr, MP_LABEL_DIM, 0);
	lv_label_set_text(avatarSectionHdr, "AVATAR");
	lv_obj_set_align(avatarSectionHdr, LV_ALIGN_TOP_MID);
	lv_obj_set_y(avatarSectionHdr, kAvatarHeaderY);
}

void PhoneContactEdit::buildNameInput(const char* initialName) {
	// PhoneT9Input is the canonical S32 multi-tap entry. We build it
	// at the configured Y and let the widget self-size from its
	// internal Width / Height constants.
	nameInput = new PhoneT9Input(obj, MaxNameLen);
	lv_obj_set_pos(nameInput->getLvObj(),
				   (160 - PhoneT9Input::Width) / 2,
				   kT9InputY);
	nameInput->setPlaceholder("NAME");
	nameInput->setCase(PhoneT9Input::Case::First);
	if(initialName != nullptr && initialName[0] != '\0') {
		nameInput->setText(String(initialName));
	}
}

void PhoneContactEdit::buildAvatarGrid() {
	// Stamp out the 4x2 grid of PhonePixelAvatar widgets. The
	// per-slot positions are computed up-front so a future re-layout
	// only has to tweak the top-level constants.
	for(uint8_t i = 0; i < AvatarCount; ++i) {
		const uint8_t col = i % 4;
		const uint8_t row = i / 4;
		const lv_coord_t x = kAvatarGridX +
				col * (kAvatarSize + kAvatarColGap);
		const lv_coord_t y = (row == 0) ? kAvatarGridY0 : kAvatarGridY1;

		avatars[i] = new PhonePixelAvatar(obj, seeds[i]);
		lv_obj_set_pos(avatars[i]->getLvObj(), x, y);
	}
	refreshAvatarSelection();
}

// ----- public API -----

void PhoneContactEdit::setOnSave(SaveHandler cb)   { saveCb = cb; }
void PhoneContactEdit::setOnBack(ActionHandler cb) { backCb = cb; }

void PhoneContactEdit::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneContactEdit::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneContactEdit::setCaption(const char* text) {
	// Caption is reserved for callers that want to display a custom
	// header (e.g. "ADD CONTACT" instead of the implicit "EDIT
	// CONTACT" tone of the form). The screen drops the default
	// caption from the layout to fit the avatar grid above the
	// soft-key bar; setting one here re-introduces the label, parented
	// to the form so it sits above the T9 input.
	if(text == nullptr || text[0] == '\0') {
		if(captionLabel) {
			lv_obj_add_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
		}
		return;
	}
	if(captionLabel == nullptr) {
		captionLabel = lv_label_create(obj);
		lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
		lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
		lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
		lv_obj_set_y(captionLabel, 1);
	}
	lv_obj_clear_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(captionLabel, text);
}

void PhoneContactEdit::copyCurrentName(char* out, size_t outLen) const {
	// `PhoneT9Input::getText()` returns the buffer by value, so we
	// must copy out of the live widget rather than handing the caller
	// a pointer that dangles after the temporary String is destroyed.
	if(out == nullptr || outLen == 0) return;
	if(nameInput == nullptr) {
		out[0] = '\0';
		return;
	}
	String snapshot = nameInput->getText();
	const size_t copyLen = (snapshot.length() < outLen - 1)
			? (size_t) snapshot.length()
			: outLen - 1;
	memcpy(out, snapshot.c_str(), copyLen);
	out[copyLen] = '\0';
}

uint8_t PhoneContactEdit::getCurrentSeed() const {
	if(selectedAvatar >= AvatarCount) return seeds[0];
	return seeds[selectedAvatar];
}

void PhoneContactEdit::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneContactEdit::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

// ----- focus + selection -----

void PhoneContactEdit::refreshFocus() {
	// The active section paints its header in the cream MP_TEXT, the
	// inactive one sits in the dim MP_LABEL_DIM. The T9 input itself
	// already shows a caret + pending strip when typed into, so the
	// header swap is the only cue we need for the NAME side.
	const bool nameActive   = (focus == Focus::Name);
	const bool avatarActive = (focus == Focus::Avatar);

	if(avatarSectionHdr) {
		lv_obj_set_style_text_color(avatarSectionHdr,
									avatarActive ? MP_TEXT : MP_LABEL_DIM, 0);
	}

	// PhoneT9Input does not expose a "focused" mode, but we do want a
	// visible cue. Tweaking the caret colour by toggling the case
	// label is too noisy; instead we show / hide the placeholder hint
	// (which the widget already supports). When NAME is the active
	// zone we keep the standard placeholder; when the user moves
	// focus to AVATAR we keep the existing text but the avatar header
	// brightening is the visual pivot.
	(void) nameActive; // documented intentionally as the inverse cue.
}

void PhoneContactEdit::refreshAvatarSelection() {
	for(uint8_t i = 0; i < AvatarCount; ++i) {
		if(avatars[i] == nullptr) continue;
		avatars[i]->setSelected(i == selectedAvatar);
	}
}

void PhoneContactEdit::cycleAvatar(int8_t direction) {
	if(direction == 0) return;
	int16_t next = (int16_t) selectedAvatar + (int16_t) direction;
	while(next < 0) next += AvatarCount;
	while(next >= (int16_t) AvatarCount) next -= AvatarCount;
	selectedAvatar = (uint8_t) next;
	refreshAvatarSelection();
}

void PhoneContactEdit::invokeSave() {
	if(softKeys) softKeys->flashLeft();

	// Force the T9 widget to commit any in-flight pending letter so
	// the visible name == saved name. Otherwise a user who taps
	// digits and immediately hits SAVE would lose their last letter.
	if(nameInput) nameInput->commitPending();

	// Snapshot the current name into a stack buffer before calling
	// into the persistence layer. The widget owns the live buffer,
	// but we want a stable c-string for both the PhoneContacts call
	// and the user-supplied callback.
	char finalName[MaxNameLen + 1] = {0};
	copyCurrentName(finalName, sizeof(finalName));
	const uint8_t finalSeed = getCurrentSeed();

	// Persist through the standard PhoneContacts helpers when we have
	// a real uid to write against. Sample / placeholder rows from the
	// S36 fallback list have uid==0 and skip the writes.
	if(uid != 0) {
		PhoneContacts::setDisplayName(uid,
									  (finalName[0] == '\0') ? nullptr : finalName);
		PhoneContacts::setAvatarSeed(uid, finalSeed);
	}

	if(saveCb) {
		saveCb(this, finalName, finalSeed);
	}else{
		// No host wired - default behaviour is pop().
		pop();
	}
}

// ----- input -----

void PhoneContactEdit::buttonPressed(uint i) {
	switch(i) {
		// Numeric input always feeds the T9 entry, regardless of
		// which focus zone is active. Typing while AVATAR is focused
		// still updates the name buffer - the user does not need to
		// switch back explicitly to type a letter.
		case BTN_0: if(nameInput) nameInput->keyPress('0'); break;
		case BTN_1: if(nameInput) nameInput->keyPress('1'); break;
		case BTN_2: if(nameInput) nameInput->keyPress('2'); break;
		case BTN_3: if(nameInput) nameInput->keyPress('3'); break;
		case BTN_4: if(nameInput) nameInput->keyPress('4'); break;
		case BTN_5: if(nameInput) nameInput->keyPress('5'); break;
		case BTN_6: if(nameInput) nameInput->keyPress('6'); break;
		case BTN_7: if(nameInput) nameInput->keyPress('7'); break;
		case BTN_8: if(nameInput) nameInput->keyPress('8'); break;
		case BTN_9: if(nameInput) nameInput->keyPress('9'); break;

		case BTN_L:
			// Bumper L: T9 backspace when NAME is focused, previous
			// avatar when AVATAR is focused.
			if(focus == Focus::Name) {
				if(nameInput) nameInput->keyPress('*');
			}else{
				cycleAvatar(-1);
			}
			break;

		case BTN_R:
			// Bumper R: T9 case toggle when NAME is focused, next
			// avatar when AVATAR is focused.
			if(focus == Focus::Name) {
				if(nameInput) nameInput->keyPress('#');
			}else{
				cycleAvatar(+1);
			}
			break;

		case BTN_ENTER:
			// Toggle which section is focused. We auto-commit any
			// pending T9 letter when we leave NAME so the user does
			// not silently lose an in-flight character mid-edit.
			if(focus == Focus::Name) {
				if(nameInput) nameInput->commitPending();
				focus = Focus::Avatar;
			}else{
				focus = Focus::Name;
			}
			refreshFocus();
			break;

		case BTN_LEFT:
			// SAVE softkey - commits + persists + fires the save
			// callback. The flash happens inside invokeSave().
			invokeSave();
			break;

		case BTN_RIGHT:
			// BACK softkey - defer the short-press behaviour to
			// buttonReleased so a long-press can pre-empt it.
			backHoldFired = false;
			break;

		default:
			break;
	}
}

void PhoneContactEdit::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		// Hold-BACK = bail to homescreen. We pop our own screen here
		// (the parent will continue the unwind chain when the user
		// holds again, mirroring PhoneContactDetail's behaviour).
		backHoldFired = true;
		if(softKeys) softKeys->flashRight();
		if(backCb) {
			backCb(this);
		}else{
			pop();
		}
	}
}

void PhoneContactEdit::buttonReleased(uint i) {
	if(i == BTN_BACK || i == BTN_RIGHT) {
		// Short-press BACK / RIGHT softkey - fire the BACK action
		// unless a hold has already done so on the same cycle.
		if(backHoldFired) return;
		if(softKeys) softKeys->flashRight();
		if(backCb) {
			backCb(this);
		}else{
			pop();
		}
	}
}
