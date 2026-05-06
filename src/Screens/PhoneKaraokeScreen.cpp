#include "PhoneKaraokeScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneRingtoneEngine.h"

// MAKERphone retro palette - kept identical to every other Phone*
// widget so the karaoke screen reads visually as part of the same
// family PhoneMusicPlayer / PhoneRadio / PhoneBeatMaker live in.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim caption

namespace {
constexpr lv_coord_t kCaptionY    = 14;
constexpr lv_coord_t kHintY       = 106;

// Bouncing-notes strip parameters - eight glyphs spread across a
// 144 px strip so each glyph gets ~18 px of horizontal space, and
// a 4-frame sine offset (-2, 0, +2, 0) so the row visually "rides"
// the engine's currentStep() like a tiny equalizer.
constexpr int8_t kNoteOffsets[4] = { -2, 0, 2, 0 };
} // namespace

// =====================================================================
// Construction
// =====================================================================
PhoneKaraokeScreen::PhoneKaraokeScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  dimTitle(nullptr),
		  litClip(nullptr),
		  litTitle(nullptr),
		  progressBg(nullptr),
		  progressFill(nullptr),
		  stepCaption(nullptr),
		  hintLabel(nullptr),
		  tickTimer(nullptr),
		  wasPlayingAtStart(false),
		  everSawMelody(false) {

	for(uint8_t i = 0; i < NumNoteGlyphs; ++i) notes[i] = nullptr;

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order
	// (same pattern as PhoneMusicPlayer / PhoneRadio / PhoneBeatMaker).
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHeader();
	buildTitle();
	buildProgress();
	buildNotesStrip();
	buildHint();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");

	wasPlayingAtStart = Ringtone.isPlaying();
	everSawMelody     = wasPlayingAtStart;
	refreshAll();

	// Suppress unused-color warnings for palette macros we don't
	// actively style in this TU.
	(void) MP_BG_DARK;
	(void) MP_TEXT;
}

PhoneKaraokeScreen::~PhoneKaraokeScreen() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneKaraokeScreen::onStart() {
	Input::getInstance()->addListener(this);
	if(tickTimer == nullptr) {
		tickTimer = lv_timer_create(&PhoneKaraokeScreen::onTickStatic,
									UpdatePeriodMs, this);
	}
	refreshAll();
}

void PhoneKaraokeScreen::onStop() {
	Input::getInstance()->removeListener(this);
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

// =====================================================================
// Builders
// =====================================================================
void PhoneKaraokeScreen::buildHeader() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "NOW PLAYING");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
}

