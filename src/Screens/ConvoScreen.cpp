#include "ConvoScreen.h"
#include "../Elements/User.h"
#include <Input/Input.h>
#include <Pins.hpp>
#include <Loop/LoopManager.h>
#include "../Services/LoRaService.h"
#include "../Fonts/font.h"
#include "../Services/BuzzerService.h"

// S33 — When MAKERPHONE_USE_T9_COMPOSER is on (default), the message
// composer is the new `PhoneT9Input` widget rather than the legacy
// LVGL textarea-backed `TextEntry`. The two paths are kept in tight
// `#if` blocks below so a single flag flip in MAKERphoneConfig.h (or a
// `-D MAKERPHONE_USE_T9_COMPOSER=0` arduino-cli flag) reverts to the
// proven keyboard. Everything outside of those `#if` blocks (message
// list, picture menu, message context menu, friend header) is shared.
//
// Mapping summary (T9 path):
//   BTN_0..BTN_9  -> PhoneT9Input::keyPress('0'..'9') (multi-tap)
//   BTN_L         -> '*' (backspace)
//   BTN_R         -> '#' (case toggle abc -> Abc -> ABC)
//   BTN_ENTER     -> sendMessage()
//   BTN_BACK      -> backspace if buffer non-empty, else pop()
//   BTN_LEFT/RIGHT-> jump out of composer into convoBox to scroll msgs

ConvoScreen::ConvoScreen(UID_t uid) : convo(uid){
	fren = Storage.Friends.get(uid);
	Profile profile = fren.profile;

#if MAKERPHONE_USE_T9_COMPOSER
	// T9 composer is 156 px wide and we want it flush against the screen
	// edges so the pending-letter strip + case label aren't clipped. The
	// legacy TextEntry path lives at 152 px inside a bordered container,
	// so we conditionally reduce the outer chrome (padding 1, no inner
	// border) only when the T9 path is active. Net result: 160 - 2 = 158
	// px content width, with a 1 px halo around the new composer.
	lv_obj_set_style_pad_all(obj, 1, LV_PART_MAIN);
#else
	lv_obj_set_style_pad_all(obj, 3, LV_PART_MAIN);
#endif
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t* container = lv_obj_create(obj);
	lv_obj_set_size(container, lv_pct(100), lv_pct(100));
#if MAKERPHONE_USE_T9_COMPOSER
	lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
#else
	lv_obj_set_style_border_width(container, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(container, lv_color_white(), LV_PART_MAIN);
#endif
	lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);

	lv_obj_t* user = (new User(container, fren))->getLvObj();
	convoBox = new ConvoBox(container, uid, profile.hue);

#if MAKERPHONE_USE_T9_COMPOSER
	// New T9 composer. PhoneT9Input is a 156x30 self-contained slab
	// (entry strip + pending-letter help strip), so we anchor it as a
	// flex-children block and let the convoBox above flex-grow to fill
	// the remaining space - same column layout the legacy TextEntry
	// participated in.
	phoneT9 = new PhoneT9Input(container, 160);
	phoneT9->setPlaceholder("Message");
	phoneT9->setCase(PhoneT9Input::Case::First);

	// Mute the IGNORE_LAYOUT flag the widget sets internally on its
	// children so the widget itself can participate in the parent
	// flex column. PhoneT9Input's own root has IGNORE_LAYOUT cleared,
	// so a quick size reservation is enough - the parent column reads
	// the widget's natural size (Width x (Height + HelpHeight)).
	lv_obj_clear_flag(phoneT9->getLvObj(), LV_OBJ_FLAG_IGNORE_LAYOUT);
#else
	textEntry = new TextEntry(container, "", 60);
	textEntry->showCaps(true);
#endif

	lv_obj_set_style_border_width(user, 1, 0);
	lv_obj_set_style_border_color(user, lv_color_white(), 0);
	lv_obj_set_style_border_side(user, LV_BORDER_SIDE_BOTTOM, 0);

	lv_obj_set_flex_grow(convoBox->getLvObj(), 1);

#if !MAKERPHONE_USE_T9_COMPOSER
	lv_obj_set_style_bg_opa(textEntry->getLvObj(), LV_OPA_100, LV_PART_MAIN);
	lv_obj_set_style_bg_color(textEntry->getLvObj(), lv_color_white(), LV_PART_MAIN);
	lv_obj_set_style_pad_hor(textEntry->getLvObj(), 2, 0);
	lv_obj_set_style_pad_top(textEntry->getLvObj(), 1, 0);
	lv_obj_set_style_text_font(textEntry->getLvObj(), &lv_font_montserrat_14, 0);
	textEntry->setTextColor(lv_color_black());
#endif

	picMenu = new PicMenu(this);

	menuMessage = new ContextMenu(this);


#if !MAKERPHONE_USE_T9_COMPOSER
	lv_obj_add_event_cb(textEntry->getLvObj(), [](lv_event_t* e){
		static_cast<ConvoScreen*>(e->user_data)->textEntryConfirm();
	}, EV_ENTRY_DONE, this);

	lv_obj_add_event_cb(textEntry->getLvObj(), [](lv_event_t* e){
		static_cast<ConvoScreen*>(e->user_data)->textEntryCancel();
	}, EV_ENTRY_CANCEL, this);

	lv_obj_add_event_cb(textEntry->getLvObj(), [](lv_event_t* e){
		auto screen = static_cast<ConvoScreen*>(e->user_data);
		screen->textEntryLR();
	}, EV_ENTRY_LR, this);
#endif

	lv_obj_add_event_cb(convoBox->getLvObj(), [](lv_event_t* e){
		static_cast<ConvoScreen*>(e->user_data)->convoBoxEnter();
	}, LV_EVENT_CLICKED, this);

	lv_obj_add_event_cb(convoBox->getLvObj(), [](lv_event_t* e){
		static_cast<ConvoScreen*>(e->user_data)->convoBoxExit();
	}, EV_ENTRY_CANCEL, this);

	lv_obj_add_event_cb(convoBox->getLvObj(), [](lv_event_t* e){
		const auto& msg = static_cast<ConvoMessage*>(e->param)->getMsg();
		static_cast<ConvoScreen*>(e->user_data)->messageSelected(msg);
	}, EV_CONVOBOX_MSG_SELECTED, this);

	lv_obj_add_event_cb(menuMessage->getLvObj(), [](lv_event_t* e){
		static_cast<ConvoScreen*>(e->user_data)->menuMessageSelected();
	}, LV_EVENT_CLICKED, this);

	lv_obj_add_event_cb(menuMessage->getLvObj(), [](lv_event_t* e){
		static_cast<ConvoScreen*>(e->user_data)->menuMessageCancel();
	}, LV_EVENT_CANCEL, this);

	lv_obj_add_event_cb(picMenu->getLvObj(), [](lv_event_t* e){
		static_cast<ConvoScreen*>(e->user_data)->picMenuSelected();
	}, LV_EVENT_CLICKED, this);

	lv_obj_add_event_cb(picMenu->getLvObj(), [](lv_event_t* e){
		static_cast<ConvoScreen*>(e->user_data)->picMenuCancel();
	}, LV_EVENT_CANCEL, this);

}

