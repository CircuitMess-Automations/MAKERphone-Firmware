#ifndef MAKERPHONE_PHONENOTIFICATIONPREVIEW_H
#define MAKERPHONE_PHONENOTIFICATIONPREVIEW_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"
#include "../Services/MessageService.h"
#include "../Services/MissedCallLog.h"

/**
 * S49 — PhoneNotificationPreview
 *
 * Code-only summary widget that surfaces "what's waiting" on the lock
 * screen. It collapses two notification streams — unread SMS and missed
 * calls — into a single 156x26 px slab that reads at a glance:
 *
 *      +----------------------------------------+
 *      |  > 3 NEW MESSAGES        ALEX          |   <- cyan, when unread > 0
 *      |  x 2 MISSED CALLS         MOM          |   <- sunset, when missed > 0
 *      +----------------------------------------+
 *
 * Either line is hidden when its category is empty; the whole widget
 * collapses (LV_OBJ_FLAG_HIDDEN) when both streams are quiet so the
 * existing "Nothing new" centre label can take the floor.
 *
 * Implementation notes:
 *  - Anchored with LV_OBJ_FLAG_IGNORE_LAYOUT so the host screen can
 *    keep its flex/column layout untouched (same pattern as every
 *    other Phone* element).
 *  - Code-only — no SPIFFS bytes, no images. Glyphs are plain ASCII
 *    rendered in the shared `pixelbasic7` font so the panel fits the
 *    rest of the MAKERphone aesthetic at zero asset cost.
 *  - Self-binds to MessageService::UnreadListener and the new
 *    MissedCallLog listener, so the preview re-renders automatically
 *    whenever a new SMS lands or a call goes unanswered. The host does
 *    not have to poke `refresh()` on every frame.
 *  - `refresh()` is also exposed publicly so the host (LockScreen) can
 *    rebuild the preview when it becomes visible again — listeners
 *    fire only on *change*, not on screen attach.
 *  - The widget is positioned by the host (set the parent's anchor +
 *    setX/setY); the constructor only sets the bounding size.
 */
class PhoneNotificationPreview : public LVObject,
                                  private UnreadListener,
                                  private MissedCallLogListener {
public:
	explicit PhoneNotificationPreview(lv_obj_t* parent);
	virtual ~PhoneNotificationPreview();

	/**
	 * Re-pull both notification counts and re-render. Listeners already
	 * call this automatically on change — only useful when the host
	 * wants to rebuild the preview after toggling visibility.
	 */
	void refresh();

	/** True when neither unread messages nor missed calls are present. */
	bool isEmpty() const { return emptyState; }

	static constexpr uint16_t PreviewWidth  = 156;
	static constexpr uint16_t PreviewHeight = 26;

private:
	// One row per notification category. `messagesRow` (top, cyan) for
	// SMS, `callsRow` (bottom, sunset orange) for missed calls. Both
	// rows are independent containers so either can be hidden without
	// disturbing the other's layout.
	lv_obj_t* messagesRow   = nullptr;
	lv_obj_t* messagesGlyph = nullptr;
	lv_obj_t* messagesCount = nullptr;
	lv_obj_t* messagesPeer  = nullptr;

	lv_obj_t* callsRow      = nullptr;
	lv_obj_t* callsGlyph    = nullptr;
	lv_obj_t* callsCount    = nullptr;
	lv_obj_t* callsPeer     = nullptr;

	bool emptyState = true;

	void onUnread(bool unread) override;
	void onMissedCallsChanged() override;

	void buildRow(lv_obj_t*& row,
	              lv_obj_t*& glyph,
	              lv_obj_t*& countLabel,
	              lv_obj_t*& peerLabel,
	              const char* glyphText,
	              lv_color_t glyphColor,
	              int16_t y);

	/** Count unread Convos via Storage. Stops once the result is known. */
	uint8_t computeUnreadCount() const;

	/** Find the peer name of the newest unread Convo, or "" if none. */
	void copyLatestUnreadPeerName(char* dst, size_t dstSize) const;

	void renderMessagesRow(uint8_t unread);
	void renderCallsRow(uint8_t missed);

	/** Pluralise: "MESSAGE" vs "MESSAGES" / "CALL" vs "CALLS". */
	static const char* pluralise(uint8_t n,
	                             const char* singular,
	                             const char* plural);
};

#endif // MAKERPHONE_PHONENOTIFICATIONPREVIEW_H
