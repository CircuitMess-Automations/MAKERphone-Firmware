#ifndef CHATTER_FIRMWARE_CONVOSCREEN_H
#define CHATTER_FIRMWARE_CONVOSCREEN_H

#include <Arduino.h>
#include "../MAKERphoneConfig.h"
#include "../Interface/LVScreen.h"
#include "../Elements/TextEntry.h"
#include "../Types.hpp"
#include "../Model/Profile.hpp"
#include "../Model/Convo.hpp"
#include "../Elements/ConvoBox.h"
#include "../Modals/ContextMenu.h"
#include "../Elements/PicMenu.h"
#if MAKERPHONE_USE_T9_COMPOSER
#include "../Elements/PhoneT9Input.h"
#endif

class ConvoScreen : public LVScreen, private InputListener{
public:
	ConvoScreen(UID_t uid);
	void onStart() override;
	void onStop() override;
	void onStarting() override;

private:
	void buttonPressed(uint i) override;
	Friend fren;
	const UID_t convo = 0;

	void textEntryConfirm();
	void textEntryCancel();
	void textEntryLR();
	void convoBoxEnter();
	void convoBoxExit();
	void messageSelected(const Message& msg);
	void menuMessageSelected();
	void menuMessageCancel();
	void picMenuSelected();
	void picMenuCancel();

	void sendMessage();
	void buttonHeld(uint i) override;
	void buttonReleased(uint i) override;

	ConvoBox* convoBox;
#if MAKERPHONE_USE_T9_COMPOSER
	PhoneT9Input* phoneT9 = nullptr;
	bool t9Active = false;
#else
	TextEntry* textEntry;
#endif
	PicMenu* picMenu;
	ContextMenu* menuMessage;

	Message selectedMessage;
};


#endif //CHATTER_FIRMWARE_CONVOSCREEN_H