void ConvoScreen::onStarting(){
	Buzz.setNoBuzzUID(convo);
	convoBox->clear();
}


void ConvoScreen::onStart(){
	Input::getInstance()->addListener(this);
	setButtonHoldTime(BTN_R, 500);

	Messages.markRead(convo);

	convoBox->load();
	convoBox->start();

#if MAKERPHONE_USE_T9_COMPOSER
	// T9 composer is "always on" while the screen is visible - there's
	// no separate focus/group dance like the legacy LVGL textarea needs.
	// We just track an `active` flag so the menu / picMenu / convoBox
	// transitions still gate input the same way they used to.
	t9Active = true;
#else
	textEntry->start();
#endif
}

void ConvoScreen::onStop(){
	Input::getInstance()->removeListener(this);
	Buzz.setNoBuzzUID(ESP.getEfuseMac());
	convoBox->stop();
#if MAKERPHONE_USE_T9_COMPOSER
	t9Active = false;
#else
	textEntry->stop();
#endif
}

void ConvoScreen::buttonPressed(uint i){
#if MAKERPHONE_USE_T9_COMPOSER
	// While a modal/sub-screen is open, the composer ignores all input
	// so the modal owns the keypad. ConvoBox's own selection mode also
	// suspends the composer (matches legacy textEntry behaviour).
	const bool modalOpen = picMenu->isActive() || menuMessage->isActive();
	const bool boxActive = convoBox->isActive();

	if(modalOpen) return;

	if(i == BTN_BACK){
		// Empty buffer + no in-flight pending letter -> classic "back out
		// of conversation". Otherwise BTN_BACK is a fast backspace, which
		// is what every Sony-Ericsson handset of the era did when there
		// was no dedicated CLR key.
		if(boxActive){
			convoBox->deselect();
			t9Active = true;
			return;
		}
		const String txt = phoneT9 ? phoneT9->getText() : String();
		const bool empty = (txt.length() == 0)
				&& (phoneT9 == nullptr || !phoneT9->hasPending());
		if(empty){
			pop();
		}else if(phoneT9){
			phoneT9->backspace();
		}
		return;
	}

	if(i == BTN_ENTER){
		// "Send" -> commit any pending letter so it lands in the buffer,
		// then dispatch through the existing sendMessage() path.
		if(boxActive){
			// While the convoBox is selecting messages, ENTER is the
			// per-message context menu trigger. Forward the click so
			// ConvoBox emits EV_CONVOBOX_MSG_SELECTED like before.
			lv_event_send(convoBox->getLvObj(), LV_EVENT_CLICKED, nullptr);
			return;
		}
		if(phoneT9) phoneT9->commitPending();
		sendMessage();
		return;
	}

	if(i == BTN_LEFT || i == BTN_RIGHT){
		// LR softkeys jump out of the composer into the convoBox so the
		// user can scroll/select messages - same flow the legacy path
		// used via EV_ENTRY_LR. Empty buffer required so a half-typed
		// reply isn't silently abandoned.
		if(boxActive) return;
		const String txt = phoneT9 ? phoneT9->getText() : String();
		const bool empty = (txt.length() == 0)
				&& (phoneT9 == nullptr || !phoneT9->hasPending());
		if(!empty) return;
		t9Active = false;
		lv_async_call([](void* user_data){
			auto cBox = static_cast<lv_obj_t*>(user_data);
			lv_event_send(cBox, LV_EVENT_CLICKED, nullptr);
		}, convoBox->getLvObj());
		return;
	}

	if(boxActive) return;

	// Digit + bumper presses go straight into the T9 state machine.
	// PhoneT9Input::keyPress maps '*' -> backspace and '#' -> case
	// toggle, so the bumpers behave exactly as on PhoneDialerScreen
	// (one consistent muscle memory across the phone shell).
	if(phoneT9 == nullptr) return;
	switch(i){
		case BTN_0: phoneT9->keyPress('0'); break;
		case BTN_1: phoneT9->keyPress('1'); break;
		case BTN_2: phoneT9->keyPress('2'); break;
		case BTN_3: phoneT9->keyPress('3'); break;
		case BTN_4: phoneT9->keyPress('4'); break;
		case BTN_5: phoneT9->keyPress('5'); break;
		case BTN_6: phoneT9->keyPress('6'); break;
		case BTN_7: phoneT9->keyPress('7'); break;
		case BTN_8: phoneT9->keyPress('8'); break;
		case BTN_9: phoneT9->keyPress('9'); break;
		case BTN_L: phoneT9->keyPress('*'); break; // backspace
		case BTN_R: phoneT9->keyPress('#'); break; // case toggle
		default: break;
	}
#else
	if(i == BTN_ENTER || i == BTN_LEFT || i == BTN_RIGHT) return;

	if(i != BTN_BACK){
		if(textEntry->isActive() || picMenu->isActive() || menuMessage->isActive()) return;


		if(convoBox->isActive()){
			convoBox->deselect();
		}

		textEntry->start();
		textEntry->keyPress(i);
		return;
	}

	if(textEntry->isActive() || convoBox->isActive() || picMenu->isActive() || menuMessage->isActive()) return;

	if(i == BTN_BACK){
		if(textEntry->isActive() && !textEntry->getText().empty()) return;
		pop();
		return;
	}
#endif
}

