#ifndef MAKERPHONE_PHONEVIRTUALPET_H
#define MAKERPHONE_PHONEVIRTUALPET_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneVirtualPet — S129
 *
 * Phase-P utility screen: a Tamagotchi-style virtual pet viewer/
 * controller. The actual hunger/happy/energy/age state lives in
 * PhoneVirtualPet (services), which keeps ticking even when the
 * user is on a different screen. This screen is purely a viewer
 * over that state plus a four-key controller (Feed / Play / Sleep
 * / Reset) — same separation PhoneAlarmClock + PhoneAlarmService
 * use, so a user opening the pet screen never sees a "stale" pet.
 *
 * Layout:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |               YOUR PET                 | <- pixelbasic7 cyan
 *   |                                        |
 *   |              ( ^ _ ^ )                 | <- pixelbasic16 face
 *   |                                        |
 *   |   HUN  [###########       ]            | <- 3 stat bars
 *   |   HAP  [############      ]
 *   |   ENG  [#########         ]
 *   |   AGE  3d 04h 12m                      | <- pixelbasic7 caption
 *   |   FEED                         BACK    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Pet face is a code-only pixelbasic16 string ("emoticon") that
 * changes with the live mood:
 *
 *   Happy  : ( ^_^ )
 *   Hungry : ( T_T )
 *   Sad    : ( ;_; )
 *   Tired  : ( -_- )
 *   Asleep : ( zZz )
 *
 * No SPIFFS assets are added — the whole sprite-look comes from the
 * existing pixelbasic16 font, which keeps the data partition the
 * same size it was before this session.
 *
 * Controls:
 *   - BTN_1 / BTN_L / BTN_ENTER ("FEED")   : Pet.feed()
 *   - BTN_2                                : Pet.play()
 *   - BTN_3                                : Pet.wakeOrSleep()
 *   - BTN_4                                : Pet.reset() (long-press
 *                                            to confirm — short-press
 *                                            shows a hint instead so
 *                                            the user cannot wipe the
 *                                            pet by mistake)
 *   - BTN_BACK / BTN_R / right softkey     : pop the screen.
 *
 * The cursor / selection model is intentionally absent — every
 * action maps directly to a digit key, exactly the way the original
 * 1996 toy worked (three physical buttons, no submenu).
 *
 * Refresh cadence: a 1 Hz lv_timer rebuilds the face label, the
 * three bars and the age caption. Cheap, mirror image of the
 * PhoneWorldClock tick.
 */
class PhoneVirtualPet : public LVScreen, private InputListener {
public:
	PhoneVirtualPet();
	virtual ~PhoneVirtualPet() override;

	void onStart() override;
	void onStop() override;

	/** Tick cadence (ms) for the live-stat refresh timer. 1 s is
	 *  enough — the underlying service only changes once per minute
	 *  on the background tick, but a 1 s redraw keeps the face in
	 *  sync with a user-driven feed/play action without measurable
	 *  cost. */
	static constexpr uint16_t TickPeriodMs    = 1000;

	/** Long-press threshold for BTN_BACK. */
	static constexpr uint16_t BackHoldMs      = 600;

	/** Long-press threshold for BTN_4 (reset confirm). */
	static constexpr uint16_t ResetHoldMs     = 800;

	/** How long the "long-press 4 to wipe" hint stays on screen
	 *  after a short-press of BTN_4. */
	static constexpr uint16_t ResetHintMs     = 1500;

	/** Public for tests / future skins; the bar geometry sits at the
	 *  top of the .cpp but the screen height is exposed here so the
	 *  layout stays self-documenting. */
	static constexpr lv_coord_t StatBarW      = 90;
	static constexpr lv_coord_t StatBarH      = 6;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Static caption ("YOUR PET").
	lv_obj_t* captionLabel;

	// The pet face sprite (pixelbasic16 emoticon).
	lv_obj_t* faceLabel;

	// Three stat rows: a tag label + a fill bar.
	lv_obj_t* statTagHunger;
	lv_obj_t* statBarHunger;
	lv_obj_t* statFillHunger;
	lv_obj_t* statTagHappy;
	lv_obj_t* statBarHappy;
	lv_obj_t* statFillHappy;
	lv_obj_t* statTagEnergy;
	lv_obj_t* statBarEnergy;
	lv_obj_t* statFillEnergy;

	// Age + transient hint caption.
	lv_obj_t* ageLabel;
	lv_obj_t* hintLabel;

	bool      backLongFired = false;
	bool      resetLongFired = false;

	uint32_t  hintExpireAtMs = 0;

	lv_timer_t* tickTimer = nullptr;

	void buildHud();
	void refreshAll();

	/** Render the mood-driven face emoticon. */
	void refreshFace();

	/** Update the three stat bars from the live service state. */
	void refreshStats();

	/** Update the age caption + clear an expired hint if any. */
	void refreshAgeAndHint();

	void refreshSoftKeys();

	/** Pop the on-screen hint for the requested duration (ms). */
	void pushHint(const char* text, uint32_t durMs);

	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEVIRTUALPET_H
