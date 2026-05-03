#include "PhonePowerDown.h"

#include <Audio/Piezo.h>
#include <Settings.h>
#include <math.h>
#include <string.h>

#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established convention in
// this codebase (see PhoneBootSplash / PhoneCallEnded / PhoneSynthwaveBg).
// Keeping the palette local to each screen makes the Phase-K widgets
// relocatable without dragging a shared header along - the trade-off is
// each new screen restating the same handful of constants. The CRT
// shrink uses warm cream as the bright phosphor and cyan as the
// scanline halo so the collapsing plate visually rhymes with the boot
// splash's wordmark + tagline pair.
#define MP_CREAM       lv_color_make(255, 220, 180)   // bright phosphor centre
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan scanline halo
#define MP_BLACK       lv_color_make(  0,   0,   0)   // CRT-off canvas

// CRT-tone envelope, in Hz. The piezo descends exponentially across the
// run so the perceived pitch drop sounds linear to the human ear (a CRT
// flyback transformer winding down is the reference). Final click is
// emitted as a brief tap at TONE_CLICK to give the animation a tactile
// "and then it's off" cue.
static constexpr uint16_t TONE_START_HZ  = 1500;
static constexpr uint16_t TONE_END_HZ    =  180;
static constexpr uint16_t TONE_CLICK_HZ  =   90;

// Pixel budgets for the phosphor plate. The plate is centred on the
// 160x128 display. The minimum height during phase 0 is 2 px so the
// horizontal scan band still reads as a discrete element (a single
// LVGL pixel with a 1 px outline would render 3 px tall, which is too
// thick when we are about to collapse it horizontally next).
static constexpr int16_t PLATE_W_FULL = 160;
static constexpr int16_t PLATE_H_FULL = 128;
static constexpr int16_t PLATE_H_MIN  =   2;
static constexpr int16_t PLATE_W_MIN  =   4;

// Afterglow dot sizing. Slightly smaller than the plate's collapsed
// width so the eye reads "the band has *settled* into a dot" rather
// than "the band became a square of the same width".
static constexpr int16_t DOT_SIZE = 4;

