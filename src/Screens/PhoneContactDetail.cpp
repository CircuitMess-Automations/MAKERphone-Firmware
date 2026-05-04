#include "PhoneContactDetail.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhonePixelAvatar.h"
#include "../Fonts/font.h"
#include "../Storage/PhoneContacts.h"
#include "PhoneContactRingtonePicker.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the contact detail reads visually as part of the same family. Inlined
// here per the established pattern (see PhoneIncomingCall.cpp etc.).
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// 160 x 128 layout. Status bar takes y = 0..9, caption sits at y = 11..18,
// avatar block centred at y = 22..53 (32 px tall), name centred under it
// (pixelbasic16 ~14 px tall) at y = 56, peer-id row at y = 72,
// divider at y = 82, action buttons at y = 88..104, softkey bar at y = 118.
static constexpr lv_coord_t kCaptionY    = 12;
static constexpr lv_coord_t kAvatarY     = 22;
static constexpr lv_coord_t kAvatarSize  = 32;
static constexpr lv_coord_t kNameY       = 56;
static constexpr lv_coord_t kNumberY     = 72;
static constexpr lv_coord_t kDividerY    = 82;
static constexpr lv_coord_t kButtonsY    = 88;
static constexpr lv_coord_t kButtonW     = 56;
static constexpr lv_coord_t kButtonH     = 16;

// Same long-press cadence the rest of the MAKERphone shell uses for the
// "hold BACK" gesture, so muscle memory transfers cleanly between Phase
// D / F screens.
static constexpr uint32_t kBackHoldMs = 600;

PhoneContactDetail::PhoneContactDetail(UID_t inUid,
									   const char* inName,
									   uint8_t inSeed,
									   bool inFavorite)
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  avatar(nullptr),
		  captionLabel(nullptr),
		  nameLabel(nullptr),
		  favLabel(nullptr),
		  numberLabel(nullptr),
		  divider(nullptr),
		  btnCall(nullptr),
		  btnMessage(nullptr),
		  btnCallLabel(nullptr),
		  btnMessageLabel(nullptr),
		  uid(inUid),
		  avatarSeed(inSeed),
		  favorite(inFavorite) {

	// Zero the name buffer up front so any early read returns a valid
	// c-string before copyName runs. Defensive against a future caller
	// that touches getName() between ctor steps.
	name[0] = '\0';
	copyName(inName);

	buildLayout();
}

PhoneContactDetail::PhoneContactDetail(UID_t inUid)
		: PhoneContactDetail(inUid,
							 PhoneContacts::displayNameOf(inUid),
							 PhoneContacts::avatarSeedOf(inUid),
							 PhoneContacts::isFavorite(inUid)) {
	// Delegating ctor - all the field setup lives in the explicit overload.
}

PhoneContactDetail::~PhoneContactDetail() {
	// All children parented to obj; LVScreen's destructor frees obj and
	// LVGL recursively frees the lv_obj_t backing storage. The avatar
	// (PhonePixelAvatar / LVObject) is parented to obj as well, so the
	// recursive free covers it - no manual delete here would be safe.
}

void PhoneContactDetail::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneContactDetail::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders -----

void PhoneContactDetail::buildLayout() {
	// Full-screen container, blank canvas - same pattern every Phone*
	// screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of LVGL's z-order so every other element
	// overlays it cleanly. Same z-order as PhoneIncomingCall.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	// Caption sits just under the status bar, cyan pixelbasic7 so it
	// reads as the screen header without competing with the name below.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "CONTACT");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);

	buildAvatarBlock();
	buildLabels();
	buildActionButtons();

	// Bottom: feature-phone soft-keys.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("CALL");
	softKeys->setRight("BACK");

	// Long-press detection on BTN_BACK so the user can hold it to bail
	// out to the homescreen. Short-press still fires the BACK softkey.
	setButtonHoldTime(BTN_BACK, kBackHoldMs);

	// S38: hold BTN_ENTER to open the contact editor. The hook is
	// optional - defaults to a no-op flash if nothing is bound, so a
	// host that does not want the gesture simply leaves setOnEdit
	// unset. The same long-press cadence as the BACK gesture so the
	// muscle memory transfers cleanly.
	setButtonHoldTime(BTN_ENTER, kBackHoldMs);

	// Initial focus paint.
	refreshFocus();
}

void PhoneContactDetail::buildAvatarBlock() {
	// 32x32 PhonePixelAvatar centred horizontally on the 160 px wide
	// display - x = (160 - 32) / 2 = 64. The widget is parented to obj
	// so the wallpaper beneath shows through the surrounding pixels.
	avatar = new PhonePixelAvatar(obj, avatarSeed);
	lv_obj_set_pos(avatar->getLvObj(),
				   (160 - kAvatarSize) / 2,
				   kAvatarY);
}

