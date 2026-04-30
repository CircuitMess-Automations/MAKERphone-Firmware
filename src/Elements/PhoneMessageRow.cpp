#include "PhoneMessageRow.h"

#include <string.h>

#include "../Fonts/font.h"
#include "../Storage/Storage.h"
#include "PhonePixelAvatar.h"

// MAKERphone retro palette - kept identical to every other Phone* widget
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneSynthwaveBg,
//  PhoneIconTile, PhoneDialerKey, PhoneDialerPad, PhonePixelAvatar,
//  PhoneChatBubble, PhoneCallHistory). One shared palette is the whole
// reason the MAKERphone reskin reads as a single coherent device UI even
// when widgets from very different surfaces share a screen.
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (NEW badge / focus border)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan (focus border alt)
#define MP_DIM          lv_color_make( 70,  56, 100)   // muted purple (focus fill)
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (name)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple (preview / "..." badge)

// ----- intra-row geometry (all in real px on the 152-wide row) -----
//
// Avatar lives in the leftmost 32 px column. The middle column hosts a
// stacked (name on top, preview below) text block. The right column
// hosts the unread / time badge, anchored to the row's right edge.
static constexpr lv_coord_t kAvatarX     = 0;
static constexpr lv_coord_t kAvatarY     = 0;
static constexpr lv_coord_t kAvatarSize  = 32;

static constexpr lv_coord_t kTextColX    = kAvatarX + kAvatarSize + 4;  // 36
static constexpr lv_coord_t kNameY       = 4;
static constexpr lv_coord_t kPreviewY    = 18;

static constexpr lv_coord_t kBadgeRight  = -2;          // -2 px from the row's right edge
static constexpr lv_coord_t kBadgeY      = 12;          // vertically centred next to text

// Width budget for the middle text column = total - avatar - gap - badge slot.
// The badge slot is reserved at 28 px so "NEW" + a 2 px padding fit on
// every line without bumping into the preview.
static constexpr lv_coord_t kBadgeSlot   = 28;
static constexpr lv_coord_t kTextColW    = PhoneMessageRow::RowWidth - kTextColX - kBadgeSlot - 2;
// = 152 - 36 - 28 - 2 = 86 px

// ----- ctor / dtor ---------------------------------------------------------

PhoneMessageRow::PhoneMessageRow(lv_obj_t* parent, const Friend& fren)
		: LVObject(parent), frenUID(fren.uid) {

	// User-data points at this so an LV_EVENT_CLICKED handler installed
	// by the host screen can recover the row's UID without juggling a
	// per-row LaunchParams struct on the heap. The host screen still
	// installs its own click callback - we only stash the pointer.
	lv_obj_set_user_data(obj, this);

	buildBackground();
	buildAvatar(fren.profile.avatar);
	buildLabels(fren.profile.nickname);

	// Initial preview + unread state come straight from storage so the
	// row paints correctly on first render even if no msgReceived /
	// onUnread events fire before the screen is shown.
	refreshPreview();
	refreshUnreadBadge();
	refreshSelectionVisual();

	// Selection watching: LVGL fires LV_EVENT_FOCUSED / LV_EVENT_DEFOCUSED
	// when the input group cursor lands / leaves the row, which is how
	// every screen-with-list in this codebase handles selection. We
	// piggyback that to keep the avatar's accent border in sync with the
	// row's own focus highlight.
	lv_obj_add_event_cb(obj, &PhoneMessageRow::onStateEvent, LV_EVENT_FOCUSED,   this);
	lv_obj_add_event_cb(obj, &PhoneMessageRow::onStateEvent, LV_EVENT_DEFOCUSED, this);

	// Live updates - same listener pair the legacy UserWithMessage uses.
	Messages.addReceivedListener(this);
	Messages.addUnreadListener(this);
	Profiles.addListener(this);
}

PhoneMessageRow::~PhoneMessageRow() {
	Messages.removeReceivedListener(this);
	Messages.removeUnreadListener(this);
	Profiles.removeListener(this);

	lv_style_reset(&styleDef);
	lv_style_reset(&styleFocus);
}

// ----- structural builders -------------------------------------------------

void PhoneMessageRow::buildBackground() {
	lv_obj_set_size(obj, RowWidth, RowHeight);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

	// Default style: transparent over the wallpaper, no border. The row
	// reads as part of the synthwave background until it gains focus.
	lv_style_init(&styleDef);
	lv_style_set_bg_opa(&styleDef, LV_OPA_TRANSP);
	lv_style_set_border_width(&styleDef, 0);
	lv_style_set_radius(&styleDef, 2);
	lv_style_set_pad_all(&styleDef, 0);
	lv_obj_add_style(obj, &styleDef, LV_PART_MAIN | LV_STATE_DEFAULT);

	// Focused style: muted purple slab + thin cyan rule, same affordance
	// PhoneCallHistory uses on its cursor highlight. The slab is at 60%
	// opacity so the wallpaper still bleeds through subtly - the row
	// looks "lit" rather than blocked.
	lv_style_init(&styleFocus);
	lv_style_set_bg_opa(&styleFocus, LV_OPA_60);
	lv_style_set_bg_color(&styleFocus, MP_DIM);
	lv_style_set_border_color(&styleFocus, MP_HIGHLIGHT);
	lv_style_set_border_opa(&styleFocus, LV_OPA_70);
	lv_style_set_border_width(&styleFocus, 1);
	lv_obj_add_style(obj, &styleFocus, LV_PART_MAIN | LV_STATE_FOCUSED);
}

