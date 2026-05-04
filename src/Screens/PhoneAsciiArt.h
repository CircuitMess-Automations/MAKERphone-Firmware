#ifndef MAKERPHONE_PHONEASCIIART_H
#define MAKERPHONE_PHONEASCIIART_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneAsciiArt -- S170
 *
 * Phase-S Easter-egg / nostalgia toy: an ASCII-art slideshow viewer
 * that cycles through eight hand-pixeled drawings, one at a time.
 * Slots in beside PhoneFortuneCookie (S133), PhoneCoinFlip (S132),
 * PhoneMagic8Ball (S130), PhoneDiceRoller (S131), PhoneFlashlight
 * (S134) and the rest of the toy apps so the user navigating
 * between them feels at home.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |             ASCII GALLERY              | <- pixelbasic7 cyan
 *   |                  CAT                   | <- pixelbasic7 cream
 *   |  +----------------------------------+  |
 *   |  |          /\___/\                 |  |
 *   |  |         ( o   o )                |  |
 *   |  |          (  ^  )                 |  | <- art panel
 *   |  |           \___/                  |  |
 *   |  +----------------------------------+  |
 *   |                 1 / 8                  | <- pixelbasic7 dim
 *   |   NEXT                          BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Visual: a centred, single-pixel-bordered "frame" panel holds the
 * art lines. The whole drawing is rendered into one lv_label with
 * the pixelbasic7 font and LV_TEXT_ALIGN_CENTER, so each line
 * self-centres independently -- handy when the underlying font is
 * not strictly monospace. The drawings are bilaterally symmetric,
 * keeping any per-glyph-width drift visually negligible.
 *
 * Controls:
 *   - BTN_RIGHT / BTN_R / BTN_ENTER / BTN_5 / BTN_L / left softkey
 *       ("NEXT") : advance one slide. Wraps from 8 -> 1.
 *   - BTN_LEFT  : go back one slide. Wraps from 1 -> 8.
 *   - BTN_0     : jump back to slide 1.
 *   - BTN_BACK / right softkey (short or long) : pop the screen.
 *
 * The viewer is a fully self-contained widget -- no SPIFFS assets,
 * no extra services, all art lives as `static const char* const`
 * tables in the .cpp. The 8 art entries are original compositions
 * for MAKERphone -- not copied from existing ASCII-art collections
 * -- so the table can be reordered, extended, or trimmed without
 * copyright concerns.
 *
 * Implementation notes:
 *   - 100% code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the toy slots into the
 *     family without a visual seam.
 *   - The 8 drawings live as a `static constexpr` C-string table
 *     in the .cpp, with paired titles in a parallel array.
 *   - The art panel is a single rounded `lv_obj` with a thin cyan
 *     border, painted once at construction and never resized.
 *   - Render is a no-op on idle frames (we only repaint the label,
 *     title, and indicator on a slide change), so the screen has
 *     zero per-frame cost when the user is just looking at a slide.
 *
 * S168 hook: the shake gesture (L+R held together) advances the
 * slide forward -- the same way pressing NEXT would -- so the toy
 * matches the rest of the shake-aware Phone* family without adding
 * a new randomization mode.
 */
class PhoneAsciiArt : public LVScreen, private InputListener {
public:
	PhoneAsciiArt();
	virtual ~PhoneAsciiArt() override;

	void onStart() override;
	void onStop() override;

	// S168 -- shake gesture forwards to advance().
	void onShake() override;

	/** Total number of drawings in the rotation. */
	static constexpr uint8_t SlideCount = 8;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs = 600;

	/** Look up the title of a slide by index. Returns nullptr if out
	 *  of range. Useful for tests + future "menu" composites. */
	static const char* titleAt(uint8_t idx);

	/** Look up the art body of a slide by index. Returns nullptr if
	 *  out of range. The string already contains '\n' line breaks. */
	static const char* artAt(uint8_t idx);

	/** Index of the slide currently displayed. */
	uint8_t currentIndex() const { return currentIdx; }

	/** Advance to the next slide, wrapping at the end. */
	void advance();

	/** Step back one slide, wrapping at the start. */
	void retreat();

	/** Jump to a specific slide index (clamped + wrapped). */
	void jumpTo(uint8_t idx);

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Header strip: static caption + per-slide title.
	lv_obj_t* captionLabel;
	lv_obj_t* titleLabel;

	// Framed art panel + art-body label.
	lv_obj_t* artPanel;
	lv_obj_t* artLabel;

	// Bottom indicator ("1 / 8") above the soft-key bar.
	lv_obj_t* indicatorLabel;

	uint8_t currentIdx = 0;
	bool    backLongFired = false;

	void buildHud();

	/** Repaint title + art + indicator for the current slide. */
	void renderCurrent();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEASCIIART_H