void ConvoScreen::buttonHeld(uint i){
	if(i != BTN_R) return;
	if(picMenu->isActive() || menuMessage->isActive()) return;

	convoBox->deselect();
#if MAKERPHONE_USE_T9_COMPOSER
	t9Active = false;
#else
	textEntry->stop();
#endif

	picMenu->start();
}

void ConvoScreen::buttonReleased(uint i){
#if MAKERPHONE_USE_T9_COMPOSER
	// Released-handler is intentionally empty for the T9 path - LR
	// short-press handling moved into buttonPressed() (where the empty-
	// buffer check happens), and the BTN_R hold-vs-tap distinction is
	// handled by the `buttonHeld` path above. Hold-time is configured
	// in onStart() so a long BTN_R opens picMenu while a tap still
	// reaches buttonPressed -> case toggle.
	(void) i;
#else
	if(i != BTN_LEFT && i != BTN_RIGHT) return;

	if(textEntry->isActive() || picMenu->isActive() || menuMessage->isActive() || convoBox->isActive()) return;

	lv_async_call([](void* user_data){
		auto cBox = static_cast<lv_obj_t*>(user_data);
		lv_event_send(cBox, LV_EVENT_CLICKED, nullptr);
	}, convoBox->getLvObj());
#endif
}

void ConvoScreen::sendMessage(){
#if MAKERPHONE_USE_T9_COMPOSER
	if(phoneT9 == nullptr) return;
	String s = phoneT9->getText();
	std::string text(s.c_str());
#else
	std::string text = textEntry->getText();
#endif
	if(text == "") return;

#if MAKERPHONE_USE_T9_COMPOSER
	phoneT9->clear();
#else
	textEntry->clear();
#endif

	Message message = Messages.sendText(convo, text);
	if(message.uid == 0) return;

	lv_timer_handler();

	convoBox->addMessage(message);
}

