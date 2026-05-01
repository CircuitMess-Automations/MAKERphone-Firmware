#include "PhoneMusicPlayer.h"

#include <stdio.h>
#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneRingtoneLibrary.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneMainMenu.cpp / PhoneHomeScreen.cpp / PhoneDialerScreen.cpp).
// Keeping the track title in cyan, the progress fill in sunset orange and
// the secondary captions in dim purple keeps the music player visually in
// the same family as every other phone-style screen.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange (progress fill)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan (title + active icon)
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple (progress track)
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream (idle icons)
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple sub-caption

// ---------------------------------------------------------------------------
// Default track list - the five S40 ringtone melodies. We hand the screen
// pointers into the static const Melody storage owned by PhoneRingtoneLibrary,
// so the array lifetime is the entire firmware lifetime (no GC concerns).
// S43 will swap this default for a proper 10-tune music library by simply
// calling setTracks() on the player; nothing else in the screen has to change.
// ---------------------------------------------------------------------------
static const PhoneRingtoneEngine::Melody* gDefaultTracks[PhoneRingtoneLibrary::Count] = { nullptr };

static const PhoneRingtoneEngine::Melody* const* defaultTracks(){
	// Lazy init - fill on first call, identical pointers on every subsequent
	// call. We deliberately avoid touching this at static-init time because
	// PhoneRingtoneLibrary::byIndex() reads from its own static storage and
	// the C++ initialisation order between TUs is not guaranteed.
	if(gDefaultTracks[0] == nullptr){
		for(uint8_t i = 0; i < PhoneRingtoneLibrary::Count; ++i){
			gDefaultTracks[i] = &PhoneRingtoneLibrary::byIndex(i);
		}
	}
	return gDefaultTracks;
}

PhoneMusicPlayer::PhoneMusicPlayer()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  titleLabel(nullptr),
		  indexLabel(nullptr),
		  progressBg(nullptr),
		  progressFill(nullptr),
		  timeLabel(nullptr),
		  prevIcon(nullptr),
		  playIcon(nullptr),
		  nextIcon(nullptr),
		  prevGlyph(nullptr),
		  playGlyph(nullptr),
		  nextGlyph(nullptr),
		  tracks(defaultTracks()),
		  trackCount(PhoneRingtoneLibrary::Count),
		  trackIndex(0),
		  playing(false),
		  trackTotalMs(0),
		  playStartMs(0),
		  pausedAtMs(0),
		  tickTimer(nullptr) {

	// Full-screen container, no scrollbars, no inner padding - same blank
	// canvas pattern PhoneHomeScreen / PhoneDialerScreen use. Children below
	// either pin themselves with IGNORE_LAYOUT or are LVGL primitives we
	// anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	// Centre stack: title -> caption -> progress bar -> time -> transport.
	buildTitle();
	buildProgressBar();
	buildTransport();

	// Bottom: feature-phone soft-keys. Left: PLAY/PAUSE depending on state;
	// right: EXIT (BTN_BACK) which stops + pops the screen.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PLAY");
	softKeys->setRight("EXIT");

	// Initial UI state: track 0, paused, 0% progress. We do NOT auto-play
	// on construction - user has to press PLAY (or ENTER) so the screen
	// does not start emitting tones the moment it is pushed. Matches the
	// expected "open the app, then start music" flow.
	refreshTrackLabels();
	refreshPlayIcon();
	refreshProgress();
}

