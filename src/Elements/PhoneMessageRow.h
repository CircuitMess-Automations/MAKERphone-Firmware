#ifndef MAKERPHONE_PHONEMESSAGEROW_H
#define MAKERPHONE_PHONEMESSAGEROW_H

#include <Arduino.h>
#include <lvgl.h>
#include <string>

#include "../Interface/LVObject.h"
#include "../Model/Friend.hpp"
#include "../Services/MessageService.h"
#include "../Services/ProfileService.h"

class PhonePixelAvatar;

/**
 * PhoneMessageRow
 *
 * Reusable retro feature-phone "inbox row" for MAKERphone 2.0. It is
 * the visual atom of the new `InboxScreen` (Phase E, S31): one row per
 * conversation, stacked vertically inside a scrolling list container.
 *
 *   +------------------------------------------------+
 *   |  [AV]  ALEX KIM                            NEW |   <- unread
 *   |        hey, you up?                        ... |   <- preview
 *   +------------------------------------------------+
 *   ^^^^^^                ^^^^^^^^^^^^^^^^^^   ^^^^^^
 *   32x32 PhonePixelAvatar    name + 1-line       time /
 *   (seeded from profile)     dot-truncated          unread
 *   no SPIFFS bytes           preview                badge
 *
 * Implementation notes:
 *  - 100% code-only - the row is a `LVObject` slab (152x32) with a
 *    `PhonePixelAvatar` child plus three `lv_label`s. Same primitive
 *    pattern as `PhoneCallHistory`'s rows; zero SPIFFS asset cost.
 *  - The avatar is seeded from the friend's `profile.avatar` byte so the
 *    same friend always renders the same pixel face. Profile changes
 *    repaint via `ProfileListener`, mirroring the legacy `User` widget.
 *  - The preview text is the last text/pic message from the convo.
 *    `MsgReceivedListener` updates the preview live; `UnreadListener`
 *    toggles the right-column "NEW" badge in MP_ACCENT.
 *  - Selection styling matches every other Phone* widget: `LV_STATE_FOCUSED`
 *    paints the row in translucent MP_DIM with a thin MP_HIGHLIGHT cyan
 *    border, and the inner avatar gets its `setSelected(true)` accent
 *    border. Same affordance the dialer / menu screens use.
 *  - Width is fixed (152 px so it lives inside the 4-px-margin list
 *    container) so callers can stack rows in a flex-column without
 *    relying on `LV_PCT(100)` width measuring during construction -
 *    LVGL 8.x sometimes resolves percentage widths late, which would
 *    leave the right-column badge anchored in the wrong place on first
 *    paint. A fixed width side-steps the whole class of bug.
 *
 * Time-column note:
 *  - Messages have no persisted timestamp in this firmware version
 *    (`Message` only stores type + text + sender flags), so the "time"
 *    slot in the roadmap spec is rendered as a *state badge* for now:
 *    "NEW" in MP_ACCENT for unread convos and a dim "..." placeholder
 *    otherwise. When a future session introduces real message epochs,
 *    `setRightLabel(const char*, lv_color_t)` already exists to slot
 *    the formatted relative-time string into that same column.
 */
class PhoneMessageRow : public LVObject,
		private MsgReceivedListener,
		private UnreadListener,
		private ProfileListener {
public:
	PhoneMessageRow(lv_obj_t* parent, const Friend& fren);
	virtual ~PhoneMessageRow();

	/** UID of the friend this row represents (stable across profile updates). */
	UID_t getUID() const { return frenUID; }

	/** Force a refresh from storage (used on screen re-entry). */
	void refresh();

	/** Override the right-column badge text (e.g. once timestamps land). */
	void setRightLabel(const char* text, lv_color_t color);

	static constexpr lv_coord_t RowWidth  = 152;  // 160 - 2*4 px list margin
	static constexpr lv_coord_t RowHeight = 32;   // matches PhonePixelAvatar size

private:
	void buildBackground();
	void buildAvatar(uint8_t avatarSeed);
	void buildLabels(const char* nickname);
	void refreshUnreadBadge();
	void refreshPreview();
	void refreshSelectionVisual();

	void msgReceived(const Message& message) override;
	void onUnread(bool unread) override;
	void profileChanged(const Friend& fren) override;

	/** Selection-watch event handler installed on `obj`. */
	static void onStateEvent(lv_event_t* e);

	UID_t frenUID = 0;

	PhonePixelAvatar* avatar = nullptr;
	lv_obj_t* nameLabel    = nullptr;
	lv_obj_t* previewLabel = nullptr;
	lv_obj_t* badgeLabel   = nullptr;

	lv_style_t styleDef;
	lv_style_t styleFocus;
};

#endif //MAKERPHONE_PHONEMESSAGEROW_H