void ConvoScreen::textEntryConfirm(){
	sendMessage();
}

void ConvoScreen::textEntryCancel(){
#if MAKERPHONE_USE_T9_COMPOSER
	// T9 path never raises EV_ENTRY_CANCEL - this slot is kept for
	// signature compatibility (the legacy TextEntry path still calls
	// it via lv_obj_add_event_cb above). When the build flag is on we
	// simply route the user back via pop() if they invoke this through
	// some future path.
	if(phoneT9 == nullptr || phoneT9->getText().length() == 0){
		pop();
		return;
	}
#else
	textEntry->stop();

	if(textEntry->getText().empty()){
		pop();
		return;
	}
#endif
}

void ConvoScreen::textEntryLR(){
#if MAKERPHONE_USE_T9_COMPOSER
	// Same compat shim as textEntryCancel above. The new buttonPressed
	// handles BTN_LEFT/BTN_RIGHT directly so this is a no-op when the
	// flag is on.
#else
	std::string text = textEntry->getText();
	if(!text.empty()) return;
	textEntry->stop();
	lv_event_send(convoBox->getLvObj(), LV_EVENT_CLICKED, nullptr);
#endif
}

void ConvoScreen::convoBoxEnter(){
#if MAKERPHONE_USE_T9_COMPOSER
	t9Active = false;
#else
	textEntry->stop();
#endif
}

void ConvoScreen::convoBoxExit(){
#if MAKERPHONE_USE_T9_COMPOSER
	t9Active = true;
#else
	textEntry->start();
#endif
}

void ConvoScreen::messageSelected(const Message& msg){
	convoBox->deselect();
	selectedMessage = msg;

	std::vector<ContextMenu::Option> options;
	if(!msg.received && msg.outgoing){
		options.push_back({ "Resend message", 0 });
	}
	options.push_back({ "Delete message", 1 });

	menuMessage->setOptions(options);
	menuMessage->start();
}

void ConvoScreen::menuMessageSelected(){
	if(selectedMessage.uid == 0) return;

	const auto& option = menuMessage->getSelected();

	if(option.value == 0 && selectedMessage.outgoing && !selectedMessage.received){
		Messages.resend(convo, selectedMessage.uid);
	}else if(option.value == 1){
		if(Messages.deleteMessage(convo, selectedMessage.uid)){
			convoBox->removeMessage(selectedMessage.uid);
		}
	}

	selectedMessage = Message();
#if MAKERPHONE_USE_T9_COMPOSER
	t9Active = true;
#else
	textEntry->start();
#endif
}

void ConvoScreen::menuMessageCancel(){
	selectedMessage = Message();
#if MAKERPHONE_USE_T9_COMPOSER
	t9Active = true;
#else
	textEntry->start();
#endif
}

void ConvoScreen::picMenuSelected(){
	uint8_t index = picMenu->getSelected();

	Message msg = Messages.sendPic(convo, index);
	if(msg.uid == 0) return;

	lv_timer_handler();

	convoBox->addMessage(msg);

#if MAKERPHONE_USE_T9_COMPOSER
	t9Active = true;
#else
	textEntry->start();
#endif
}

void ConvoScreen::picMenuCancel(){
#if MAKERPHONE_USE_T9_COMPOSER
	t9Active = true;
#else
	textEntry->start();
#endif
}
