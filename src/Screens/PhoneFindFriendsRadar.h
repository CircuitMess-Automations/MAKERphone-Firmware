#ifndef MAKERPHONE_PHONEFINDFRIENDSRADAR_H
#define MAKERPHONE_PHONEFINDFRIENDSRADAR_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneFindFriendsRadar -- S174
 *
 * Phase-S Easter-egg toy: a tiny retro "radar" that sweeps over the
 * paired-friends list. Each contact in Storage.Friends becomes a
 * pixel "blip" placed at a deterministic bearing + range on a
 * circular radar plate; a thin cyan sweep-line rotates clockwise
 * around the centre at ~1 revolution every two seconds. As the
 * sweep passes a blip, the blip flashes bright cyan then fades
 * through orange to dim purple before the sweep comes back round to
 * relight it. A right-side banner keeps a running "RADAR LOCK" on
 * whichever blip is currently the brightest, showing that contact's
 * name plus a fake bearing/range read-out -- the kind of detail
 * customers screenshot and post about when they first stumble into it.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar  (10 px)
 *   |             FIND FRIENDS               | <- pixelbasic7 cyan
 *   |              RADAR LOCK                | <- pixelbasic7 dim
 *   |          .---------.                   |
 *   |        .'    |      `.    +--------+   |
 *   |       /      |        \   |ALEX KIM|   |
 *   |      /    ---+---      \  |BRG 045 |   | <- right banner
 *   |      \      |          /  |RNG 22M |   |
 *   |       \     |   * ----/.- |        |   | <- sweep + blips
 *   |        `.   |       .'    +--------+   |
 *   |          `---------'                   |
 *   |   PING                          BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - The sweep advances `SweepStepDeg` degrees every `TickPeriodMs`
 *    milliseconds. A short fade-tail is drawn behind the sweep at
 *    decreasing opacity so the rotation reads as a continuous
 *    clockwise arc instead of a strobing line.
 *  - Each blip has its own deterministic bearing (0..359 deg) + range
 *    (some fraction of the radar radius). The values come from a
 *    cheap mix of the friend's UID -- the same UID always lands in
 *    the same spot, so a user can learn "Mom is in the upper-left
 *    quadrant" the way they would learn a real radar fixture.
 *  - Brightness for each blip = 100 - ((sweepDeg - blipBearingDeg) mod
 *    360), clamped to [0..100]. The blip therefore peaks the instant
 *    the sweep crosses its bearing, then fades smoothly through
 *    cream/orange to dim purple before the sweep returns. The
 *    brightest blip in any frame is the "current lock"; the right
 *    banner re-binds to its name + bearing + range.
 *  - Pressing PING (left softkey / BTN_ENTER / BTN_5) instantly
 *    re-aligns the sweep to the locked blip and fires a short
 *    buzzer chirp via `BuzzerService`. Useful for "show me where my
 *    friend is" demos at a CircuitMess booth.
 *  - BTN_0 toggles the sweep on/off without leaving the screen
 *    (useful for screenshots).
 *
 * Population:
 *  - The constructor walks Storage.Friends.all() (skipping the
 *    device's own efuse-mac id, the same convention FriendsScreen +
 *    PhoneContactsScreen use). Display name + avatar seed come from
 *    `PhoneContacts::displayNameOf` / `::avatarSeedOf` so user-edited
 *    overrides flow in transparently.
 *  - When no friends exist yet, the screen falls back to a small
 *    sample roster so the radar reads as populated on first boot
 *    before the user has paired any peer (mirrors the
 *    PhoneContactsScreen sample-fallback).
 *  - The roster is hard-capped at MaxBlips (16) entries -- the
 *    radar plate would otherwise be a noisy speckle of overlapping
 *    blips. Any further friends are silently dropped.
 *
 * Implementation notes:
 *  - 100% code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually
 *    part of the same MAKERphone family.
 *  - Sweep + fade-tail are three `lv_line` widgets re-pointed each
 *    tick. Three lines is enough trail to read as motion blur and
 *    light enough to keep the per-frame cost flat.
 *  - Each blip is a 4x4 rounded-rect `lv_obj` whose `bg_color` is
 *    rebound by a small palette interpolation in renderBlips(). No
 *    canvas/lottie/font work happens in the tick path.
 *  - Bearing maths uses `lv_trigo_sin` so the screen never pulls in
 *    the floating-point library just for sin/cos.
 */
class PhoneFindFriendsRadar : public LVScreen, private InputListener {
public:
	PhoneFindFriendsRadar();
	virtual ~PhoneFindFriendsRadar() override;

	void onStart() override;
	void onStop() override;

	/** Hard upper bound on rendered blips (UI density limit). */
	static constexpr uint8_t  MaxBlips      = 16;

