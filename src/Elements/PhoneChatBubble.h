#ifndef MAKERPHONE_PHONECHATBUBBLE_H
#define MAKERPHONE_PHONECHATBUBBLE_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneChatBubble
 *
 * Reusable retro feature-phone chat bubble for MAKERphone 2.0. It is the
 * Phase-A atom that the future ConvoScreen restyle (Phase E, S29) will
 * stack to render incoming and outgoing SMS-like messages in the
 * Sony-Ericsson silhouette:
 *
 *   |  +------------------+               |
 *   |  | hey, you there?  |               |     <- Received: left, muted purple
 *   |  +------------------+               |
 *   |  ##                                 |        small notch tail at bottom-left
 *   |  #          08:42                   |        optional grey timestamp below
 *   |               +-----------------+   |
 *   |               | yep, just woke  |   |     <- Sent: right, sunset orange
 *   |               | up              |   |
 *   |               +-----------------+   |
 *   |                                ##   |        notch tail at bottom-right
 *   |                       08:43    #    |        timestamp anchored to right
 *
 * Implementation notes:
 *  - 100% code-only - the bubble is one rounded `lv_obj` with a label
 *    inside, plus three 1px-tall rectangles that form the stair-stepped
 *    tail. Same primitive style as PhoneSoftKeyBar's softkey arrows. No
 *    SPIFFS assets, no canvas backing buffers, zero data partition cost.
 *  - The bubble auto-sizes to its text content but is capped at
 *    MaxBubbleWidth (120 px), which leaves 40 px on the 160 px display
 *    for alignment slack and tail/timestamp positioning. Long messages
 *    wrap inside the bubble using LV_LABEL_LONG_WRAP.
 *  - The widget is a full-width row (lv_pct(100)) with explicit height
 *    so it cooperates cleanly with a flex-column parent (the future
 *    ConvoScreen scroll list) without depending on SIZE_CONTENT
 *    measuring children that we manually position. The tail and the
 *    optional timestamp are children of the row container with
 *    IGNORE_LAYOUT, then anchored relative to the bubble after each
 *    layout pass via relayout().
 *  - The Sent variant uses MP_ACCENT (sunset orange) on dark text, the
 *    Received variant uses MP_DIM (muted purple) on warm cream text -
 *    same palette every other Phone* widget already uses, so a screen
 *    that mixes a status bar, a chat bubble, and a softkey bar reads as
 *    one coherent device UI.
 *  - setText / setTimestamp / setShowTail re-run relayout() in place so
 *    callers can mutate the bubble after construction (e.g. toggling
 *    the tail when stacking multiple bubbles from the same sender, or
 *    revealing the timestamp once a message is acknowledged).
 */
class PhoneChatBubble : public LVObject {
public:
	enum class Variant : uint8_t {
		Sent,      // outgoing - right-aligned, sunset orange
		Received   // incoming - left-aligned, muted purple
	};

	/**
	 * Phase-E S34: per-message delivery status for outgoing bubbles.
	 *
	 *   None       - no indicator drawn (default; also forced for
	 *                Received bubbles regardless of caller input).
	 *   Pending    - small clock face. The message has been queued
	 *                locally but not yet handed off to the LoRa radio.
	 *   Sent       - single tick. The packet was transmitted; we have
	 *                not yet seen an ACK from the recipient.
	 *   Delivered  - double tick. We received an ACK from the peer
	 *                (corresponds to `Message::received == true` for
	 *                outgoing messages in the existing MessageService
	 *                model).
	 */
	enum class Status : uint8_t {
		None,
		Pending,
		Sent,
		Delivered
	};

	/**
	 * Build a chat bubble inside `parent`.
	 *
	 * @param parent     LVGL parent (typically a flex-column scroll list).
	 * @param variant    Sent (outgoing) or Received (incoming).
	 * @param text       Initial bubble text. nullptr is treated as "".
	 * @param timestamp  Optional timestamp string (e.g. "08:42"). nullptr
	 *                   or "" hides the timestamp slot entirely.
	 */
	PhoneChatBubble(lv_obj_t* parent, Variant variant,
					const char* text = "", const char* timestamp = nullptr);
	virtual ~PhoneChatBubble() = default;

	/** Replace the bubble text and re-flow. */
	void setText(const char* text);