PhonePowerDown::PhonePowerDown(DismissHandler onComplete,
							   uint32_t       durationMs)
		: LVScreen(),
		  dismissCb(onComplete),
		  durationMs(durationMs),
		  elapsedMs(0),
		  firedAlready(false),
		  plate(nullptr),
		  dot(nullptr),
		  tickTimer(nullptr),
		  // S146 - default to no message; fall through to the persisted
		  // Settings.powerOffMessage slot below. messagePreambleMs stays
		  // zero until setMessage() decides we have something to paint.
		  messagePreambleMs(0),
		  messageLabel(nullptr) {

	// Zero-init the message buffer so an empty Settings slot leaves us
	// in the "no preamble" branch even if the firmware boots before
	// SettingsImpl::begin() loads from NVS.
	message[0] = '\0';

	// Full-screen black canvas, no scrollbars, no inner padding. Same
	// blank-canvas pattern as every other Phone* screen. Children are
	// anchored manually and centred inside the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	// The canvas behind the plate must be hard black so the collapsing
	// rectangle reads against true zero, not a theme-tinted background.
	// Without this the phosphor plate's edges blur into whatever the
	// LVGL default theme paints for an LV_OBJ at z=0.
	lv_obj_set_style_bg_color(obj, MP_BLACK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

	// Build the visual stack from back to front: plate first (the
	// shrinking phosphor band) and dot last (the centre afterglow that
	// becomes visible only in phase 2). LVGL z-orders children by
	// creation order on a non-scrollable parent, so the dot ends up on
	// top of the plate during the fade-out crossover frame where both
	// are technically visible at the centre.
	buildPlate();
	buildDot();
	buildMessageLabel();

	// S146 - pull the persisted custom power-off message (if any) into
	// the screen's local buffer. setMessage() also recomputes
	// messagePreambleMs and toggles the message label's visibility, so
	// after this call the screen is fully primed for either branch
	// (preamble + shrink, or shrink-only) without any further wiring.
	const char* persisted = Settings.get().powerOffMessage;
	setMessage(persisted);
}

PhonePowerDown::~PhonePowerDown() {
	// Cancel the animation ticker ahead of LVGL teardown so its
	// callback never fires against a freed screen during destruction.
	stopTicker();
	// The piezo may still be holding a tone if the destructor runs
	// mid-animation (e.g. host force-pops us). Silence it defensively
	// so a stuck PWM does not survive the screen swap.
	Piezo.noTone();
	// All children (plate, dot) are parented to obj - LVScreen's
	// destructor frees obj and LVGL recursively tears down the rest.
}

// ---- lifecycle -------------------------------------------------------------

void PhonePowerDown::onStart() {
	// Reset state so a screen reused across multiple runs (unlikely
	// for a power-down overlay, but cheap to support) starts fresh.
	elapsedMs = 0;
	firedAlready = false;

	// Render frame 0 explicitly so the very first paint shows a
	// full-bleed phosphor plate rather than whatever geometry the ctor
	// left behind. The piezo also kicks the descending sweep at the
	// top of the envelope.
	applyFrame(0.0f);
	applyTone(0.0f);

	startTicker();
}

void PhonePowerDown::onStop() {
	stopTicker();
	Piezo.noTone();
}

// ---- builders --------------------------------------------------------------

void PhonePowerDown::buildPlate() {
	// The shrinking phosphor plate. Vertical 2-stop gradient (warm
	// cream at the top, cyan at the bottom) so the eye reads
	// "still-bright phosphor with a CRT scanline tint" even when the
	// plate is mid-collapse. The plate has a 1 px cyan outline that
	// gives it a soft scanline halo without needing canvas-level masking.
	plate = lv_obj_create(obj);
	lv_obj_remove_style_all(plate);
	lv_obj_clear_flag(plate, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(plate, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(plate, PLATE_W_FULL, PLATE_H_FULL);
	lv_obj_set_align(plate, LV_ALIGN_CENTER);
	lv_obj_set_style_radius(plate, 0, 0);
	lv_obj_set_style_bg_color(plate, MP_CREAM, 0);
	lv_obj_set_style_bg_grad_color(plate, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_grad_dir(plate, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
	// Soft cyan outline at ~50% so the plate's edge reads as a CRT
	// scanline halo rather than a hard rectangle.
	lv_obj_set_style_outline_color(plate, MP_HIGHLIGHT, 0);
	lv_obj_set_style_outline_width(plate, 1, 0);
	lv_obj_set_style_outline_opa(plate, LV_OPA_50, 0);
}

void PhonePowerDown::buildMessageLabel() {
	// Build the message label once, anchored at the centre of the
	// 160 x 128 display. Hidden by default so the original S57 visual
	// timeline is preserved exactly when the powerOffMessage slot is
	// empty -- setMessage() unhides + repaints it when there is text
	// to show. Uses pixelbasic16 (the bigger of the two retro fonts)
	// so even short three-character messages read clearly across the
	// phosphor plate.
	messageLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(messageLabel, &pixelbasic16, 0);
	// Deep-purple text reads as a hard CRT phosphor caption against
	// the warm-cream plate. We deliberately do NOT use cyan/orange
	// here -- the message must contrast strongly with the cream
	// gradient under it, and deep purple is the inverse pole of the
	// MAKERphone palette.
	lv_obj_set_style_text_color(messageLabel,
								lv_color_make(20, 12, 36),
								0);
	// Centred horizontally on the 160 px display, vertically on the
	// 128 px height -- exactly the centre of the phosphor plate so
	// the eye reads "the message lives ON the screen, not floating
	// above it".
	lv_obj_set_align(messageLabel, LV_ALIGN_CENTER);
	// Single-line, dot-truncate when the message is too wide for the
	// 156 px usable plate. Keeps an accidentally-long entry from
	// wrapping into the plate's collapse line at phase 1.
	lv_obj_set_width(messageLabel, 156);
	lv_label_set_long_mode(messageLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_style_text_align(messageLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(messageLabel, "");
	// Hidden by default. setMessage() will unhide when there is
	// content to display.
	lv_obj_add_flag(messageLabel, LV_OBJ_FLAG_HIDDEN);
}

void PhonePowerDown::setMessage(const char* text) {
	// Defensive: a null pointer collapses to "no message" so the
	// preamble drops out cleanly. The empty-string branch matches.
	if(text == nullptr || text[0] == '\0') {
		message[0] = '\0';
		messagePreambleMs = 0;
		if(messageLabel != nullptr) {
			lv_label_set_text(messageLabel, "");
			lv_obj_add_flag(messageLabel, LV_OBJ_FLAG_HIDDEN);
		}
		return;
	}

	// Copy into the local 24-byte buffer (23 chars + nul). strncpy is
	// safe here because we always nul-terminate the destination after
	// the copy, regardless of whether the source ran past the cap.
	const size_t maxCopy = MessageMaxLen;
	size_t i = 0;
	for(; i < maxCopy && text[i] != '\0'; ++i) {
		message[i] = text[i];
	}
	message[i] = '\0';

	messagePreambleMs = DefaultMessagePreambleMs;

	if(messageLabel != nullptr) {
		lv_label_set_text(messageLabel, message);
		lv_obj_clear_flag(messageLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

bool PhonePowerDown::applyPreamble() {
	// No message persisted (or explicitly cleared via setMessage("")):
	// drop straight through to the existing CRT-shrink timeline. This
	// preserves the original S57 behaviour pixel-for-pixel for every
	// host that has not opted into the S146 custom message feature.
	if(messagePreambleMs == 0) {
		if(messageLabel != nullptr) {
			lv_obj_add_flag(messageLabel, LV_OBJ_FLAG_HIDDEN);
		}
		return false;
	}

	// Inside the preamble window: hold the plate at full bleed, hide
	// the afterglow dot, and unhide the message label so the user
	// reads "BYE!" (or whatever they typed) over a steady warm-cream
	// background. The piezo descending sweep is suppressed for the
	// duration of the preamble by applyTone()'s own check on
	// elapsedMs < messagePreambleMs.
	if(elapsedMs < messagePreambleMs) {
		lv_obj_set_size(plate, 160, 128);
		lv_obj_clear_flag(plate, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
		if(messageLabel != nullptr) {
			lv_obj_clear_flag(messageLabel, LV_OBJ_FLAG_HIDDEN);
		}
		return true;
	}

	// First tick past the preamble: hide the message so it does not
	// fight the collapsing plate. The CRT-shrink path takes over from
	// here via applyFrame(progress) below.
	if(messageLabel != nullptr) {
		lv_obj_add_flag(messageLabel, LV_OBJ_FLAG_HIDDEN);
	}
	return false;
}

void PhonePowerDown::buildDot() {
	// Centre afterglow dot. Hidden by default (LV_OBJ_FLAG_HIDDEN) so
	// frames during the vertical / horizontal collapse phases see a
	// pristine "plate only" composition. Phase 2 unhides it and walks
	// its background opacity from full warm cream down to zero.
	dot = lv_obj_create(obj);
	lv_obj_remove_style_all(dot);
	lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(dot, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
	lv_obj_set_align(dot, LV_ALIGN_CENTER);
	lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(dot, MP_CREAM, 0);
	lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
	// Soft cyan halo at ~40 % so the cooling dot still reads warm.
	lv_obj_set_style_outline_color(dot, MP_HIGHLIGHT, 0);
	lv_obj_set_style_outline_width(dot, 1, 0);
	lv_obj_set_style_outline_opa(dot, LV_OPA_40, 0);
	lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
}

// ---- ticker + frame composition --------------------------------------------

void PhonePowerDown::startTicker() {
	// Idempotent: if a previous onStart() left a timer running we reuse
	// it rather than stacking a second one. The 30 ms cadence is fast
	// enough for a smooth read on the 160x128 display without burning
	// LVGL repaints during the still frames at the start of the run.
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(onTickStatic, AnimTickMs, this);
}

void PhonePowerDown::stopTicker() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhonePowerDown::onTickStatic(lv_timer_t* timer) {
	auto self = static_cast<PhonePowerDown*>(timer->user_data);
	if(self == nullptr) return;

	// Advance the wall-clock counter by the timer's nominal cadence.
	// LVGL does not expose the actual delta on a periodic timer, but
	// the tick rate is fixed so the cumulative drift across the
	// preamble + shrink run is well under a frame's worth of error.
	// Cap at the total run length (preamble + shrink) so a slow LVGL
	// loop that drops a frame does not blow the progress past 1.0f
	// and confuse the phase math.
	if(self->firedAlready) return;

	const uint32_t totalMs = self->messagePreambleMs + self->durationMs;
	self->elapsedMs += AnimTickMs;
	if(self->elapsedMs > totalMs) self->elapsedMs = totalMs;

	// S146 - while the preamble is active the message label paints
	// over a full-bleed plate and the CRT-shrink phase math is
	// suppressed. applyTone still sees the raw progress so the
	// piezo sweep stays muted during the hold (handled inside
	// applyTone via the same elapsedMs < messagePreambleMs check).
	const bool inPreamble = self->applyPreamble();

	// Compute the post-preamble progress (0.0..1.0 across the original
	// CRT-shrink window) and let the existing applyFrame /
	// applyTone branch unchanged. Outside the preamble window
	// elapsedMs ranges over [messagePreambleMs .. totalMs], so we
	// subtract the preamble offset before normalising.
	float progress = 0.0f;
	if(!inPreamble){
		const uint32_t shrinkElapsed =
			self->elapsedMs > self->messagePreambleMs
				? self->elapsedMs - self->messagePreambleMs
				: 0;
		progress = self->durationMs == 0
					   ? 1.0f
					   : (float) shrinkElapsed / (float) self->durationMs;
		if(progress > 1.0f) progress = 1.0f;
		self->applyFrame(progress);
	}

	self->applyTone(progress);

	if(self->elapsedMs >= totalMs){
		self->fireComplete();
	}
}

void PhonePowerDown::applyFrame(float progress) {
	if(progress < 0.0f) progress = 0.0f;
	if(progress > 1.0f) progress = 1.0f;

	// ---- Phase 0: vertical collapse (full plate -> 2 px tall band) ----
	if(progress <= PhaseVerticalEnd){
		// Map [0 .. PhaseVerticalEnd] onto [0 .. 1]. Eased with a
		// gentle quadratic so the plate sits "still" for a couple of
		// frames before snapping inward, which sells the CRT power-cut
		// feel better than a linear ramp.
		const float t = progress / PhaseVerticalEnd;
		const float eased = t * t;
		int16_t h = (int16_t) ((1.0f - eased) * (float) PLATE_H_FULL);
		if(h < PLATE_H_MIN) h = PLATE_H_MIN;
		lv_obj_set_size(plate, PLATE_W_FULL, h);
		lv_obj_clear_flag(plate, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(dot,    LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// ---- Phase 1: horizontal collapse (full-width band -> centre dot) ----
	if(progress <= PhaseHorizontalEnd){
		const float span = PhaseHorizontalEnd - PhaseVerticalEnd;
		const float t    = (progress - PhaseVerticalEnd) / span;
		const float eased = t;  // linear feels right for the snap-inward
		int16_t w = (int16_t) ((1.0f - eased) * (float) PLATE_W_FULL);
		if(w < PLATE_W_MIN) w = PLATE_W_MIN;
		lv_obj_set_size(plate, w, PLATE_H_MIN);
		lv_obj_clear_flag(plate, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(dot,    LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// ---- Phase 2: afterglow dot fade ----
	// The plate has done its job; hide it and let the centre dot
	// carry the rest of the run. Opacity walks linearly from full
	// (LV_OPA_COVER == 255) to zero across the remaining progress
	// window, so the final frame paints pure black.
	lv_obj_add_flag(plate, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);

	const float span = 1.0f - PhaseHorizontalEnd;
	const float t    = (progress - PhaseHorizontalEnd) / span;
	float opaF = (1.0f - t) * 255.0f;
	if(opaF < 0.0f)   opaF = 0.0f;
	if(opaF > 255.0f) opaF = 255.0f;
	const lv_opa_t opa = (lv_opa_t) opaF;
	lv_obj_set_style_bg_opa(dot, opa, 0);
	lv_obj_set_style_outline_opa(dot, opa, 0);
}

void PhonePowerDown::applyTone(float progress) {
	if(progress < 0.0f) progress = 0.0f;
	if(progress > 1.0f) progress = 1.0f;

	// Honour the user's global mute setting. PhoneSoundScreen / S52
	// drives Settings.sound; if it is off we leave the piezo silent
	// and let the visual carry the animation alone. This matches the
	// way PhoneRingtoneEngine eats its own output when sound is off.
	if(!Settings.get().sound){
		Piezo.noTone();
		return;
	}

	// S146 - suppress the descending sweep while the preamble is
	// holding the plate at full brightness. The user reads the
	// message in silence; once the preamble window elapses the
	// existing exponential sweep takes over from the top of the
	// envelope just like the original S57 timeline.
	if(messagePreambleMs > 0 && elapsedMs < messagePreambleMs){
		Piezo.noTone();
		return;
	}

	// Final tail: Phase 2 silences the descending sweep and emits a
	// single brief click at TONE_CLICK_HZ on the very first frame of
	// the dot fade. The click is short by design - a long buzz here
	// would fight the visual "and then it's off" cue.
	if(progress >= 1.0f){
		Piezo.noTone();
		return;
	}
	if(progress >= PhaseHorizontalEnd){
		// Emit the click exactly once at the boundary, then go silent.
		// We detect "first frame past the boundary" by checking that
		// elapsedMs is within one tick of the boundary timestamp.
		// S146 - the boundary lives in the post-preamble timeline, so
		// add messagePreambleMs to the durationMs-based offset.
		// messagePreambleMs is zero on the legacy (no-message) path so
		// the original S57 timing is preserved exactly when no power-off
		// message is persisted.
		const uint32_t boundaryMs =
			messagePreambleMs +
			(uint32_t) (PhaseHorizontalEnd * (float) durationMs);
		if(elapsedMs >= boundaryMs && elapsedMs < boundaryMs + AnimTickMs){
			Piezo.tone(TONE_CLICK_HZ, AnimTickMs);
		}else{
			Piezo.noTone();
		}
		return;
	}

	// Descending sweep across phases 0 + 1. Exponential curve in
	// frequency space so the perceived pitch drop sounds linear to
	// the human ear (Hz scales geometrically with perceived pitch).
	// We use the running progress over the [0 .. PhaseHorizontalEnd]
	// window so the sweep finishes exactly as the plate becomes a dot.
	const float t = progress / PhaseHorizontalEnd;
	const float ratio = (float) TONE_END_HZ / (float) TONE_START_HZ;
	const float freqF = (float) TONE_START_HZ * powf(ratio, t);
	uint16_t freq = (uint16_t) freqF;
	if(freq < TONE_END_HZ)   freq = TONE_END_HZ;
	if(freq > TONE_START_HZ) freq = TONE_START_HZ;
	Piezo.tone(freq);
}

// ---- completion dispatch ---------------------------------------------------

void PhonePowerDown::fireComplete() {
	// Guard against double-fire: the timer can race with a future
	// "skip" path in a single LVGL tick. Collapsing both to a single
	// dispatch keeps the host's callback from being invoked twice.
	if(firedAlready) return;
	firedAlready = true;

	// Stop the ticker up-front so a slow handler (e.g. one that does
	// heavyweight Sleep.turnOff() work) cannot let it re-enter
	// fireComplete via the static callback.
	stopTicker();
	Piezo.noTone();

	// Stash the handler on the stack first so the `this` access
	// happens before any teardown side effects.
	auto cb = dismissCb;

	if(cb != nullptr){
		// Host callback owns the rest of the lifecycle (typically the
		// real hardware shutdown). We do NOT pop ourselves here -
		// SleepService.turnOff() never returns, so popping first
		// would just briefly redraw whatever we were covering.
		cb();
		return;
	}

	// No host callback: behave like every other transient overlay and
	// pop back to the screen that pushed us. With no parent (someone
	// constructed and start()'ed us as a top-level demo screen) the
	// LVScreen pop becomes a no-op, which is fine.
	pop();
}