void PhoneKaraokeScreen::buildTitle() {
	// Dim baseline title. Centered, full TitleStripW wide so any
	// reasonably long melody name (PhoneMusicLibrary names top out
	// at "Sunset Blvd" - 11 glyphs - and PhoneRingtoneLibrary at
	// "Synthwave" / "Classic", so 144 px clears them all in
	// pixelbasic16).
	// Both labels live at x=8 so the 144-px title strip is centred
	// on the 160-px screen (left margin 8 px, right margin 8 px).
	// Absolute positioning keeps the clip container's x stable as
	// its width changes, which is what makes the fill grow strictly
	// rightward (rather than shifting back-and-forth as a centred
	// container would).
	const lv_coord_t kTitleX = 8;

	dimTitle = lv_label_create(obj);
	lv_obj_set_style_text_font(dimTitle, &pixelbasic16, 0);
	lv_obj_set_style_text_color(dimTitle, MP_DIM, 0);
	lv_obj_set_style_text_align(dimTitle, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(dimTitle, TitleStripW);
	lv_label_set_long_mode(dimTitle, LV_LABEL_LONG_DOT);
	lv_label_set_text(dimTitle, "");
	lv_obj_set_pos(dimTitle, kTitleX, TitleY);

	// Lit clip-container. Sits at the same x/y as the dim title
	// but holds an orange-tinted copy of the same text. By setting
	// the container's width to (progress * TitleStripW) the orange
	// child gets clipped to the progress fraction - which is the
	// karaoke "ball bouncing across the words" effect, simplified
	// to "left N px coloured, right rest dim". No per-character
	// math, no font measurement, no recolor parsing.
	litClip = lv_obj_create(obj);
	lv_obj_remove_style_all(litClip);
	lv_obj_set_size(litClip, 0, 18);
	lv_obj_set_pos(litClip, kTitleX, TitleY);
	lv_obj_set_style_bg_opa(litClip, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(litClip, 0, 0);
	lv_obj_set_style_radius(litClip, 0, 0);
	lv_obj_set_style_pad_all(litClip, 0, 0);
	lv_obj_clear_flag(litClip, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(litClip, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_scrollbar_mode(litClip, LV_SCROLLBAR_MODE_OFF);

	litTitle = lv_label_create(litClip);
	lv_obj_set_style_text_font(litTitle, &pixelbasic16, 0);
	lv_obj_set_style_text_color(litTitle, MP_ACCENT, 0);
	lv_obj_set_style_text_align(litTitle, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(litTitle, TitleStripW);
	lv_label_set_long_mode(litTitle, LV_LABEL_LONG_DOT);
	lv_label_set_text(litTitle, "");
	lv_obj_set_pos(litTitle, 0, 0);
}

void PhoneKaraokeScreen::buildProgress() {
	// Dim purple track (full width) + a sunset-orange fill that
	// grows left-to-right with progress. Identical silhouette to
	// PhoneMusicPlayer's progress bar so the karaoke screen reads
	// as a sibling view, not a separate app.
	progressBg = lv_obj_create(obj);
	lv_obj_remove_style_all(progressBg);
	lv_obj_set_size(progressBg, ProgressBarW, ProgressBarH);
	lv_obj_set_align(progressBg, LV_ALIGN_TOP_MID);
	lv_obj_set_y(progressBg, ProgressBarY);
	lv_obj_set_style_bg_color(progressBg, MP_DIM, 0);
	lv_obj_set_style_bg_opa(progressBg, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(progressBg, MP_LABEL_DIM, 0);
	lv_obj_set_style_border_width(progressBg, 1, 0);
	lv_obj_set_style_border_opa(progressBg, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(progressBg, 1, 0);
	lv_obj_clear_flag(progressBg, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(progressBg, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_scrollbar_mode(progressBg, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(progressBg, 0, 0);

	progressFill = lv_obj_create(progressBg);
	lv_obj_remove_style_all(progressFill);
	lv_obj_set_size(progressFill, 0, ProgressBarH - 2);
	lv_obj_set_pos(progressFill, 0, 0);
	lv_obj_set_style_bg_color(progressFill, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(progressFill, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(progressFill, 0, 0);
	lv_obj_set_style_radius(progressFill, 0, 0);
	lv_obj_clear_flag(progressFill, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(progressFill, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_scrollbar_mode(progressFill, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(progressFill, 0, 0);

	stepCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(stepCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(stepCaption, MP_LABEL_DIM, 0);
	lv_label_set_text(stepCaption, "STEP 0 / 0");
	lv_obj_set_align(stepCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(stepCaption, StepCaptionY);
}

void PhoneKaraokeScreen::buildNotesStrip() {
	// Eight tiny "*" glyphs evenly spread across the 144 px strip.
	// Their y-position cycles every tick keyed off the engine's
	// currentStep(), so the row dances even when the title fill
	// hasn't moved yet (e.g. on a long-duration note).
	const lv_coord_t stripX0 = 8;
	const lv_coord_t spacing = (lv_coord_t)(TitleStripW / NumNoteGlyphs);
	for(uint8_t i = 0; i < NumNoteGlyphs; ++i) {
		lv_obj_t* g = lv_label_create(obj);
		lv_obj_set_style_text_font(g, &pixelbasic7, 0);
		lv_obj_set_style_text_color(g, MP_HIGHLIGHT, 0);
		lv_label_set_text(g, "*");
		lv_obj_set_pos(g,
					   (lv_coord_t)(stripX0 + i * spacing + spacing / 2 - 2),
					   NotesStripY);
		notes[i] = g;
	}
}

void PhoneKaraokeScreen::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "BACK to exit karaoke");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, kHintY);
}

// =====================================================================
// Refreshers
// =====================================================================
void PhoneKaraokeScreen::refreshAll() {
	const bool        active = Ringtone.isPlaying();
	const char* const name   = active ? Ringtone.currentName() : nullptr;
	const uint16_t    step   = active ? Ringtone.currentStep()  : (uint16_t)0;
	const uint16_t    total  = active ? Ringtone.totalSteps()   : (uint16_t)0;
	if(active) everSawMelody = true;

	refreshTitleText(name != nullptr ? name : "");
	refreshFill(step, total);
	refreshNotes(step);
	refreshCaption(active, everSawMelody);
}

void PhoneKaraokeScreen::refreshTitleText(const char* name) {
	if(dimTitle == nullptr || litTitle == nullptr) return;

	// Substitute a clear placeholder so the screen is never blank -
	// gives the user something to read in the "no melody" / "ended"
	// states. The placeholder is fully dim (the lit clip is sized
	// to 0 in those states by refreshFill()).
	const char* shown = (name != nullptr && name[0] != '\0')
						? name
						: "(silent)";
	lv_label_set_text(dimTitle, shown);
	lv_label_set_text(litTitle, shown);
}

void PhoneKaraokeScreen::refreshFill(uint16_t step, uint16_t total) {
	uint16_t fillW = 0;
	uint16_t barW  = 0;
	if(total > 0) {
		// Step-based progress: 0-of-N renders empty, (N-1)-of-N
		// renders ~94% full, N-of-N renders 100% (handled by the
		// engine flipping !isPlaying when the last note ends - so
		// the bar latches at 100% rather than wrapping).
		uint32_t stepClamped = step;
		if(stepClamped >= total) stepClamped = total;
		fillW = (uint16_t) ((uint32_t) TitleStripW * stepClamped / total);
		barW  = (uint16_t) ((uint32_t) (ProgressBarW - 2) *
							stepClamped / total);
	}
	if(litClip != nullptr) {
		lv_obj_set_width(litClip, fillW);
	}
	if(progressFill != nullptr) {
		lv_obj_set_width(progressFill, barW);
	}
	if(stepCaption != nullptr) {
		char buf[20];
		snprintf(buf, sizeof(buf), "STEP %u / %u",
				 (unsigned) step, (unsigned) total);
		lv_label_set_text(stepCaption, buf);
	}
}

void PhoneKaraokeScreen::refreshNotes(uint16_t step) {
	for(uint8_t i = 0; i < NumNoteGlyphs; ++i) {
		if(notes[i] == nullptr) continue;
		// Each glyph picks an offset frame derived from the engine
		// step + the glyph's index, so adjacent glyphs are out of
		// phase by one frame and the row reads as a wave.
		const uint8_t frame = (uint8_t) ((step + i) & 0x03);
		const int8_t  dy    = kNoteOffsets[frame];
		lv_obj_set_y(notes[i], (lv_coord_t)(NotesStripY + dy));
	}
}

void PhoneKaraokeScreen::refreshCaption(bool active, bool ever) {
	if(captionLabel == nullptr) return;
	const char* text;
	lv_color_t  color;
	if(active) {
		text  = "NOW PLAYING";
		color = MP_HIGHLIGHT;
	} else if(ever) {
		text  = "MELODY ENDED";
		color = MP_LABEL_DIM;
	} else {
		text  = "NO MELODY";
		color = MP_LABEL_DIM;
	}
	lv_label_set_text(captionLabel, text);
	lv_obj_set_style_text_color(captionLabel, color, 0);
}

// =====================================================================
// Tick
// =====================================================================
void PhoneKaraokeScreen::onTickStatic(lv_timer_t* t) {
	if(t == nullptr) return;
	auto* self = static_cast<PhoneKaraokeScreen*>(t->user_data);
	if(self == nullptr) return;
	self->onTick();
}

void PhoneKaraokeScreen::onTick() {
	refreshAll();
}

// =====================================================================
// Input
// =====================================================================
void PhoneKaraokeScreen::buttonPressed(uint i) {
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
	}
	// Every other key is intentionally inert: the engine is owned
	// by the parent screen, so karaoke has no business stopping,
	// pausing, skipping or seeking from this overlay. The user
	// goes back to PhoneMusicPlayer for transport changes.
}