void PhoneContactDetail::buildLabels() {
	// Display name in pixelbasic16 - the focal element under the avatar.
	// Cream for non-favorite, sunset orange for favorites so the colour
	// alone communicates the favorite state at a glance.
	nameLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(nameLabel,
								favorite ? MP_ACCENT : MP_TEXT, 0);
	lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(nameLabel, 140);
	lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(nameLabel, name);
	lv_obj_set_align(nameLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(nameLabel, kNameY);

	// Tiny "*" favorite mark to the right of the name. Hidden when the
	// contact is not a favorite.
	favLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(favLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(favLabel, MP_ACCENT, 0);
	lv_label_set_text(favLabel, "*");
	lv_obj_align(favLabel, LV_ALIGN_TOP_RIGHT, -8, kNameY + 4);
	if(!favorite) lv_obj_add_flag(favLabel, LV_OBJ_FLAG_HIDDEN);

	// LoRa peer id rendered in 4-hex-digit groups under the name. UIDs
	// of 0 (sample / placeholder rows from S36's seed list) render as
	// "(no number)" in dim purple so the user has a clear visual cue.
	numberLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(numberLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(numberLabel,
								(uid == 0) ? MP_LABEL_DIM : MP_HIGHLIGHT, 0);
	char buf[24] = {0};
	formatPeerId(buf, sizeof(buf));
	lv_label_set_text(numberLabel, buf);
	lv_obj_set_align(numberLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(numberLabel, kNumberY);

	// Divider - a thin 1 px dim purple bar that visually separates the
	// id block from the action-button row. 100 px wide, centred.
	divider = lv_obj_create(obj);
	lv_obj_remove_style_all(divider);
	lv_obj_set_size(divider, 100, 1);
	lv_obj_set_align(divider, LV_ALIGN_TOP_MID);
	lv_obj_set_y(divider, kDividerY);
	lv_obj_set_style_bg_color(divider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(divider, LV_OPA_70, 0);
	lv_obj_set_style_border_width(divider, 0, 0);
}

void PhoneContactDetail::buildActionButtons() {
	// Two action buttons side-by-side. The CALL button is on the left
	// (mapped to BTN_LEFT_ARROW / BTN_2 focus + BTN_ENTER), MESSAGE is
	// on the right (BTN_RIGHT_ARROW / BTN_8 focus + BTN_ENTER). With
	// 56 px buttons centred symmetrically: total width = 56*2 + 8 (gap)
	// = 120, so left edge of the row sits at (160 - 120) / 2 = 20.
	const lv_coord_t leftX  = 20;
	const lv_coord_t rightX = leftX + kButtonW + 8;

	btnCall = lv_obj_create(obj);
	lv_obj_remove_style_all(btnCall);
	lv_obj_set_size(btnCall, kButtonW, kButtonH);
	lv_obj_set_pos(btnCall, leftX, kButtonsY);
	lv_obj_set_scrollbar_mode(btnCall, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(btnCall, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(btnCall, 3, 0);
	lv_obj_set_style_bg_color(btnCall, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(btnCall, LV_OPA_60, 0);
	lv_obj_set_style_border_color(btnCall, MP_DIM, 0);
	lv_obj_set_style_border_opa(btnCall, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(btnCall, 1, 0);

	btnCallLabel = lv_label_create(btnCall);
	lv_obj_set_style_text_font(btnCallLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(btnCallLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(btnCallLabel, "CALL");
	lv_obj_set_align(btnCallLabel, LV_ALIGN_CENTER);

	btnMessage = lv_obj_create(obj);
	lv_obj_remove_style_all(btnMessage);
	lv_obj_set_size(btnMessage, kButtonW, kButtonH);
	lv_obj_set_pos(btnMessage, rightX, kButtonsY);
	lv_obj_set_scrollbar_mode(btnMessage, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(btnMessage, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(btnMessage, 3, 0);
	lv_obj_set_style_bg_color(btnMessage, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(btnMessage, LV_OPA_60, 0);
	lv_obj_set_style_border_color(btnMessage, MP_DIM, 0);
	lv_obj_set_style_border_opa(btnMessage, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(btnMessage, 1, 0);

	btnMessageLabel = lv_label_create(btnMessage);
	lv_obj_set_style_text_font(btnMessageLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(btnMessageLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(btnMessageLabel, "MESSAGE");
	lv_obj_set_align(btnMessageLabel, LV_ALIGN_CENTER);
}

// ----- public API -----

void PhoneContactDetail::setOnCall(ActionHandler cb)    { callCb    = cb; }
void PhoneContactDetail::setOnMessage(ActionHandler cb) { messageCb = cb; }
void PhoneContactDetail::setOnBack(ActionHandler cb)    { backCb    = cb; }
void PhoneContactDetail::setOnEdit(ActionHandler cb)    { editCb    = cb; }

void PhoneContactDetail::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneContactDetail::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneContactDetail::setCaption(const char* text) {
	if(captionLabel) lv_label_set_text(captionLabel, text != nullptr ? text : "");
}

void PhoneContactDetail::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneContactDetail::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

// ----- helpers -----

void PhoneContactDetail::copyName(const char* src) {
	if(src == nullptr || src[0] == '\0') {
		strncpy(name, "UNKNOWN", MaxNameLen);
	}else{
		strncpy(name, src, MaxNameLen);
	}
	name[MaxNameLen] = '\0';
}

void PhoneContactDetail::formatPeerId(char* out, size_t outLen) const {
	// "Phone-number"-style rendering of the LoRa peer id:
	// "+ABCD EFGH IJKL MNOP". The leading '+' echoes the international
	// dialling-code convention real feature-phones use, so the line
	// reads as a number rather than a hex blob. UID 0 (the sample
	// rows from S36's fallback list) renders as a placeholder caption.
	if(out == nullptr || outLen == 0) return;
	if(uid == 0) {
		strncpy(out, "(no number)", outLen);
		out[outLen - 1] = '\0';
		return;
	}
	const uint64_t v = (uint64_t) uid;
	const uint16_t g3 = (uint16_t)((v >>  0) & 0xFFFF);
	const uint16_t g2 = (uint16_t)((v >> 16) & 0xFFFF);
	const uint16_t g1 = (uint16_t)((v >> 32) & 0xFFFF);
	const uint16_t g0 = (uint16_t)((v >> 48) & 0xFFFF);
	snprintf(out, outLen, "+%04X %04X %04X %04X", g0, g1, g2, g3);
}

void PhoneContactDetail::refreshFocus() {
	// Focused button: sunset orange border + cream label. Unfocused:
	// dim purple border + dim purple label. Single-property swap per
	// element keeps the redraw cost trivial.
	const bool callFocused = (focus == Focus::Call);

	if(btnCall) {
		lv_obj_set_style_border_color(btnCall,
									  callFocused ? MP_ACCENT : MP_DIM, 0);
	}
	if(btnCallLabel) {
		lv_obj_set_style_text_color(btnCallLabel,
									callFocused ? MP_TEXT : MP_LABEL_DIM, 0);
	}
	if(btnMessage) {
		lv_obj_set_style_border_color(btnMessage,
									  callFocused ? MP_DIM : MP_ACCENT, 0);
	}
	if(btnMessageLabel) {
		lv_obj_set_style_text_color(btnMessageLabel,
									callFocused ? MP_LABEL_DIM : MP_TEXT, 0);
	}
}

void PhoneContactDetail::invokeFocusedAction() {
	if(focus == Focus::Call) {
		if(softKeys) softKeys->flashLeft();
		if(callCb) callCb(this);
	}else{
		// MESSAGE has no softkey, so a flash on either side would feel
		// wrong here. We just dispatch.
		if(messageCb) messageCb(this);
	}
}

// ----- input -----

void PhoneContactDetail::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_2:
			// Move focus to the CALL button.
			if(focus != Focus::Call) {
				focus = Focus::Call;
				refreshFocus();
			}
			break;

		case BTN_RIGHT:
		case BTN_8:
			// Move focus to the MESSAGE button.
			if(focus != Focus::Message) {
				focus = Focus::Message;
				refreshFocus();
			}
			break;

		case BTN_ENTER:
			// Activate whichever action button is currently focused.
			invokeFocusedAction();
			break;

		case BTN_R:
			// Right bumper - shortcut to fire the CALL softkey directly,
			// matching the "muscle-memory CALL" affordance every other
			// Phase D screen offers.
			if(softKeys) softKeys->flashLeft();
			if(callCb) callCb(this);
			break;

		case BTN_L:
			// Left bumper - shortcut to fire the MESSAGE action.
			if(messageCb) messageCb(this);
			break;

		case BTN_5:
			// S153 — open the per-contact ringtone picker. BTN_5 is
			// otherwise unbound on this screen (CALL / MESSAGE focus
			// already lives on BTN_2 / BTN_8 + bumpers). The picker
			// reads + persists the new id itself, so the detail
			// screen does not have to thread anything through.
			if(uid != 0) {
				push(new PhoneContactRingtonePicker(uid));
			}else if(softKeys) {
				softKeys->flashLeft();
			}
			break;

		case BTN_BACK:
			// Defer the short-press behaviour to buttonReleased so a
			// long-press can pre-empt it. backHoldFired is reset on
			// each fresh press so a new hold cycle starts clean.
			backHoldFired = false;
			break;

		default:
			break;
	}
}

void PhoneContactDetail::buttonHeld(uint i) {
	if(i == BTN_ENTER) {
		// S38: hold-ENTER = open the contact editor. The default
		// behaviour with no callback wired is a flash on the LEFT
		// softkey so the user gets a visible cue that the gesture
		// was recognised even before any host wires the callback.
		if(softKeys) softKeys->flashLeft();
		if(editCb) editCb(this);
		return;
	}
	if(i == BTN_BACK) {
		// Hold-BACK = bail to homescreen. We pop our own screen here
		// (the parent will continue the unwind chain when the user
		// holds again, mirroring PhoneDialerScreen's behaviour).
		backHoldFired = true;
		if(softKeys) softKeys->flashRight();
		if(backCb) {
			backCb(this);
		}else{
			pop();
		}
	}
}

void PhoneContactDetail::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		// Short-press BACK - fire the BACK softkey unless a hold has
		// already done so on the same cycle.
		if(backHoldFired) return;
		if(softKeys) softKeys->flashRight();
		if(backCb) {
			backCb(this);
		}else{
			pop();
		}
	}
}
