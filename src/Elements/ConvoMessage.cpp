#include "ConvoMessage.h"
#include "PhoneChatBubble.h"
#include "PhonePixelAvatar.h"
#include <Arduino.h>
#include "../Interface/Pics.h"

// MAKERphone palette - shared with every other Phone* widget (PhoneStatusBar,
// PhoneSoftKeyBar, PhoneChatBubble, PhoneIconTile, PhoneDialerKey, ...). Keeping
// the focus-outline color matched to the ChatterTheme highlight so a focused
// message reads as the same kind of selection as a focused softkey or menu tile.
#define MP_HIGHLIGHT lv_color_make(122, 232, 255)   // cyan

ConvoMessage::ConvoMessage(lv_obj_t* parent, const Message& msg, uint16_t bgColor)
		: LVObject(parent), msg(msg){

	const bool outgoing = msg.outgoing;
	(void) bgColor; // hue ignored - MAKERphone bubbles use the fixed retro palette.

	// Outer row: a transparent full-width container that just delegates
	// alignment to whichever child builds the actual message visual. The
	// flex-column scroll list in ConvoBox stacks these rows; we set
	// SIZE_CONTENT so each row is exactly as tall as its content (a
	// PhoneChatBubble auto-sizes itself, including its tail and any
	// future timestamp slot, via the bubble's internal relayout()).
	lv_obj_remove_style_all(obj);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_width(obj, lv_pct(100));
	lv_obj_set_height(obj, LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);

	if(msg.getType() == Message::TEXT){
		// PhoneChatBubble already renders a 100%-width row that aligns
		// the bubble end (right) for Sent and start (left) for Received,
		// so we don't need a second flex layer in ConvoMessage.
		bubble = new PhoneChatBubble(obj,
									 outgoing ? PhoneChatBubble::Variant::Sent
											  : PhoneChatBubble::Variant::Received,
									 msg.getText().c_str());
		focusTarget = bubble->getBubbleObj();

		// S30: received TEXT messages get a 32x32 pixel avatar pinned
		// to the row's top-left so each incoming bubble visibly belongs
		// to its sender. We do not change PhoneChatBubble itself --
		// instead we (a) add the avatar as an IGNORE_LAYOUT child of
		// the row (so it never participates in the flex pass that the
		// bubble's outer container does internally), and (b) bump the
		// bubble's outer container left padding so the bubble fill
		// shifts right past the avatar. The bubble's own relayout()
		// reads the bubble's actual x to anchor its tail / timestamp,
		// so the indent propagates without further plumbing.
		//
		// The seed is derived from the conversation UID (which equals
		// the friend's UID for received messages). PhonePixelAvatar
		// internally hashes the seed into independent hair/skin/eyes/
		// mouth/shirt salts, so the avatar is stable per-contact across
		// reboots without needing any persistence layer of our own.
		if(!outgoing){
			const uint64_t u = static_cast<uint64_t>(msg.convo);
			const uint8_t  seed = static_cast<uint8_t>(
				(u      ) ^ (u >>  8) ^ (u >> 16) ^ (u >> 24) ^
				(u >> 32) ^ (u >> 40) ^ (u >> 48) ^ (u >> 56));

			avatar = new PhonePixelAvatar(obj, seed);
			lv_obj_t* aObj = avatar->getLvObj();
			lv_obj_add_flag(aObj, LV_OBJ_FLAG_IGNORE_LAYOUT);
			// Pin at x = SideMargin to line up with the indent that the
			// Received bubble would have had. y = 0 keeps the avatar
			// top-aligned with the bubble (single-line bubbles are far
			// shorter than 32 px, so center-aligning would just hide
			// the avatar's hair behind a future status indicator).
			lv_obj_align(aObj, LV_ALIGN_TOP_LEFT,
						 PhoneChatBubble::SideMargin, 0);

			// Push the bubble's content right of the avatar. The
			// bubble's outer container already had pad_left = SideMargin
			// (the Received indent); we replace it with
			//   SideMargin + AvatarSize + AvatarBubbleGap
			// so the inner bubble fill starts a few pixels past the
			// avatar's right edge. Right pad is left untouched so long
			// messages still wrap at MaxBubbleWidth.
			constexpr uint8_t AvatarBubbleGap = 3;
			lv_obj_set_style_pad_left(
				bubble->getLvObj(),
				PhoneChatBubble::SideMargin
					+ PhonePixelAvatar::AvatarSize
					+ AvatarBubbleGap,
				0);
			// Force the bubble to re-anchor its tail/timestamp now that
			// the bubble's x has shifted. setText() with the same text
			// is the cheapest way to retrigger relayout() without
			// adding a public API to PhoneChatBubble.
			bubble->setText(msg.getText().c_str());

			// The avatar is 32 px tall; a one-line bubble is ~14 px.
			// Without a min-height the row would clip to the bubble's
			// height and the avatar would visibly bleed into the next
			// message. Min-height keeps SIZE_CONTENT growth intact for
			// long wrapped bubbles.
			lv_obj_set_style_min_height(obj,
				PhonePixelAvatar::AvatarSize, 0);
		}

	}else if(msg.getType() == Message::PIC){
		// PIC messages keep their existing alignment behavior. The pic
		// is created as a single child of an end/start-aligned flex row
		// so picture-stickers still fall on the correct screen side
		// without competing with PhoneChatBubble's own internal layout.
		lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(obj,
			outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
			LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_left(obj, 4, 0);
		lv_obj_set_style_pad_right(obj, 4, 0);

		Pic pic = Pics[msg.getPic()];
		picObj = pic.create(obj);
		focusTarget = picObj;
	}

	// Focus styling: subtle cyan outline around the focused element.
	// The full focus indicator is the row-level translate_x animation
	// driven by ConvoBox::startAnim(); the outline is the static
	// "you're hovering here" hint that survives the animation pause.
	if(focusTarget != nullptr){
		lv_style_init(&focusedStyle);
		focusedStyleInited = true;
		lv_style_set_outline_width(&focusedStyle, 1);
		lv_style_set_outline_color(&focusedStyle, MP_HIGHLIGHT);
		lv_style_set_outline_opa(&focusedStyle, LV_OPA_80);
		lv_style_set_outline_pad(&focusedStyle, 1);
		lv_obj_add_style(focusTarget, &focusedStyle,
						 LV_STATE_FOCUSED | LV_PART_MAIN);

		// LVGL focus events arrive on the row obj (LVSelectable adds it
		// to the input group); forward LV_STATE_FOCUSED to the actual
		// focus target so the outline appears around the bubble (or
		// pic), not the invisible row.
		lv_obj_add_event_cb(obj, [](lv_event_t* e){
			auto* self = static_cast<ConvoMessage*>(e->user_data);
			if(self->focusTarget){
				lv_obj_add_state(self->focusTarget, LV_STATE_FOCUSED);
			}
		}, LV_EVENT_FOCUSED, this);

		lv_obj_add_event_cb(obj, [](lv_event_t* e){
			auto* self = static_cast<ConvoMessage*>(e->user_data);
			if(self->focusTarget){
				lv_obj_clear_state(self->focusTarget, LV_STATE_FOCUSED);
			}
		}, LV_EVENT_DEFOCUSED, this);
	}
}

void ConvoMessage::setDelivered(bool delivered){
	if(!msg.outgoing) return;
	msg.received = delivered;
	// S34 will introduce real status indicators (clock=pending, single
	// tick=sent, double tick=delivered) on PhoneChatBubble. S29 just
	// stores the state - no visual change here so the bubble layout
	// stays stable across the upcoming status-icon work.
}

const Message& ConvoMessage::getMsg() const{
	return msg;
}

void ConvoMessage::clearFocus(){
	lv_obj_clear_state(obj, LV_STATE_FOCUSED);
	if(focusTarget){
		lv_obj_clear_state(focusTarget, LV_STATE_FOCUSED);
	}
}

void ConvoMessage::setHue(uint16_t hue){
	(void) hue;
	// MAKERphone bubbles use a single shared palette across every
	// conversation so the device-wide retro look stays consistent. The
	// per-friend hue that ChatterTheme used to mix into bubble fills is
	// intentionally ignored here. ConvoBox still passes it in (and may
	// keep using it for non-bubble decorations later), so the API is
	// preserved for backward compatibility.
}

ConvoMessage::~ConvoMessage(){
	if(focusedStyleInited){
		lv_style_reset(&focusedStyle);
		focusedStyleInited = false;
	}
	// PhoneChatBubble (if any) is owned by the LVGL parent->child
	// cascade: when our LVObject base destroys obj, the bubble's lv_obj
	// is destroyed too, and LVObject's LV_EVENT_DELETE handler deletes
	// the C++ wrapper. No manual `delete bubble` here - that would
	// race the cascade.
}