PhoneMusicPlayer::~PhoneMusicPlayer() {
	// Defensive: stop any outstanding tick timer + ringtone playback. The
	// onStop() hook does the same thing, but covering both paths keeps us
	// safe if the screen is destroyed while never started (e.g. a host
	// constructed it and changed its mind before push()).
	if(tickTimer != nullptr){
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
	if(playing){
		Ringtone.stop();
		playing = false;
	}
	// Children (wallpaper, statusBar, softKeys, labels, icons) are all
	// parented to obj - LVGL frees them recursively when the screen's obj
	// is destroyed by the LVScreen base destructor. Nothing manual.
}

void PhoneMusicPlayer::onStart() {
	Input::getInstance()->addListener(this);

	// Tick every UpdatePeriodMs to drive the progress bar + time label.
	// We create the timer on every onStart() so a screen that is stopped
	// + restarted (e.g. user backs out of a sub-modal) does not run with
	// a stale handle. Pair with the matching lv_timer_del() in onStop().
	if(tickTimer == nullptr){
		tickTimer = lv_timer_create(onTick, UpdatePeriodMs, this);
	}
}

void PhoneMusicPlayer::onStop() {
	Input::getInstance()->removeListener(this);

	// Stop the ringtone engine so leaving the screen never leaks a buzzing
	// piezo into the parent screen. We also nuke our tick timer so the
	// screen has zero residual cost while invisible.
	if(playing){
		Ringtone.stop();
		playing = false;
	}
	if(tickTimer != nullptr){
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

// =========================================================================
// builders
// =========================================================================

void PhoneMusicPlayer::buildTitle() {
	// Track name in pixelbasic16, centred horizontally near the top - the
	// focal point of the screen, same y-band the dialer uses for its big
	// digit buffer so the visual hierarchy is consistent across phone apps.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(titleLabel, MP_HIGHLIGHT, 0);
	lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(titleLabel, 150);
	lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(titleLabel, "");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 14);

	// "Track X / N" caption directly under the title in pixelbasic7 +
	// dim purple. Tells the user where they are in the playlist without
	// stealing focus from the title.
	indexLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(indexLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(indexLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(indexLabel, "");
	lv_obj_set_align(indexLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(indexLabel, 34);
}

void PhoneMusicPlayer::buildProgressBar() {
	// Track background - dim purple slab spanning most of the screen
	// width. Two-pixel rounded corners give it a subtle pill shape that
	// still reads cleanly on the 160x128 panel.
	progressBg = lv_obj_create(obj);
	lv_obj_remove_style_all(progressBg);
	lv_obj_set_size(progressBg, BarWidth, BarHeight);
	lv_obj_set_pos(progressBg, BarLeft, BarY);
	lv_obj_set_style_bg_color(progressBg, MP_DIM, 0);
	lv_obj_set_style_bg_opa(progressBg, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(progressBg, 2, 0);
	lv_obj_set_style_border_width(progressBg, 0, 0);
	lv_obj_set_style_pad_all(progressBg, 0, 0);
	lv_obj_clear_flag(progressBg, LV_OBJ_FLAG_SCROLLABLE);

	// Fill - sunset orange, sized to BarWidth * progress when refreshed.
	// We use a sibling rectangle rather than a child so style_radius can
	// stay clipped to the parent without LVGL fighting us about layout.
	progressFill = lv_obj_create(obj);
	lv_obj_remove_style_all(progressFill);
	lv_obj_set_size(progressFill, 0, BarHeight);
	lv_obj_set_pos(progressFill, BarLeft, BarY);
	lv_obj_set_style_bg_color(progressFill, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(progressFill, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(progressFill, 2, 0);
	lv_obj_set_style_border_width(progressFill, 0, 0);
	lv_obj_set_style_pad_all(progressFill, 0, 0);
	lv_obj_clear_flag(progressFill, LV_OBJ_FLAG_SCROLLABLE);

	// "0:00 / 0:00" caption directly under the bar.
	timeLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(timeLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(timeLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(timeLabel, "0:00 / 0:00");
	lv_obj_set_align(timeLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(timeLabel, BarY + BarHeight + 4);
}

void PhoneMusicPlayer::buildTransport() {
	// Three transport icons in a horizontal row, centred. Each is a small
	// rounded rectangle (frame) with a glyph child label. Positioning is
	// pinned numerically to keep them centred on the 160 px panel.
	const uint16_t spacing = 8;
	const uint16_t totalW  = IconW * 3 + spacing * 2;
	const uint16_t startX  = (ScreenW - totalW) / 2;

	struct IconDesc {
		lv_obj_t** frame;
		lv_obj_t** glyph;
		const char* text;
		uint16_t x;
	};

	prevIcon = lv_obj_create(obj);
	playIcon = lv_obj_create(obj);
	nextIcon = lv_obj_create(obj);

	IconDesc descs[3] = {
		{ &prevIcon, &prevGlyph, "<<", (uint16_t)(startX) },
		{ &playIcon, &playGlyph, ">",  (uint16_t)(startX + IconW + spacing) },
		{ &nextIcon, &nextGlyph, ">>", (uint16_t)(startX + (IconW + spacing) * 2) }
	};

	for(uint8_t i = 0; i < 3; ++i){
		lv_obj_t* frame = *(descs[i].frame);
		lv_obj_remove_style_all(frame);
		lv_obj_set_size(frame, IconW, IconH);
		lv_obj_set_pos(frame, descs[i].x, IconY);
		lv_obj_set_style_bg_color(frame, MP_BG_DARK, 0);
		lv_obj_set_style_bg_opa(frame, LV_OPA_70, 0);
		lv_obj_set_style_radius(frame, 3, 0);
		lv_obj_set_style_border_color(frame, MP_LABEL_DIM, 0);
		lv_obj_set_style_border_width(frame, 1, 0);
		lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_all(frame, 0, 0);
		lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

		lv_obj_t* glyph = lv_label_create(frame);
		*(descs[i].glyph) = glyph;
		lv_obj_set_style_text_font(glyph, &pixelbasic7, 0);
		lv_obj_set_style_text_color(glyph, MP_TEXT, 0);
		lv_label_set_text(glyph, descs[i].text);
		lv_obj_center(glyph);
	}
}

// =========================================================================
// helpers
// =========================================================================

uint32_t PhoneMusicPlayer::computeTotalMs(const PhoneRingtoneEngine::Melody* m) {
	if(m == nullptr || m->notes == nullptr || m->count == 0) return 0;

	// Mirror the engine's loop() arithmetic: every step contributes its
	// note duration plus a per-note gap (gapMs is asserted between notes,
	// not after the last one when looping is off, but for total-display
	// purposes treating it as N steps * (dur + gap) is close enough that
	// the user never notices the off-by-one rounding on a 100 ms tick).
	uint32_t total = 0;
	for(uint16_t i = 0; i < m->count; ++i){
		total += m->notes[i].durationMs;
		if(i + 1 < m->count) total += m->gapMs;
	}
	if(total < 1) total = 1;   // guard div-by-zero in refreshProgress()
	return total;
}

uint32_t PhoneMusicPlayer::currentElapsedMs() const {
	if(trackTotalMs == 0) return 0;

	if(!playing){
		// Show a frozen elapsed value while paused. pausedAtMs is updated
		// every time we transition from playing -> paused, so the bar
		// stays exactly where the user left it.
		return pausedAtMs;
	}

	const uint32_t now = millis();
	const uint32_t since = now - playStartMs;
	// For looping melodies the bar should wrap around naturally. For
	// non-loop melodies the engine itself stops at end-of-track, which
	// we detect in onTick() via Ringtone.isPlaying() == false, so the
	// modulo here only matters during the brief overlap before that
	// detection fires.
	return since % trackTotalMs;
}

void PhoneMusicPlayer::refreshTrackLabels() {
	if(trackCount == 0 || tracks == nullptr || tracks[trackIndex] == nullptr){
		lv_label_set_text(titleLabel, "-");
		lv_label_set_text(indexLabel, "No tracks");
		return;
	}

	const PhoneRingtoneEngine::Melody* m = tracks[trackIndex];
	lv_label_set_text(titleLabel, (m->name != nullptr) ? m->name : "?");

	char buf[24];
	snprintf(buf, sizeof(buf), "Track %u / %u",
			 (unsigned)(trackIndex + 1),
			 (unsigned) trackCount);
	lv_label_set_text(indexLabel, buf);
}

void PhoneMusicPlayer::refreshPlayIcon() {
	// The middle button reads "||" while playing and ">" while paused -
	// classic media-player affordance. We flip both the glyph and the
	// frame border colour so the user has redundant cues for the state.
	if(playGlyph != nullptr){
		lv_label_set_text(playGlyph, playing ? "||" : ">");
		lv_obj_set_style_text_color(playGlyph,
									playing ? MP_HIGHLIGHT : MP_TEXT, 0);
	}
	if(playIcon != nullptr){
		lv_obj_set_style_border_color(playIcon,
									  playing ? MP_HIGHLIGHT : MP_LABEL_DIM, 0);
	}
	if(softKeys != nullptr){
		softKeys->setLeft(playing ? "PAUSE" : "PLAY");
	}
}

void PhoneMusicPlayer::refreshProgress() {
	const uint32_t elapsed = currentElapsedMs();
	const uint32_t total   = trackTotalMs;

	// Bar fill width - clamp to BarWidth so a brief overshoot before
	// onTick() catches end-of-track does not blow past the bar's right
	// edge.
	uint16_t fillW = 0;
	if(total > 0){
		uint32_t w = ((uint32_t) BarWidth) * elapsed / total;
		if(w > BarWidth) w = BarWidth;
		fillW = (uint16_t) w;
	}
	if(progressFill != nullptr){
		lv_obj_set_width(progressFill, fillW);
	}

	// "M:SS / M:SS" caption. We never expect a melody longer than a
	// couple of minutes (the longest S40 ringtone is ~3 s loop), but
	// using the M:SS format keeps room for the S43 music library which
	// may include longer tunes.
	if(timeLabel != nullptr){
		const uint32_t es = elapsed / 1000;
		const uint32_t ts = total   / 1000;
		char buf[24];
		snprintf(buf, sizeof(buf), "%lu:%02lu / %lu:%02lu",
				 (unsigned long)(es / 60), (unsigned long)(es % 60),
				 (unsigned long)(ts / 60), (unsigned long)(ts % 60));
		lv_label_set_text(timeLabel, buf);
	}
}

// =========================================================================
// public API
// =========================================================================

void PhoneMusicPlayer::setTracks(const PhoneRingtoneEngine::Melody* const* t,
								 uint8_t count) {
	tracks      = t;
	trackCount  = (t == nullptr) ? 0 : count;
	if(trackIndex >= trackCount) trackIndex = 0;

	// Stop any current playback so the new list always starts fresh.
	// The host can call play() / selectTrack() right after setTracks()
	// if they want a specific track to auto-play.
	if(playing){
		Ringtone.stop();
		playing = false;
	}
	pausedAtMs = 0;

	// Recompute the duration for the (possibly different) track 0.
	trackTotalMs = (trackCount > 0 && tracks[trackIndex] != nullptr)
				   ? computeTotalMs(tracks[trackIndex]) : 0;

	refreshTrackLabels();
	refreshPlayIcon();
	refreshProgress();
}

void PhoneMusicPlayer::selectTrack(uint8_t index) {
	if(trackCount == 0) return;
	if(index >= trackCount) index %= trackCount;
	trackIndex = index;
	pausedAtMs = 0;

	trackTotalMs = (tracks != nullptr && tracks[trackIndex] != nullptr)
				   ? computeTotalMs(tracks[trackIndex]) : 0;

	refreshTrackLabels();
	// Auto-play the new selection so the user gets immediate feedback
	// when skipping. Matches every other phone music player muscle
	// memory.
	play();
}

void PhoneMusicPlayer::play() {
	if(trackCount == 0 || tracks == nullptr || tracks[trackIndex] == nullptr) return;

	const PhoneRingtoneEngine::Melody* m = tracks[trackIndex];
	Ringtone.play(*m);

	// The S39 engine always restarts a melody from note 0 on play(), so
	// the visual bar is reset to 0 even if we are resuming from a pause.
	// Aligning the progress visual with the actual audio is more honest
	// than pretending we resumed mid-track.
	playing      = true;
	playStartMs  = millis();
	pausedAtMs   = 0;
	trackTotalMs = computeTotalMs(m);
	refreshPlayIcon();
	refreshProgress();
}

void PhoneMusicPlayer::pause() {
	if(!playing) return;
	pausedAtMs = currentElapsedMs();
	Ringtone.stop();
	playing = false;
	refreshPlayIcon();
	refreshProgress();
}

void PhoneMusicPlayer::togglePlay() {
	if(playing) pause();
	else        play();
}

void PhoneMusicPlayer::next() {
	if(trackCount == 0) return;
	selectTrack((uint8_t)((trackIndex + 1) % trackCount));
}

void PhoneMusicPlayer::prev() {
	if(trackCount == 0) return;
	const uint8_t newIdx = (trackIndex == 0)
						   ? (uint8_t)(trackCount - 1)
						   : (uint8_t)(trackIndex - 1);
	selectTrack(newIdx);
}

// =========================================================================
// timer + input
// =========================================================================

void PhoneMusicPlayer::onTick(lv_timer_t* t) {
	auto* self = static_cast<PhoneMusicPlayer*>(t->user_data);
	if(self == nullptr) return;

	// Auto-detect end-of-track for non-loop melodies: the engine has
	// already stopped, so we reflect that in our own state and either
	// advance or freeze the UI.
	if(self->playing && !Ringtone.isPlaying()){
		self->playing    = false;
		self->pausedAtMs = self->trackTotalMs;
		// For a non-loop track we advance to the next one for a smooth
		// "albums playing through" feel. With looping melodies this
		// branch never fires because Ringtone.isPlaying() stays true.
		if(self->trackCount > 1){
			self->next();
		}else{
			self->refreshPlayIcon();
		}
	}

	self->refreshProgress();
}

void PhoneMusicPlayer::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_L:
			prev();
			break;

		case BTN_RIGHT:
		case BTN_R:
			next();
			break;

		case BTN_ENTER:
			togglePlay();
			break;

		case BTN_BACK:
			// Always stop playback before popping so the parent screen
			// never inherits a buzzing piezo. The base LVScreen pop()
			// removes us from the stack; onStop() runs the rest of the
			// cleanup (input listener + tick timer).
			if(playing){
				Ringtone.stop();
				playing = false;
			}
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		default:
			break;
	}
}
