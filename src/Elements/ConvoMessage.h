#ifndef CHATTER_FIRMWARE_CONVOMESSAGE_H
#define CHATTER_FIRMWARE_CONVOMESSAGE_H

#include <string>
#include "../Interface/LVObject.h"
#include "../Model/Message.h"

class PhoneChatBubble;

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
	PhoneChatBubble* bubble = nullptr;     // owned via LVGL parent->child cascade
	lv_obj_t*        picObj = nullptr;     // for PIC variant only
	lv_obj_t*        focusTarget = nullptr;// what wears the focused outline

	lv_style_t       focusedStyle;
	bool             focusedStyleInited = false;

	Message          msg;
};


#endif //CHATTER_FIRMWARE_CONVOMESSAGE_H
