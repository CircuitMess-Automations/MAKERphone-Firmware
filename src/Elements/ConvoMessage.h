#ifndef CHATTER_FIRMWARE_CONVOMESSAGE_H
#define CHATTER_FIRMWARE_CONVOMESSAGE_H

#include <string>
#include "../Interface/LVObject.h"
#include "../Model/Message.h"

class PhoneChatBubble;
class PhonePixelAvatar;

class ConvoMessage : public LVObject{
public:
	ConvoMessage(lv_obj_t* parent, const Message& msg, uint16_t bgColor);
	virtual ~ConvoMessage();
	void setDelivered(bool delivered);
	void setHue(uint16_t hue);

	const Message& getMsg() const;
	void clearFocus();

private:
	// MAKERphone restyle (S29): TEXT messages now render as a
	// PhoneChatBubble (right-aligned Sent for outgoing, left-aligned
	// Received for incoming). PIC messages keep their original
	// alignment row to avoid touching the picture flow before the
	// dedicated camera/gallery sessions land.
	//
	// S30 adds: a 32x32 PhonePixelAvatar pinned to the row's top-left
	// for *received* TEXT messages so each incoming bubble visibly
	// belongs to its sender. Outgoing rows and PIC rows are unchanged
	// (no avatar) - "your own" messages don't need a self-avatar, and
	// the camera/gallery flow gets its own decorations later.
	PhoneChatBubble* bubble = nullptr;     // owned via LVGL parent->child cascade
	PhonePixelAvatar* avatar = nullptr;    // received TEXT only; LVGL parent-owned
	lv_obj_t*        picObj = nullptr;     // for PIC variant only
	lv_obj_t*        focusTarget = nullptr;// what wears the focused outline

	lv_style_t       focusedStyle;
	bool             focusedStyleInited = false;

	Message          msg;
};


#endif //CHATTER_FIRMWARE_CONVOMESSAGE_H
