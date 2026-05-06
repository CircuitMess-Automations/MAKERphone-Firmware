#ifndef CHATTER2_FIRMWARE_LOCKSCREEN_H
#define CHATTER2_FIRMWARE_LOCKSCREEN_H

#include <Arduino.h>
#include <memory>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Elements/UnlockSlide.h"
#include "../Elements/PhoneClockFace.h"
#include "../Elements/PhoneLockHint.h"
#include "../Elements/PhoneNotificationPreview.h"
#include "../Elements/PhoneChargingOverlay.h"
#include "../Elements/PhoneChargeBars.h"
#include "../Elements/PhoneMissedCallFlash.h"
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
	// S155 - wide animated charge fill-bars that sit between the
	// charging chip and the slide-to-unlock hint while the device is
	// plugged in. Subscribes to chargingOverlay->isCharging() each
	// loop, so visibility tracks the same auto-detect heuristic the
	// chip already uses; nullptr until the constructor finishes
	// building chargingOverlay (the bars need it as their source).
	PhoneChargeBars*           chargeBars      = nullptr;
	// S158 - full-screen white-pulse overlay that fires once per
	// wake when MissedCallLog::consumePendingFlash() returns true.
	// Built lazily in the ctor (last child = top of z-order) and
	// hidden by default; onStarting() decides whether to start it.
	PhoneMissedCallFlash*      missedFlash     = nullptr;
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
	// S184 - read Settings.get().lockWidgetMode and apply it to the
	// LockScreen widget tree. ClockDate (0): the classic layout, both
	// date rows visible, no event label. ClockOnly (1): hide the date
	// rows, no event label. ClockEvent (2): hide the date rows and
	// paint the next armed PhoneAlarmService alarm in their place
	// ("NEXT ALARM 07:00", or "NO ALARMS SET" when none is enabled).
	// Safe to call repeatedly; idempotent across mode changes so a
	// settings-edit -> back-to-lock cycle picks up the new mode on
	// the next push without a screen rebuild.
	void applyLockWidgetMode();
	// Cached pointer to the clock face's lvgl object; we set its Y in
	// layoutForOwner() rather than reaching through PhoneClockFace's
	// public API for a setY (which the widget does not expose today).
	// nullptr until the constructor finishes building the clock.
	lv_obj_t* clockFaceObj = nullptr;
	// S184 - cached pointer to the PhoneClockFace widget so
	// applyLockWidgetMode() can flip its date rows on / off when the
	// user re-picks a different widget composition without rebuilding
	// the LockScreen. nullptr until the constructor finishes building
	// the face.
	PhoneClockFace* clockFace = nullptr;
	// S184 - "NEXT ALARM HH:MM" preview line painted in place of the
	// hidden weekday + month rows when the user picks the
	// PhoneLockWidgetScreen "CLOCK + EVT" mode. Mounted lazily by
	// applyLockWidgetMode() the first time the picker selects mode 2,
	// then kept around (just text-updated + show/hidden) for the rest
	// of the screen's lifetime so a later mode flip can re-use it.
	// Stays nullptr (and the layout stays in its pre-S184 form) when
	// the persisted mode is ClockOnly / ClockDate.
	lv_obj_t* nextEventLabel = nullptr;
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
