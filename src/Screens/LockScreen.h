#ifndef CHATTER2_FIRMWARE_LOCKSCREEN_H
#define CHATTER2_FIRMWARE_LOCKSCREEN_H

#include <Arduino.h>
#include <memory>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Elements/UnlockSlide.h"
#include "../Elements/PhoneLockHint.h"
#include "../Elements/PhoneNotificationPreview.h"
#include "../Elements/PhoneChargingOverlay.h"
#include "../Services/MessageService.h"
#include "../Elements/UserWithMessage.h"

class LockScreen : public LVScreen, private InputListener, private UnreadListener {
public:
	LockScreen();
	virtual ~LockScreen();

	void onStarting() override;
	void onStart() override;
	void onStop() override;

	static void activate(LVScreen* parent);

private:
	static LockScreen* instance;


	lv_obj_t* container;
	lv_obj_t* noUnreads = nullptr;

	UnlockSlide*               slide;
	PhoneLockHint*             lockHint = nullptr;
	PhoneNotificationPreview*  preview  = nullptr;
	PhoneChargingOverlay*      chargingOverlay = nullptr;
	// S144 - small "OWNER" greeting label tucked between the status
	// bar and the clock face. Mounted lazily by ensureOwnerLabel() the
	// first time onStarting() sees a non-empty Settings.ownerName, then
	// kept around (just text-updated) for the rest of the screen's
	// lifetime so a later edit -> back-to-lock cycle picks the new
	// value up without leaking widgets. Stays nullptr (and the layout
	// stays in its pre-S144 form with the clock anchored under the
	// status bar) when the user has never set an owner name.
	lv_obj_t*                  ownerLabel = nullptr;
	// Captures the on-screen height of the owner label + its top/bottom
	// gutter (0 when the label is hidden) so layoutForOwner() can shift
	// the clock face / preview / unread container by exactly the right
	// amount without re-deriving constants in two places.
	lv_coord_t                 ownerStripH = 0;

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;

	void onUnread(bool unread) override;

	std::vector<UserWithMessage*> unreads;

	void loadUnread();
	void createNoUnreads();
	void clearUnreads();

	// S144 - mount or refresh the small owner-name greeting based on
	// the current Settings.ownerName value. Safe to call repeatedly;
	// hides the label (set_text empty + add_flag HIDDEN) when the
	// persisted value is blank so the layout falls back to its
	// pre-S144 form.
	void refreshOwnerLabel();
	// S144 - re-anchor the clock face / preview / unread container
	// based on whether the owner-name label is currently visible. Run
	// once from the constructor and from refreshOwnerLabel() so a
	// settings edit -> pop-back-to-lock cycle picks up the new layout
	// without rebuilding the whole screen.
	void layoutForOwner();
	// Cached pointer to the clock face's lvgl object; we set its Y in
	// layoutForOwner() rather than reaching through PhoneClockFace's
	// public API for a setY (which the widget does not expose today).
	// nullptr until the constructor finishes building the clock.
	lv_obj_t* clockFaceObj = nullptr;
	// Mirror of the clock face's natural top edge (y=11 today). Cached
	// so layoutForOwner() can return to it when the owner label hides.
	static constexpr lv_coord_t kClockBaseY = 11;
	// Height of the owner-name strip when it is shown. Sized to fit a
	// single pixelbasic7 line plus 1 px of breathing room above and
	// below so the cyan greeting does not touch the status bar's
	// bottom edge or the clock face's top digits.
	static constexpr lv_coord_t kOwnerStripH = 9;


};


#endif //CHATTER2_FIRMWARE_LOCKSCREEN_H