	/**
	 * Replace the timestamp label below the bubble. Pass nullptr or an
	 * empty string to hide it (collapses the row's height).
	 */
	void setTimestamp(const char* timestamp);

	/** Show/hide the small triangular tail at the bubble's bottom corner. */
	void setShowTail(bool show);

	/**
	 * Set the delivery status indicator for a Sent bubble. Calls on a
	 * Received bubble are accepted but render nothing - the indicator
	 * is intentionally one-sided so a stray setStatus(Delivered) flowing
	 * through ConvoBox::msgChanged() can't accidentally tag the peer's
	 * bubbles too.
	 */
	void setStatus(Status status);

	Variant getVariant() const { return variant; }
	bool    hasTail()    const { return showTail; }
	Status  getStatus()  const { return status; }

	/**
	 * Inner LVGL object that draws the rounded bubble fill. Exposed so
	 * a screen-level wrapper (e.g. ConvoScreen's per-message focus
	 * highlight) can attach extra styles or state flags directly to the
	 * bubble without needing to know its construction internals.
	 */
	lv_obj_t* getBubbleObj() const { return bubble; }

	// 160 px display - leave 40 px of slack so the bubble never touches the
	// opposite side and the tail/timestamp have room to breathe.
	static constexpr uint16_t MaxBubbleWidth = 120;
	// Inside-bubble padding (kept tight so a 4-char message still looks
	// like a bubble, not a square block).
	static constexpr uint8_t  BubblePadH = 3;
	static constexpr uint8_t  BubblePadV = 2;
	// How far the bubble sits from the row's left/right edge - matches the
	// tail width so the stair-stepped tail fits flush against the screen
	// margin without overflowing.
	static constexpr uint8_t  SideMargin = 4;
	// Stair-stepped tail: 3 rows of 3, 2, 1 px stacked downward.
	static constexpr uint8_t  TailHeight = 3;
	// Vertical gap between the tail and the timestamp below it.
	static constexpr uint8_t  TimestampGap = 1;

	// Status indicator metrics (S34). Designed to fit in the existing
	// "below the bubble's tail" line that already hosts the optional
	// timestamp, so adding status doesn't grow the message row beyond
	// what the timestamp variant already produces.
	//   StatusBoxW - widest of the three glyph variants (the double
	//                tick is 7 px wide; pending/single tick are 5 px
	//                and centered inside the box).
	//   StatusBoxH - 4 rows tall, exactly matching pixelbasic7's cap
	//                height so the timestamp baseline lines up.
	//   StatusGap  - horizontal gap between the timestamp and the
	//                status icon when both are shown.
	static constexpr uint8_t  StatusBoxW = 7;
	static constexpr uint8_t  StatusBoxH = 5;
	static constexpr uint8_t  StatusGap  = 2;

private:
	Variant   variant;
	bool      showTail = true;
	bool      hasTimestamp = false;
	Status    status = Status::None;

	lv_obj_t* bubble        = nullptr;
	lv_obj_t* label         = nullptr;
	lv_obj_t* tailRect[3]   = { nullptr, nullptr, nullptr };
	lv_obj_t* timestampLabel = nullptr;
	// Status indicator host. Children are wiped + rebuilt by
	// renderStatus() each time the status changes; the box itself
	// stays alive for the bubble's full lifetime so relayout() can
	// reposition it without touching the icon contents.
	lv_obj_t* statusBox     = nullptr;

	void buildContainer();
	void buildBubble();
	void buildTail();
	void buildTimestamp();
	void buildStatus();

	/**
	 * Wipe the statusBox and re-render the glyph for the current
	 * `status`. No-op for Received bubbles (the box stays hidden).
	 * Called from setStatus() and from relayout() so the bubble's
	 * indicator survives a text change that triggered a re-flow.
	 */
	void renderStatus();

	/**
	 * Helper used by renderStatus(): create a single 1x1 colored
	 * "pixel" inside `parent` at (x, y). The statusBox uses these
	 * to draw clock and tick glyphs without a canvas backing buffer.
	 */
	lv_obj_t* statusPixel(lv_obj_t* parent, int16_t x, int16_t y, lv_color_t color);

	/**
	 * Re-measure the bubble, then anchor the tail rectangles and the
	 * optional timestamp relative to it. Also sets the row container's
	 * explicit height so a flex-column parent can stack bubbles cleanly.
	 */
	void relayout();
};

#endif //MAKERPHONE_PHONECHATBUBBLE_H
