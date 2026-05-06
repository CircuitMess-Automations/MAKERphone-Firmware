#ifndef MAKERPHONE_PHONEACCENTSCREEN_H
#define MAKERPHONE_PHONEACCENTSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneAccentScreen
 *
 * Phase-T sub-screen (S187): the custom RGB accent picker reachable
 * from the DISPLAY section of PhoneSettingsScreen ("Accent" row,
 * directly below "Theme"). Lets the user dial in any 24-bit colour
 * for the global MakerphoneTheme::accent() role and watch a small
 * preview slab repaint in real time as the R / G / B sliders move,
 * the same way PhoneBrightnessScreen drives the LCD live under the
 * user's finger but never persists until SAVE.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |               ACCENT                   | <- pixelbasic7 cyan caption
 *   |   R [###############     ] 255         | <- channel 0 (red)
 *   |   G [###########         ] 140         | <- channel 1 (green)
 *   |   B [###                 ]  30         | <- channel 2 (blue)
 *   |                                        |
 *   |     +------------------------+         | <- preview slab
 *   |     | TILE  CHATBUBBLE  RULE |         |    repaints live in
 *   |     +------------------------+         |    the chosen colour
 *   |                                        |
 *   |              [ ON ]                    | <- override toggle
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Three R / G / B channel sliders sit stacked above the live preview
 * slab. The focused channel is highlighted in cyan and swallows the
 * LEFT / RIGHT (and 4 / 6) keys to step its value by 16 (one of 16
 * stops, so the bar walks from 0 to 240 in 16-unit increments and
 * snaps to 255 at the top so the canonical sunset orange / pure white
 * land cleanly on a stop). 2 / 8 step the focused channel as a
 * mirror; UP / DOWN are not present on the Chatter hardware so we
 * rely on BTN_UP / BTN_DOWN being aliased to LEFT / RIGHT in
 * Pins.hpp, leaving the explicit channel-focus shift to the BTN_L /
 * BTN_R bumpers + the BTN_5 ("toggle override") affordance.
 *
 * Behaviour:
 *  - BTN_L / BTN_R cycle which channel is focused (R -> G -> B ->
 *    R...). The focused channel's bar gets a cyan border to read as
 *    "the one your sliders are aimed at".
 *  - LEFT / 4 step the focused channel down by StepSize; RIGHT / 6
 *    step it up. 2 / 8 mirror LEFT / RIGHT for users who navigate
 *    with the numpad. No wrap-around -- clamping to [0, 255] is the
 *    right feature-phone affordance for an RGB slider.
 *  - BTN_5 toggles the customAccentEnabled override on/off live so
 *    the user can A/B against the per-theme accent without saving.
 *  - ENTER (SAVE softkey) writes Settings.customAccentEnabled / R /
 *    G / B from the live preview state, calls Settings.store(), and
 *    pop()s. The next screen rebuild picks up the new accent via
 *    MakerphoneTheme::accent().
 *  - BACK reverts every preview-only edit (the override flag + the
 *    three channels) back to the values the screen opened with and
 *    pop()s -- the standard Sony-Ericsson "discard changes"
 *    affordance, byte-identical to the BACK contract on
 *    PhoneBrightnessScreen and PhoneThemeScreen.
 *
 * Implementation notes:
 *  - Code-only -- zero SPIFFS asset growth. Reuses PhoneSynthwaveBg
 *    / PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually
 *    part of the rest of the MAKERphone family.
 *  - The "real-time preview" is not a retro-fit pass on every
 *    Phone* widget. It is a small composition slab living on this
 *    screen alone -- a PhoneIconTile-style 28x14 swatch, a chat-
 *    bubble pill, and an underline rule -- repainted on every
 *    channel step using the chosen RGB so the user can see what the
 *    accent will *look* like across the three roles it dominates.
 *    Once the user SAVEs, every Phone* widget that already calls
 *    MakerphoneTheme::accent() (PhoneIconTile halo, PhoneSoftKeyBar
 *    arrow tint, PhoneChatBubble Sent fill, ...) repaints with the
 *    new colour on the next screen push -- no per-widget plumbing
 *    needed beyond the central resolver swap done in
 *    MakerphoneTheme.cpp.
 *  - The slider geometry mirrors PhoneBrightnessScreen's segment
 *    bar: 16 cells per channel, sunset-orange / chosen-RGB filled
 *    cells, muted-purple empty cells, a thin dim-purple frame. Cells
 *    are plain LVGL rects (cheap, no text shaping) so a per-step
 *    repaint touches at most 16 styles per channel.
 */
class PhoneAccentScreen : public LVScreen, private InputListener {
public:
	PhoneAccentScreen();
	virtual ~PhoneAccentScreen() override;

	void onStart() override;
	void onStop() override;

	/** Three RGB channels (in painter order, top -> bottom). */
	enum class Channel : uint8_t { R = 0, G = 1, B = 2 };

	/** Number of cells per channel slider. 16 cells * 16 = 256. */
	static constexpr uint8_t StopCount = 16;
	/** Step size in raw 0..255 space (256/16 = 16). */
	static constexpr uint8_t StepSize  = 16;

	/** Currently focused channel (public for tests / introspection). */
	Channel getFocusedChannel() const { return focused; }

	/** Live preview value for a given channel (0..255). */
	uint8_t getChannelValue(Channel c) const;

	/** Live preview override flag (0 = off, 1 = on). */
	uint8_t getOverrideEnabled() const { return overrideEnabled; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;     // "ACCENT"
	// Three channels stacked vertically. Each channel has a label
	// ("R" / "G" / "B"), a frame, 16 cells inside, and a numeric
	// readout ("255") on the right. We keep parallel arrays rather
	// than a struct so the per-channel paint loop stays cheap.
	lv_obj_t* chLabels[3];      // "R" / "G" / "B"
	lv_obj_t* chFrames[3];      // 16-cell frames
	lv_obj_t* chCells[3][16];   // cells inside each frame
	lv_obj_t* chValues[3];      // numeric readout per channel
	lv_obj_t* previewSlab;      // outer rounded rect (chosen-RGB fill)
	lv_obj_t* previewLabel;     // "PREVIEW" caption inside slab
	lv_obj_t* toggleLabel;      // "[ ON ]" / "[ OFF ]" override pill
	lv_obj_t* hintLabel;        // "L/R chan, 5 toggle"

	// Live preview state -- never persisted unless SAVE fires.
	uint8_t channels[3];        // R, G, B (0..255)
	uint8_t overrideEnabled;    // 0 / 1 toggle preview

	// Snapshot of the values the screen opened with -- used by BACK
	// to revert the preview without committing.
	uint8_t initialChannels[3];
	uint8_t initialOverride;

	Channel focused;            // R / G / B

	void buildCaption();
	void buildSliders();
	void buildPreview();
	void buildToggle();
	void buildHint();

	/** Repaint the focused-channel highlight + all three sliders. */
	void refreshSliders();
	/** Repaint the preview slab using the live chosen RGB. */
	void refreshPreview();
	/** Repaint the override pill ("[ ON ]" / "[ OFF ]"). */
	void refreshToggle();
	/** Refresh the L/R softkey captions based on dirty/pristine state. */
	void refreshSoftKeys();

	/** Step the focused channel by delta (clamps to [0, 255]). */
	void stepFocusedBy(int16_t delta);
	/** Cycle focused channel (R -> G -> B -> R -> ...). */
	void cycleChannel(int8_t delta);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEACCENTSCREEN_H
