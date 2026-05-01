#ifndef MAKERPHONE_PHONEBATTERYLOWMODAL_H
#define MAKERPHONE_PHONEBATTERYLOWMODAL_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVModal.h"

class PhoneBatteryIcon;

/**
 * S58 — PhoneBatteryLowModal
 *
 * MAKERphone 2.0 retro feature-phone "battery low" modal. Replaces the
 * stock `BatteryNotification::WARNING` overlay with a code-only,
 * palette-coherent slab that matches the rest of the Phone* widgets,
 * and chirps the piezo via the existing PhoneRingtoneEngine so the
 * user gets the same audio cue early-2000s phones used to nag you
 * when the battery dipped below the comfort line.
 *
 *      +----------------------------------------+
 *      |                                        |
 *      |          BATTERY LOW                   | <- pixelbasic7 sunset orange caption
 *      |       +-----------------+              |
 *      |       |  [#]    12%     |              | <- PhoneBatteryIcon + pixelbasic16 percent
 *      |       +-----------------+              |
 *      |          Charge soon                   | <- pixelbasic7 dim hint
 *      |                                        |
 *      +----------------------------------------+
 *
 *      ^ MP_BG_DARK fill, MP_ACCENT 1px border, 4 px corner radius
 *
 * Behaviour:
 *  - Constructor takes the parent screen and (optionally) the percent
 *    value to display. The default percent is `Battery.getPercentage()`
 *    captured at construction so the modal always shows what the user
 *    actually has at the moment they get nagged.
 *  - Auto-dismisses after `AutoDismissMs` (default 4000 ms). Any key
 *    press short-circuits the auto-dismiss and stops the chirp
 *    immediately (same "press anything to skip" pattern as
 *    `PhoneCallEnded` and `PhoneBootSplash`).
 *  - Plays the static `ChirpMelody` via the global Ringtone engine on
 *    `onStart()` if the user has sound enabled. The chirp is a short,
 *    non-looping 3-note descending arpeggio so it reads as "warning"
 *    rather than "ringing". On `onStop()` we explicitly silence the
 *    engine so a long chirp doesn't outlast a quick dismiss.
 *  - Owns a `PhoneBatteryIcon` pinned at level 0 with the low-battery
 *    pulse running. The pulse and the chirp share the same cadence so
 *    the visual + audio cues sync up.
 *
 * Implementation notes:
 *  - 100 % code-only. No SPIFFS assets, no canvas. Same family as
 *    every other Phone* widget. The slab is a single rounded `lv_obj`
 *    with a 1 px MP_ACCENT border; everything else is plain labels +
 *    one `PhoneBatteryIcon` instance.
 *  - 130 x 64 footprint, centred on the 160 x 128 display by LVModal.
 *    Sized to leave a 15 px gutter on either side so the modal reads
 *    as a "card" rather than a full-screen takeover - the user can
 *    still see the screen behind it through the gutters and feel that
 *    the device is fine, just nagging.
 *  - Keys are caught via a single `LV_EVENT_KEY` / `LV_EVENT_PRESSED`
 *    callback bound to the obj inside the modal's input group,
 *    mirroring the dismissal pattern used by the legacy
 *    `BatteryNotification`. LVModal already swaps the indev group for
 *    us, so the focus-event path is the cheapest way to catch a press
 *    without fighting the modal stack.
 *  - The chirp ringtone is defined as a static `Note[]` + `Melody`
 *    inside the .cpp file so the compiler can place it in flash. No
 *    runtime allocation, no dependency on PhoneRingtoneLibrary.
 *  - The modal `delete`s itself shortly after the dismiss path runs
 *    (one-shot self-destroy timer), so the caller can `new ...; ->start();`
 *    and forget about lifetime — same pattern the legacy
 *    `BatteryNotification` uses (the legacy modal leaks; we don't).
 */
class PhoneBatteryLowModal : public LVModal {
public:
	/**
	 * Build the modal inside `parent`. The caller is responsible for
	 * calling `start()` (the modal is a `new`-and-start-and-forget
	 * pattern - the modal subscribes itself to a one-shot dismiss
	 * timer that deletes the heap allocation when the dismiss path
	 * runs).
	 *
	 * @param parent    Screen the modal is layered on top of.
	 * @param percent   Percentage value shown in the centre. Pass
	 *                  0xff (the default) to read the live value from
	 *                  `Battery.getPercentage()` at construction.
	 */
	PhoneBatteryLowModal(LVScreen* parent, uint8_t percent = 0xff);
	virtual ~PhoneBatteryLowModal() override;

	/** Override the percent text without rebuilding. */
	void setPercent(uint8_t percent);

	/**
	 * Override the auto-dismiss delay (ms). 0 disables the timer
	 * entirely (any-key dismiss only).
	 */
	void setAutoDismissMs(uint32_t ms);

	uint8_t  getPercent()       const { return percent; }
	uint32_t getAutoDismissMs() const { return autoDismissMs; }

	// 130 x 64 modal slab, 4 px corner radius, 1 px MP_ACCENT border.
	static constexpr uint16_t ModalWidth      = 130;
	static constexpr uint16_t ModalHeight     = 64;
	// Auto-dismiss after 4 s - long enough to read the percent + chirp,
	// short enough not to stall the user.
	static constexpr uint32_t DefaultDismissMs = 4000;

protected:
	void onStart() override;
	void onStop() override;

private:
	lv_obj_t*   captionLabel = nullptr;
	lv_obj_t*   percentLabel = nullptr;
	lv_obj_t*   hintLabel    = nullptr;
	PhoneBatteryIcon* batteryIcon = nullptr;

	lv_timer_t* dismissTimer     = nullptr;
	lv_timer_t* selfDestroyTimer = nullptr;

	uint8_t  percent       = 0;
	uint32_t autoDismissMs = DefaultDismissMs;
	bool     dismissed     = false;

	void buildSlab();
	void buildContent();
	void refreshPercentLabel();

	void startDismissTimer();
	void stopDismissTimer();
	void fireDismiss();
	void scheduleSelfDestroy();

	static void onDismissTimer(lv_timer_t* timer);
	static void onSelfDestroyTimer(lv_timer_t* timer);
	static void onKeyEvent(lv_event_t* event);
};

#endif // MAKERPHONE_PHONEBATTERYLOWMODAL_H