	/** Maximum length of a blip name as displayed in the banner. */
	static constexpr uint8_t  MaxNameLen    = 14;

	/** Sweep cadence -- ~50 ms per step + 6 deg per step => 1 rev / 3 s. */
	static constexpr uint16_t TickPeriodMs  = 50;
	static constexpr uint16_t SweepStepDeg  = 6;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs    = 600;

	/** Radar geometry: centre + radius, plate sits in the left 2/3 of
	 *  the 160x128 display so the right-side banner has room. */
	static constexpr lv_coord_t RadarCx     = 50;
	static constexpr lv_coord_t RadarCy     = 70;
	static constexpr lv_coord_t RadarR      = 36;

	/** Number of blips currently seeded onto the radar. */
	uint8_t blipCount() const { return blipsCount; }

	/** Current sweep bearing in degrees, 0 = up, clockwise. */
	uint16_t sweepBearing() const { return sweepDeg; }

	/** Index of the currently-locked blip (i.e. the brightest), or
	 *  -1 when no blip is brighter than the LockBrightnessFloor. */
	int8_t lockedBlipIdx() const { return lockedIdx; }

	/** True iff the sweep timer is currently animating. BTN_0
	 *  toggles this; the radar still renders the blip palette
	 *  correctly when paused. */
	bool sweeping() const { return sweepActive; }

	/** Pure-function: fold a UID into a deterministic radar bearing
	 *  (0..359 deg). Factored out so tests can introspect placement. */
	static uint16_t bearingForSeed(uint32_t seed);

	/** Pure-function: fold a UID into a deterministic blip range
	 *  (0..maxR pixels). Same goal as bearingForSeed. */
	static uint8_t  rangeForSeed(uint32_t seed, uint8_t maxR);

	/** Pure-function: brightness of a blip whose bearing is at
	 *  `blipDeg` when the sweep sits at `sweepDeg`. Returns 0..100,
	 *  100 = sweep just passed, 0 = sweep is far away. */
	static uint8_t  brightnessFor(uint16_t sweepDeg, uint16_t blipDeg);

	/** Below this brightness the right banner shows the idle
	 *  "SCANNING..." copy instead of locking onto a blip. */
	static constexpr uint8_t LockBrightnessFloor = 30;

private:
	struct Blip {
		// Clipped-and-uppercased copy of the friend's display name.
		// MaxNameLen + 1 to leave room for the terminator.
		char     name[MaxNameLen + 1];
		uint16_t bearingDeg;   // 0..359
		uint8_t  rangePx;      // 0..RadarR
		uint8_t  brightness;   // 0..100, last computed value
		lv_obj_t* dot;         // 4x4 rounded-rect on the screen
	};

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;
	lv_obj_t* subtitleLabel;
	lv_obj_t* hintLabel;

	// Static radar plate: outer/mid/inner rings + cross-hairs.
	lv_obj_t* radarFrame;
	lv_obj_t* radarMidRing;
	lv_obj_t* radarInnerRing;
	lv_obj_t* crossH;
	lv_obj_t* crossV;
	lv_obj_t* radarCentreDot;

	// Sweep + two trailing fade lines for the motion-blur effect.
	lv_obj_t*  sweepLine;
	lv_obj_t*  sweepFade1;
	lv_obj_t*  sweepFade2;
	lv_point_t sweepPoints[2];
	lv_point_t fade1Points[2];
	lv_point_t fade2Points[2];

	// Right banner (locked-blip read-out).
	lv_obj_t* bannerBox;
	lv_obj_t* bannerNameLabel;
	lv_obj_t* bannerBearingLabel;
	lv_obj_t* bannerRangeLabel;

	Blip    blips[MaxBlips];
	uint8_t blipsCount = 0;

	uint16_t sweepDeg    = 0;     // current bearing
	bool     sweepActive = true;  // BTN_0 toggles
	int8_t   lockedIdx   = -1;
	bool     backLongFired = false;

	lv_timer_t* tickTimer = nullptr;

	void buildHud();
	void buildRadarPlate();
	void buildSweep();
	void buildBanner();
	void buildBlipDots();

	void seedFromStorageOrSamples();

	/** Append a blip. Truncates the name to MaxNameLen. Caller-supplied
	 *  strings are copied so the caller doesn't have to keep them
	 *  alive. Returns the new blip count. */
	uint8_t addBlip(const char* name, uint32_t seed);

	void renderSweep();
	void renderBlips();
	void renderBanner();

	/** Snap the sweep bearing onto the locked blip's bearing and
	 *  fire a short buzzer chirp. No-op when there's no lock. */
	void pingLockedBlip();

	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);
	void onTick();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEFINDFRIENDSRADAR_H
