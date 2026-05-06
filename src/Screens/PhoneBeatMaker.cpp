#include "PhoneBeatMaker.h"

#include <Audio/Piezo.h>
#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette — kept identical to every other Phone*
// widget so the beat-maker reads visually as part of the same family
// PhoneDrumKitScreen / PhoneDialerScreen / PhoneFortuneCookie live in.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim caption

// Track 0 = KICK (digit 1), 1 = SNARE (digit 2), 2 = HI-HAT closed
// (digit 3), 3 = CLAP (digit 0). The mapping is chosen so the four
// classic "boom-tss-bap-tss" voices line up top-to-bottom in
// frequency-content order, which makes the visual grid read in the
// same direction the ear hears the kit (low at top, high at bottom).
const uint8_t PhoneBeatMaker::kTrackDrumDigit[PhoneBeatMaker::NumTracks] = {
	1, // KICK
	2, // SNARE
	3, // HI-HAT
	0, // CLAP
};

// =====================================================================
// Geometry — 160x128, four-track 16-step grid
//
//   y =   0..  9    PhoneStatusBar
//   y =  12.. 19    "BEAT MAKER" caption (pixelbasic7 cyan)
//   y =  22.. 29    meta strip: BPM, STEP, transport glyph
//   y =  34..101    grid: 4 tracks * (13 px cell + 4 px gap)
//                    = 4*13 + 3*4 = 64 px → ends at 98
//   y = 105..114    bottom hint line (pixelbasic7 dim purple)
//   y = 118..127    PhoneSoftKeyBar
//
// Per-row geometry (label column + step grid):
//
//   x =   2.. 17    track-name label column (16 px wide)
//   x =  20..147    step grid: 16 cells * 8 px = 128 px
//                   with a 1-px visual gap every 4 cells (drawn as
//                   a slightly dimmer border, not a hole) so the
//                   four phrases of four steps each read as
//                   distinct sub-bars without breaking the grid
//                   geometry our cell-index math depends on.
// =====================================================================

namespace {
constexpr lv_coord_t kCaptionY    = 12;
constexpr lv_coord_t kMetaY       = 22;
constexpr lv_coord_t kGridY       = 34;
constexpr lv_coord_t kHintY       = 106;

constexpr lv_coord_t kLabelX      =  2;
constexpr lv_coord_t kGridX       = 20;
constexpr lv_coord_t kCellW       =  8;
constexpr lv_coord_t kCellH       = 13;
constexpr lv_coord_t kRowGap      =  4;

constexpr lv_coord_t cellX(uint8_t step) {
	return (lv_coord_t) (kGridX + step * kCellW);
}
constexpr lv_coord_t rowY(uint8_t track) {
	return (lv_coord_t) (kGridY + track * (kCellH + kRowGap));
}
} // namespace

// =====================================================================
// Construction
// =====================================================================

