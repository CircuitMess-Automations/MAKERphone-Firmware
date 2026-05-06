#ifndef MAKERPHONE_PHONEKARAOKESCREEN_H
#define MAKERPHONE_PHONEKARAOKESCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneKaraokeScreen - S196
 *
 * Karaoke title-display mode for whatever melody the global S39
 * Ringtone engine is currently driving. The screen pulls the active
 * track's name from `Ringtone.currentName()` and animates a
 * left-to-right "lit / unlit" highlight across the title characters
 * so the user can read along with the melody as if it were a song.
 * The fill ratio = `Ringtone.currentStep() / Ringtone.totalSteps()`,
 * which advances one notch per note the engine plays - i.e. each
 * "note" feels like a syllable lighting up.
 *
 * Slots in alongside PhoneMusicPlayer (S42) and PhoneRadio (S195) as
 * the third Phase-U music view: where PhoneMusicPlayer is the
 * navigation surface and PhoneRadio is the dial, PhoneKaraokeScreen
 * is the "lyrics overlay" the user pops up while a track is playing
 * to get the visual rhythm-cue feature phones used to ship as a
 * marketing checkbox.
 *
 * Launched from PhoneMusicPlayer via BTN_3 ("3:KARAOKE" hint), so
 * the feature is one short keypress from the place a melody is most
 * obviously already playing. The screen never starts, stops or
 * changes the melody itself - it is a pure observer of the engine.
 *
 * Layout (160x128):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |              NOW PLAYING               | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |               NEON DRIVE               | <- pixelbasic16 title
 *   |               ^^^^^^^^                 |    cyan-fill clipped to
 *   |                                        |    progress; rest dim
 *   |    [###############----------]         | <- progress bar
 *   |          STEP 12 / 31                  | <- step/total caption
 *   |       <  3 4 5 6 7 8 9  >              | <- bouncing-notes strip
 *   |                                  BACK  | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Visual technique:
 *   The title is rendered TWICE on top of each other in pixelbasic16
 *   - once dim purple (the "unlit" baseline), once sunset orange
 *   inside a clip-container whose width is set to
 *   (titleWidthPx * progress). Because LVGL clips children to the
 *   parent's content area, the cyan layer only paints the left
 *   portion. Updating one width per tick is a single
 *   lv_obj_set_width() call - cheap, no recolor parsing, no
 *   per-character math.
 *
 *   The "bouncing notes" strip below the bar is purely decorative -
 *   eight tiny `*` glyphs whose y-offset cycles through a 4-frame
 *   sine pattern keyed off the engine's currentStep(), so the row
 *   visually "dances" in lockstep with the equalizer (S191).
 *
 * Controls:
 *   - BTN_BACK / right softkey : pop the screen, leave the music
 *                                playing on the parent.
 *   - all other keys           : intentionally inert. The engine
 *                                is owned by the parent screen,
 *                                so karaoke is a passive overlay.
 *
 * Implementation notes:
 *   - 100% code-only, zero SPIFFS. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen reads as part
 *     of the same MAKERphone visual family as the rest of Phase-U.
 *   - A single lv_timer at 50 ms drives every animation. Slow
 *     enough to be cheap on the ESP32, fast enough that the
 *     fill-bar grow appears smooth on a 100-step progress bar.
 *   - When `Ringtone.isPlaying()` is false at construction time,
 *     the screen still draws (with caption "NO MELODY") rather
 *     than auto-popping - leaves the user a moment to read the
 *     state before they hit BACK themselves. When playback ends
 *     mid-screen the bar latches at 100% and the caption flips
 *     to "MELODY ENDED"; the screen does NOT auto-pop, mirroring
 *     the no-surprises rule the rest of Phase-U follows.
 */
class PhoneKaraokeScreen : public LVScreen, private InputListener {
public:
	PhoneKaraokeScreen();
	virtual ~PhoneKaraokeScreen() override;

	void onStart() override;
	void onStop() override;

	/** UI tick period (ms). Slow enough to be cheap, fast enough to
	 *  feel smooth on a 100-step progress bar and the bouncing-notes
	 *  strip. Same cadence PhoneMusicPlayer uses. */
	static constexpr uint16_t UpdatePeriodMs = 50;

	/** Width of the dim/cyan title row in pixels. The whole title
	 *  is rendered in pixelbasic16 inside a fixed-width strip so
	 *  the clip-mask math is straightforward. 144 px leaves an
	 *  8 px margin on either side of a 160 px screen. */
	static constexpr uint16_t TitleStripW = 144;

	/** Vertical y-position of the title row. Sits just under the
	 *  caption row in the layout above (kCaptionY+18). */
	static constexpr uint16_t TitleY = 32;

	/** Progress-bar geometry. */
	static constexpr uint16_t ProgressBarY = 64;
	static constexpr uint16_t ProgressBarW = 130;
	static constexpr uint16_t ProgressBarH = 6;

	/** Step caption row (centered). */
	static constexpr uint16_t StepCaptionY = 76;

	/** Bouncing-notes strip geometry. */
	static constexpr uint16_t NotesStripY  = 92;
	static constexpr uint8_t  NumNoteGlyphs = 8;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;     // "NOW PLAYING" / "NO MELODY" / "MELODY ENDED"
	lv_obj_t* dimTitle;         // dim baseline of the title (rendered behind)
	lv_obj_t* litClip;          // clip parent, width = progress * TitleStripW
	lv_obj_t* litTitle;         // sunset-orange title, clipped to the parent
	lv_obj_t* progressBg;       // dim purple track for the bar
	lv_obj_t* progressFill;     // sunset orange fill rectangle
	lv_obj_t* stepCaption;      // "STEP X / N"
	lv_obj_t* notes[NumNoteGlyphs];   // tiny "*" glyphs that bounce
	lv_obj_t* hintLabel;

	lv_timer_t* tickTimer;

	bool wasPlayingAtStart;     // remembered for the "NO MELODY" state
	bool everSawMelody;         // set true on the first non-empty title

	void buildHeader();
	void buildTitle();
	void buildProgress();
	void buildNotesStrip();
	void buildHint();

	void refreshAll();
	void refreshTitleText(const char* name);
	void refreshFill(uint16_t step, uint16_t total);
	void refreshNotes(uint16_t step);
	void refreshCaption(bool active, bool ever);

	static void onTickStatic(lv_timer_t* t);
	void onTick();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEKARAOKESCREEN_H