void PhoneMessageRow::buildAvatar(uint8_t avatarSeed) {
	avatar = new PhonePixelAvatar(obj, avatarSeed);

	// Pin the avatar at the row's top-left. PhonePixelAvatar takes its
	// own 32x32 size, so we only set the position. IGNORE_LAYOUT keeps
	// it out of any flex pass the parent might still trigger.
	lv_obj_add_flag(avatar->getLvObj(), LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_pos(avatar->getLvObj(), kAvatarX, kAvatarY);
}

void PhoneMessageRow::buildLabels(const char* nickname) {
	// Name - pixelbasic7, warm cream, single-line dot truncation. Sits
	// at (kTextColX, kNameY). Width is the middle column width so a
	// long nickname clips with an ellipsis instead of pushing into the
	// badge slot.
	nameLabel = lv_label_create(obj);
	lv_obj_add_flag(nameLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nameLabel, MP_TEXT, 0);
	lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(nameLabel, kTextColW);
	lv_label_set_text(nameLabel, nickname != nullptr ? nickname : "");
	lv_obj_set_pos(nameLabel, kTextColX, kNameY);

	// 1-line preview - dim purple, also dot-truncated. Populated from
	// storage by refreshPreview(). The preview reads "noticeably softer"
	// than the name (MP_LABEL_DIM vs MP_TEXT) so the eye can flick down
	// the column scanning names without preview text fighting back.
	previewLabel = lv_label_create(obj);
	lv_obj_add_flag(previewLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(previewLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(previewLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(previewLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(previewLabel, kTextColW);
	lv_label_set_text(previewLabel, "");
	lv_obj_set_pos(previewLabel, kTextColX, kPreviewY);

	// Right-column badge - "NEW" when unread, "..." otherwise. Anchored
	// to the row's right edge with a small inset so the cyan focus rule
	// does not crowd the badge text.
	badgeLabel = lv_label_create(obj);
	lv_obj_add_flag(badgeLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(badgeLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(badgeLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(badgeLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(badgeLabel, "...");
	lv_obj_align(badgeLabel, LV_ALIGN_RIGHT_MID, kBadgeRight, 0);
}

// ----- public API ----------------------------------------------------------

void PhoneMessageRow::refresh() {
	refreshPreview();
	refreshUnreadBadge();
}

void PhoneMessageRow::setRightLabel(const char* text, lv_color_t color) {
	if(badgeLabel == nullptr) return;
	lv_label_set_text(badgeLabel, text != nullptr ? text : "");
	lv_obj_set_style_text_color(badgeLabel, color, 0);
}

// ----- internal refresh helpers -------------------------------------------

void PhoneMessageRow::refreshPreview() {
	if(previewLabel == nullptr) return;

	// Pull the most recent message from the message service. Empty
	// convo -> empty preview (which still leaves the name visible).
	Message msg = Messages.getLastMessage(frenUID);
	std::string text;

	if(msg.uid != 0) {
		if(msg.getType() == Message::TEXT) {
			text = msg.getText();
		} else if(msg.getType() == Message::PIC) {
			// "Meme" is the established convention used by UserWithMessage
			// for picture messages on the legacy inbox - keep parity so
			// the user does not see "Meme" become "[image]" or some other
			// renaming purely because we restyled the row.
			text = "Meme";
		}
	}

	// LV_LABEL_LONG_DOT handles the "..." when the string is wider than
	// the label width, so we simply drop the raw text in.
	lv_label_set_text(previewLabel, text.c_str());
}

void PhoneMessageRow::refreshUnreadBadge() {
	if(badgeLabel == nullptr) return;

	Convo convo = Storage.Convos.get(frenUID);
	const bool unread = (convo.uid != 0 && convo.unread);

	if(unread) {
		lv_label_set_text(badgeLabel, "NEW");
		lv_obj_set_style_text_color(badgeLabel, MP_ACCENT, 0);
	} else {
		// Three dots is the established "no info" placeholder elsewhere
		// in the firmware (see PhoneCallHistory's empty time column).
		// MP_LABEL_DIM keeps it from competing with the name text.
		lv_label_set_text(badgeLabel, "...");
		lv_obj_set_style_text_color(badgeLabel, MP_LABEL_DIM, 0);
	}
}

void PhoneMessageRow::refreshSelectionVisual() {
	if(avatar == nullptr) return;

	const bool focused =
			lv_obj_has_state(obj, LV_STATE_FOCUSED) ||
			lv_obj_has_state(obj, LV_STATE_FOCUS_KEY);

	avatar->setSelected(focused);
}

// ----- listener overrides --------------------------------------------------

void PhoneMessageRow::msgReceived(const Message& message) {
	if(message.convo != frenUID) return;
	refreshPreview();
	// Unread state is event-driven by onUnread - we do not flip the
	// badge here because Messages.markRead() / markUnread() will fire
	// onUnread for every listener including this one.
}

void PhoneMessageRow::onUnread(bool /*unread*/) {
	// The bool the service hands us is the *global* unread state across
	// all convos. The actual per-row truth lives in Storage.Convos, so
	// just re-query and update.
	refreshUnreadBadge();
}

void PhoneMessageRow::profileChanged(const Friend& fren) {
	if(fren.uid != frenUID) return;

	if(nameLabel != nullptr) {
		lv_label_set_text(nameLabel, fren.profile.nickname);
	}
	if(avatar != nullptr) {
		avatar->setSeed(fren.profile.avatar);
	}
}

// ----- LVGL focus event trampoline ----------------------------------------

void PhoneMessageRow::onStateEvent(lv_event_t* e) {
	auto* self = static_cast<PhoneMessageRow*>(lv_event_get_user_data(e));
	if(self == nullptr) return;
	self->refreshSelectionVisual();
}