PhoneBeatMaker::PhoneBeatMaker()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  bpmLabel(nullptr),
		  stepLabel(nullptr),
		  transportLabel(nullptr),
		  hintLabel(nullptr) {

	for(uint8_t t = 0; t < NumTracks; ++t) {
		for(uint8_t s = 0; s < NumSteps; ++s) {
			cells[t][s]   = nullptr;
			pattern[t][s] = false;
		}
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order
	// (same pattern as PhoneDrumKitScreen / PhoneDialerScreen).
	wallpaper = new PhoneSynthwaveBg(obj);

	statusBar = new PhoneStatusBar(obj);

	buildHeader();
	buildGrid();
	buildHint();
	seedDefaultPattern();
	refreshAllCells();
	refreshBpmLabel();
	refreshStepLabel();
	refreshTransportLabel();

	// Right softkey reads BACK; the left softkey is intentionally
	// empty because there's no "commit" action — the pattern is
	// transient by design (see header comment). This matches the
	// PhoneDrumKitScreen softkey shape, which is the closest sibling.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");

	// Suppress unused-color warnings for palette macros we don't
	// actively style. Kept defined for parity with every other
	// Phone* screen so a future tweak is one-line.
	(void) MP_BG_DARK;
}

PhoneBeatMaker::~PhoneBeatMaker() {
	if(stepTimer != nullptr) {
		lv_timer_del(stepTimer);
		stepTimer = nullptr;
	}
	if(envTimer != nullptr) {
		lv_timer_del(envTimer);
		envTimer = nullptr;
	}
	// Belt-and-braces: silence the piezo on tear-down so a long
	// envelope (CRASH ~60 ms) that was mid-flight at pop() time
	// doesn't keep buzzing into whichever screen takes over.
	Piezo.noTone();
}

void PhoneBeatMaker::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneBeatMaker::onStop() {
	Input::getInstance()->removeListener(this);
	stopTransport();
	stopEnvelope();
}

// =====================================================================
// Builders
// =====================================================================

void PhoneBeatMaker::buildHeader() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "BEAT MAKER");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);

	bpmLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(bpmLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(bpmLabel, MP_TEXT, 0);
	lv_label_set_text(bpmLabel, "BPM 120");
	lv_obj_set_align(bpmLabel, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(bpmLabel, 2, kMetaY);

	stepLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(stepLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(stepLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(stepLabel, "STEP 01/16");
	lv_obj_set_align(stepLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(stepLabel, kMetaY);

	transportLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(transportLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(transportLabel, MP_DIM, 0);
	lv_label_set_text(transportLabel, "||");
	lv_obj_set_align(transportLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(transportLabel, -2, kMetaY);
}

void PhoneBeatMaker::buildGrid() {
	for(uint8_t t = 0; t < NumTracks; ++t) {
		// Track-name label on the left margin. Cyan because it's
		// effectively the "voice name" — same colour treatment
		// PhoneDrumKitScreen uses for its pad-digit caption.
		lv_obj_t* label = lv_label_create(obj);
		lv_obj_set_style_text_font(label, &pixelbasic7, 0);
		lv_obj_set_style_text_color(label, MP_HIGHLIGHT, 0);
		const PhoneDrumKitScreen::Drum* drum =
			PhoneDrumKitScreen::drumForDigit(kTrackDrumDigit[t]);
		lv_label_set_text(label, drum != nullptr ? drum->shortName : "??");
		lv_obj_set_pos(label,
					   kLabelX,
					   (lv_coord_t) (rowY(t) + (kCellH - 7) / 2));

		for(uint8_t s = 0; s < NumSteps; ++s) {
			buildCell(t, s, cellX(s), rowY(t));
		}
	}
}

void PhoneBeatMaker::buildCell(uint8_t track, uint8_t step,
							   lv_coord_t x, lv_coord_t y) {
	lv_obj_t* cell = lv_obj_create(obj);
	lv_obj_remove_style_all(cell);
	lv_obj_set_size(cell, kCellW - 1, kCellH);
	lv_obj_set_pos(cell, x, y);
	lv_obj_set_style_bg_color(cell, MP_DIM, 0);
	lv_obj_set_style_bg_opa(cell, 200, 0);
	lv_obj_set_style_border_color(cell, MP_DIM, 0);
	lv_obj_set_style_border_width(cell, 1, 0);
	lv_obj_set_style_border_opa(cell, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(cell, 1, 0);
	lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_scrollbar_mode(cell, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(cell, 0, 0);

	cells[track][step] = cell;
}

void PhoneBeatMaker::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	// Compact hint that fits 160 px in pixelbasic7. The dialer-style
	// "L/R" is the row swap (track), arrows are the column step,
	// "0" is play/stop, "ENT" is toggle. The text uses ASCII arrows
	// (< / >) so it renders correctly under pixelbasic7 (which
	// doesn't carry the unicode ◀ / ▶ glyphs).
	lv_label_set_text(hintLabel, "L/R TRK <> STEP  0:PLAY  ENT:TOGGLE");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(hintLabel, 2, kHintY);
}

void PhoneBeatMaker::seedDefaultPattern() {
	// Canonical four-on-the-floor groove, written out long-hand
	// rather than algorithmically so the seed is obvious in a
	// diff:
	//
	//   KICK  on steps 0 and 8                 (beat 1 and beat 3)
	//   SNARE on steps 4 and 12                (beat 2 and beat 4)
	//   HHT   on every other 16th (0,2,4,...)  (8th-note hat)
	//   CLAP  empty                            (room for the user)
	//
	// This is the "boom-tss-bap-tss" pattern the screen is paying
	// homage to, and 9 out of 10 listeners can hum it on cue.
	pattern[0][0]  = true;
	pattern[0][8]  = true;

	pattern[1][4]  = true;
	pattern[1][12] = true;

	for(uint8_t s = 0; s < NumSteps; s += 2) {
		pattern[2][s] = true;
	}
}

// =====================================================================
// Refreshers
// =====================================================================

void PhoneBeatMaker::refreshBpmLabel() {
	if(bpmLabel == nullptr) return;
	char buf[12];
	snprintf(buf, sizeof(buf), "BPM %u", (unsigned) bpm);
	lv_label_set_text(bpmLabel, buf);
}

void PhoneBeatMaker::refreshStepLabel() {
	if(stepLabel == nullptr) return;
	// 1-indexed for the user — feature-phone displays count from 1
	// in every other context (track 1, message 1, ...), so the
	// sequencer follows suit.
	char buf[16];
	snprintf(buf, sizeof(buf), "STEP %02u/%02u",
			 (unsigned) (playStep + 1), (unsigned) NumSteps);
	lv_label_set_text(stepLabel, buf);
}

void PhoneBeatMaker::refreshTransportLabel() {
	if(transportLabel == nullptr) return;
	if(playing) {
		lv_label_set_text(transportLabel, ">");
		lv_obj_set_style_text_color(transportLabel, MP_HIGHLIGHT, 0);
	} else {
		lv_label_set_text(transportLabel, "||");
		lv_obj_set_style_text_color(transportLabel, MP_DIM, 0);
	}
}

void PhoneBeatMaker::refreshCell(uint8_t track, uint8_t step) {
	if(track >= NumTracks || step >= NumSteps) return;
	lv_obj_t* cell = cells[track][step];
	if(cell == nullptr) return;

	const bool isLit       = pattern[track][step];
	const bool isCursor    = (cursorTrack == track && cursorStep == step);
	const bool isPlayhead  = (playing && playStep == step);
	const bool onBeat      = (step % 4) == 0;

	// Fill colour — orange when the cell is "armed" in the pattern,
	// dim purple otherwise. The on-beat columns get a slightly
	// brighter dim purple so the eye can count the four sub-bars
	// of four 16ths each without the gridlines fragmenting the
	// geometry.
	if(isLit) {
		lv_obj_set_style_bg_color(cell, MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
	} else {
		lv_obj_set_style_bg_color(cell, MP_DIM, 0);
		lv_obj_set_style_bg_opa(cell, onBeat ? 220 : 160, 0);
	}

	// Border — cyan for the playhead column (so the user can see
	// which step is currently sounding), brighter cream for the
	// cursor (so the user can see which step they're editing),
	// dim purple otherwise.
	if(isPlayhead) {
		lv_obj_set_style_border_color(cell, MP_HIGHLIGHT, 0);
		lv_obj_set_style_border_width(cell, 1, 0);
	} else if(isCursor) {
		lv_obj_set_style_border_color(cell, MP_TEXT, 0);
		lv_obj_set_style_border_width(cell, 1, 0);
	} else {
		// On-beat columns get a slightly stronger border so the
		// 4-of-16 rhythm is visible even at rest. Mid-beat
		// columns fade to MP_DIM.
		lv_obj_set_style_border_color(cell,
									  onBeat ? MP_LABEL_DIM : MP_DIM, 0);
		lv_obj_set_style_border_width(cell, 1, 0);
	}
}

void PhoneBeatMaker::refreshAllCells() {
	for(uint8_t t = 0; t < NumTracks; ++t) {
		for(uint8_t s = 0; s < NumSteps; ++s) {
			refreshCell(t, s);
		}
	}
}

// =====================================================================
// Cursor / pattern editing
// =====================================================================

void PhoneBeatMaker::moveCursor(int8_t deltaTrack, int8_t deltaStep) {
	const uint8_t prevTrack = cursorTrack;
	const uint8_t prevStep  = cursorStep;

	int8_t newTrack = (int8_t) cursorTrack + deltaTrack;
	int8_t newStep  = (int8_t) cursorStep  + deltaStep;

	// Wrap rather than clamp — feels more like a tracker / drum
	// machine, where the grid is a torus and the cursor never
	// "stops" at the edge.
	while(newTrack < 0)               newTrack += NumTracks;
	while(newTrack >= (int8_t) NumTracks) newTrack -= NumTracks;
	while(newStep < 0)                newStep  += NumSteps;
	while(newStep  >= (int8_t) NumSteps)  newStep  -= NumSteps;

	cursorTrack = (uint8_t) newTrack;
	cursorStep  = (uint8_t) newStep;

	// Repaint just the two cells whose cursor flag changed —
	// avoids a full grid refresh on every keypress.
	refreshCell(prevTrack, prevStep);
	refreshCell(cursorTrack, cursorStep);
}

void PhoneBeatMaker::toggleCell() {
	pattern[cursorTrack][cursorStep] = !pattern[cursorTrack][cursorStep];
	refreshCell(cursorTrack, cursorStep);
	// Audible confirmation: fire the drum voice for the toggled
	// track immediately so the user hears what they just armed.
	// Only fires when toggling ON — toggling OFF stays silent so
	// rapid editing doesn't smear voices.
	if(pattern[cursorTrack][cursorStep]) {
		const PhoneDrumKitScreen::Drum* drum =
			PhoneDrumKitScreen::drumForDigit(kTrackDrumDigit[cursorTrack]);
		startEnvelope(drum);
	}
}

void PhoneBeatMaker::clearPattern() {
	for(uint8_t t = 0; t < NumTracks; ++t) {
		for(uint8_t s = 0; s < NumSteps; ++s) {
			pattern[t][s] = false;
		}
	}
	refreshAllCells();
}

void PhoneBeatMaker::nudgeBpm(int8_t delta) {
	int16_t newBpm = (int16_t) bpm + (int16_t) (delta * (int8_t) BpmNudge);
	if(newBpm < (int16_t) MinBpm) newBpm = MinBpm;
	if(newBpm > (int16_t) MaxBpm) newBpm = MaxBpm;
	if((uint8_t) newBpm == bpm) return;
	bpm = (uint8_t) newBpm;
	refreshBpmLabel();
	if(playing) rearmStepTimer();
}

// =====================================================================
// Transport
// =====================================================================

uint32_t PhoneBeatMaker::stepIntervalMs() const {
	// 16th-note interval at 4/4: one beat = 60000 / BPM ms,
	// one 16th = beat / 4. Integer math is fine — at 240 BPM
	// we get 62 ms (rounded down by 1), which the user's ear
	// will not notice on a piezo.
	return (uint32_t) (60000UL / (uint32_t) bpm / 4UL);
}

void PhoneBeatMaker::startTransport() {
	if(playing) return;
	playing  = true;
	playStep = 0;
	refreshAllCells();
	refreshStepLabel();
	refreshTransportLabel();
	rearmStepTimer();
	// Fire step 0 immediately on play so the downbeat lands on the
	// instant the user pressed play, not one tick later.
	fireStep(playStep);
}

void PhoneBeatMaker::stopTransport() {
	if(!playing) {
		// Even if we weren't actively playing, kill any envelope
		// that might still be running (e.g. user toggled ON, then
		// hit BACK before the envelope finished).
		stopEnvelope();
		return;
	}
	playing = false;
	if(stepTimer != nullptr) {
		lv_timer_del(stepTimer);
		stepTimer = nullptr;
	}
	stopEnvelope();
	refreshAllCells();
	refreshTransportLabel();
}

void PhoneBeatMaker::rearmStepTimer() {
	if(stepTimer != nullptr) {
		lv_timer_del(stepTimer);
		stepTimer = nullptr;
	}
	stepTimer = lv_timer_create(&PhoneBeatMaker::onStepTimerStatic,
								stepIntervalMs(), this);
}

void PhoneBeatMaker::onStepTick() {
	if(!playing) return;

	const uint8_t prevStep = playStep;
	playStep = (uint8_t) ((playStep + 1) % NumSteps);
	// Repaint only the two columns whose playhead flag changed.
	for(uint8_t t = 0; t < NumTracks; ++t) {
		refreshCell(t, prevStep);
		refreshCell(t, playStep);
	}
	refreshStepLabel();
	fireStep(playStep);
}

void PhoneBeatMaker::fireStep(uint8_t step) {
	if(step >= NumSteps) return;
	// Lowest-numbered active track wins — KICK > SNARE > HI-HAT >
	// CLAP. Documented in the header comment; this single line is
	// the entire policy.
	for(uint8_t t = 0; t < NumTracks; ++t) {
		if(!pattern[t][step]) continue;
		const PhoneDrumKitScreen::Drum* drum =
			PhoneDrumKitScreen::drumForDigit(kTrackDrumDigit[t]);
		startEnvelope(drum);
		return;
	}
}

// =====================================================================
// Drum envelope (mirrors PhoneDrumKitScreen)
// =====================================================================

void PhoneBeatMaker::startEnvelope(const PhoneDrumKitScreen::Drum* drum) {
	if(drum == nullptr || drum->frameCount == 0) return;

	stopEnvelope();

	activeDrum    = drum;
	activeFrameIx = 0;

	if(Settings.get().sound) {
		const PhoneDrumKitScreen::Frame& f0 = drum->frames[0];
		if(f0.freq != 0) Piezo.tone(f0.freq, f0.durMs);
	}

	envTimer = lv_timer_create(&PhoneBeatMaker::onEnvTimerStatic,
							   drum->frames[0].durMs, this);
	lv_timer_set_repeat_count(envTimer, 1);
}

void PhoneBeatMaker::stepEnvelope() {
	envTimer = nullptr;
	if(activeDrum == nullptr) return;

	activeFrameIx++;
	if(activeFrameIx >= activeDrum->frameCount) {
		Piezo.noTone();
		activeDrum    = nullptr;
		activeFrameIx = 0;
		return;
	}

	const PhoneDrumKitScreen::Frame& f = activeDrum->frames[activeFrameIx];
	if(Settings.get().sound) {
		if(f.freq == 0) {
			Piezo.noTone();
		} else {
			Piezo.tone(f.freq, f.durMs);
		}
	}

	envTimer = lv_timer_create(&PhoneBeatMaker::onEnvTimerStatic,
							   f.durMs, this);
	lv_timer_set_repeat_count(envTimer, 1);
}

void PhoneBeatMaker::stopEnvelope() {
	if(envTimer != nullptr) {
		lv_timer_del(envTimer);
		envTimer = nullptr;
	}
	if(activeDrum != nullptr) {
		Piezo.noTone();
		activeDrum    = nullptr;
		activeFrameIx = 0;
	}
}

void PhoneBeatMaker::onEnvTimerStatic(lv_timer_t* t) {
	if(t == nullptr) return;
	auto* self = static_cast<PhoneBeatMaker*>(t->user_data);
	if(self == nullptr) return;
	self->stepEnvelope();
}

void PhoneBeatMaker::onStepTimerStatic(lv_timer_t* t) {
	if(t == nullptr) return;
	auto* self = static_cast<PhoneBeatMaker*>(t->user_data);
	if(self == nullptr) return;
	self->onStepTick();
}

// =====================================================================
// Input
// =====================================================================

void PhoneBeatMaker::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:  moveCursor(0, -1); break;
		case BTN_RIGHT: moveCursor(0, +1); break;

		// BTN_L / BTN_R are the shoulder buttons; they walk the
		// cursor across tracks. We deliberately don't reuse
		// BTN_UP/BTN_DOWN — those macros are aliased to LEFT/RIGHT
		// on the Chatter, so the column-step keys would conflict.
		case BTN_L:     moveCursor(-1, 0); break;
		case BTN_R:     moveCursor(+1, 0); break;

		case BTN_ENTER: toggleCell(); break;

		case BTN_0:
			if(playing) stopTransport(); else startTransport();
			break;

		case BTN_5:
			clearPattern();
			break;

		case BTN_2:
			nudgeBpm(+1);
			break;

		case BTN_8:
			nudgeBpm(-1);
			break;

		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			stopTransport();
			stopEnvelope();
			pop();
			break;

		default:
			break;
	}
}
