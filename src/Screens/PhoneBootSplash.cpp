#include "PhoneBootSplash.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Fonts/font.h"
#include "../Services/PhoneRingtoneEngine.h"

// ---- MAKERphone retro palette ----------------------------------------------
// Inlined per the established convention in this codebase (see
// PhoneSynthwaveBg / PhoneCallEnded / PhoneActiveCall). Keeping the
// palette local to each screen makes the Phase-A widgets relocatable
// without dragging a shared header along - the trade-off is each new
// screen restating the same handful of constants. The wordmark uses
// warm cream so it reads as the focal element; the tagline uses cyan
// so it pops as a hot-key accent; the sunset is built from deep purple
// at the top of the sky band, magenta at the horizon, and sunset orange
// blending into the ground.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple (zenith)
#define MP_PURPLE_MID  lv_color_make( 70,  40, 110)   // mid purple
#define MP_MAGENTA     lv_color_make(180,  40, 140)   // magenta horizon band
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange (ground)
#define MP_SUN         lv_color_make(255, 200,  90)   // sun fill (hot orange-yellow)
#define MP_HALO        lv_color_make(255, 170,  50)   // sun outline halo
#define MP_HORIZON     lv_color_make(255, 220, 150)   // bright horizon glow line
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream wordmark
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan tagline ("2.0")
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim hint text

// =====================================================================
// S148 - Boot melody (Sony-Ericsson-style four-note chime)
//
// A bright, ascending major-arpeggio chime that plays the moment the
// boot splash appears. Four notes sized to land well inside the 3 s
// splash hold, so the chime always completes naturally before the
// splash auto-dismisses (any earlier user-driven dismiss stops the
// melody via Ringtone.stop() in onStop() below).
//
// Pitch choice: G5 - B5 - D6 - G6 (G-major triad, last note is the
// octave). Mirrors the unmistakable rising "ta-da-da-DAA" cadence the
// late-2000s Sony-Ericsson feature phones used as their startup mark.
// The final note is held longer so the chime feels resolved rather
// than truncated. 30 ms inter-note gap keeps the strikes distinct
// without sounding mechanical.
//
// Volume / mute is delegated entirely to the Ringtone engine: the
// engine already respects Settings.sound (PhoneRingtoneEngine.cpp
// emitTone()) and the Piezo.setMute(!Settings.sound) gate set in
// setup(), so a muted prototype boots silently.
//
// Total duration: 110 + 110 + 110 + 320 + 3*30 ~= 740 ms
// =====================================================================
static const PhoneRingtoneEngine::Note kBootChimeNotes[] = {
	{  784, 110 },  // G5
	{  988, 110 },  // B5
	{ 1175, 110 },  // D6
	{ 1568, 320 },  // G6 (held)
};
static const PhoneRingtoneEngine::Melody kBootChime = {
	kBootChimeNotes,
	(uint16_t)(sizeof(kBootChimeNotes) / sizeof(kBootChimeNotes[0])),
	30,    // gapMs - tiny breath between notes
	false, // play once - boot chime never loops
	"BootChime"
};


PhoneBootSplash::PhoneBootSplash(DismissHandler onDismiss,
								 uint32_t       durationMs)
		: LVScreen(),
		  dismissCb(onDismiss),
		  durationMs(durationMs),
		  dismissedAlready(false),
		  sky(nullptr),
		  ground(nullptr),
		  sun(nullptr),
		  horizonLine(nullptr),
		  wordmark(nullptr),
		  tagline(nullptr),
		  hint(nullptr),
		  dismissTimer(nullptr) {

	// Full-screen container, no scrollbars, zero padding - same blank
	// canvas pattern the other Phone* screens use. Children below are
	// placed manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	// Hard-fill the screen black behind the gradient bands. If a slow
	// flush leaves a single pixel of LVGL's default theme bleed during
	// boot it reads as warm-cream-on-black rather than warm-cream-on-
	// theme-tint, which matches the rest of the MAKERphone shell.
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

	// Build the visual stack from back to front: sky band, ground band,
	// horizon line, half-sun, wordmark, tagline, "press any key" hint.
	// Order matters - LVGL z-orders children by creation order on a
	// non-scrollable parent, so the latest-built objects sit on top.
	buildSky();
	buildGround();
	buildHorizon();
	buildSun();
	buildWordmark();
	buildTagline();
	buildHint();
}

PhoneBootSplash::~PhoneBootSplash() {
	// Cancel the auto-dismiss timer ahead of LVGL teardown so its
	// callback never fires against a freed screen during destruction.
	stopDismissTimer();
	// Every child is parented to obj - LVScreen's destructor frees obj
	// and LVGL recursively tears down their backing storage.
}

// ---- lifecycle -------------------------------------------------------------

void PhoneBootSplash::onStart() {
	// Subscribe to hardware input so any key short-circuits the timer.
	// Every key path (digits, L/R, BACK, ENTER, bumpers) feeds through
	// Input::getInstance() and gets forwarded here via buttonPressed.
	Input::getInstance()->addListener(this);

	// Reset the dismissed flag so a screen reused after a fire (unlikely
	// for a boot splash, but cheap to support and consistent with the
	// rest of the Phone* overlays) starts fresh. Timer is restarted from
	// here too so the 3 s window begins when the splash is actually
	// visible, not when it was constructed.
	dismissedAlready = false;
	startDismissTimer();

	// S148 - launch the boot chime the instant the splash becomes
	// visible. Routed through the global Ringtone engine so it shares
	// the same mute / Settings.sound gating as every other Phone* sound
	// surface. The chime is one-shot (kBootChime.loop == false) and
	// short enough (~740 ms) to finish well inside the 3 s splash hold;
	// onStop() below stops it explicitly to cover any-key early dismiss.
	Ringtone.play(kBootChime);
}

void PhoneBootSplash::onStop() {
	stopDismissTimer();
	Input::getInstance()->removeListener(this);

	// S148 - silence the boot chime if the splash is leaving while the
	// melody is still playing (any-key dismiss before the chime
	// finishes). When the chime has already completed naturally this is
	// a no-op: the engine's loop() advance has already returned to its
	// idle state and Ringtone.stop() short-circuits on !playing.
	Ringtone.stop();
}

// ---- builders --------------------------------------------------------------

void PhoneBootSplash::buildSky() {
	// Upper gradient band: zenith deep purple -> magenta near the horizon.
	// 64 px tall (top half of the screen). LVGL 8.x's bg_grad_color is
	// a 2-stop linear gradient; the trick to fake a 3-stop sunset is to
	// build a second band below this one (see buildGround) that picks
	// up from the magenta and continues into sunset orange.
	sky = lv_obj_create(obj);
	lv_obj_remove_style_all(sky);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sky, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sky, 160, 64);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_bg_color(sky, MP_BG_DARK, 0);
	lv_obj_set_style_bg_grad_color(sky, MP_PURPLE_MID, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
}

void PhoneBootSplash::buildGround() {
	// Lower gradient band: magenta near horizon -> sunset orange at
	// the bottom. Anchored at y=64 to butt up against the sky band so
	// the eye reads a single 3-stop sunset across the whole 128 px.
	ground = lv_obj_create(obj);
	lv_obj_remove_style_all(ground);
	lv_obj_clear_flag(ground, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(ground, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(ground, 160, 64);
	lv_obj_set_pos(ground, 0, 64);
	lv_obj_set_style_radius(ground, 0, 0);
	lv_obj_set_style_bg_color(ground, MP_MAGENTA, 0);
	lv_obj_set_style_bg_grad_color(ground, MP_ACCENT, 0);
	lv_obj_set_style_bg_grad_dir(ground, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
}

void PhoneBootSplash::buildHorizon() {
	// 1-pixel-tall hot-orange line at the seam where the sky and ground
	// gradient bands meet. Reads as the "bright glow on the horizon"
	// you get a few minutes after sunset. Drawn slightly translucent so
	// it blends into the gradient instead of looking like a hard cut.
	horizonLine = lv_obj_create(obj);
	lv_obj_remove_style_all(horizonLine);
	lv_obj_clear_flag(horizonLine, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(horizonLine, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(horizonLine, 160, 1);
	lv_obj_set_pos(horizonLine, 0, 80);
	lv_obj_set_style_radius(horizonLine, 0, 0);
	lv_obj_set_style_bg_color(horizonLine, MP_HORIZON, 0);
	lv_obj_set_style_bg_opa(horizonLine, LV_OPA_70, 0);
}

void PhoneBootSplash::buildSun() {
	// Half-sun rising at the bottom-centre. We anchor a 72x72 disc
	// with its centre below the screen edge so the parent's clip
	// region cuts off the bottom half of the disc, leaving only the
	// "rising" upper half visible. A soft halo outline gives it the
	// trademark synthwave bloom without needing canvas-level masking.
	sun = lv_obj_create(obj);
	lv_obj_remove_style_all(sun);
	lv_obj_clear_flag(sun, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sun, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sun, 72, 72);
	// Centre the 72-wide disc horizontally: x = (160 - 72) / 2 = 44.
	// Centre below the screen so only the upper ~28 px of the disc
	// shows: y = 128 - 28 = 100.
	lv_obj_set_pos(sun, 44, 100);
	lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(sun, MP_SUN, 0);
	lv_obj_set_style_bg_opa(sun, LV_OPA_COVER, 0);
	// Soft halo: a 4-pixel outline of the warmer orange, anti-aliased
	// by LVGL across the circle's perimeter. Drawn at ~60% opacity so
	// it bleeds into the sunset rather than ringing the disc sharply.
	lv_obj_set_style_outline_color(sun, MP_HALO, 0);
	lv_obj_set_style_outline_width(sun, 4, 0);
	lv_obj_set_style_outline_opa(sun, LV_OPA_60, 0);
}

void PhoneBootSplash::buildWordmark() {
	// The MAKERphone wordmark, centered horizontally, anchored ~28 px
	// from the top of the screen. pixelbasic16 (the bigger of the two
	// MAKERphone fonts) in warm cream so it reads as the focal element
	// against the deep purple sky band. The label is intentionally
	// rendered as a single line so a future host that wants to drive
	// a fade-in / shimmer animation can target the whole label without
	// per-glyph gymnastics.
	wordmark = lv_label_create(obj);
	lv_obj_set_style_text_font(wordmark, &pixelbasic16, 0);
	lv_obj_set_style_text_color(wordmark, MP_TEXT, 0);
	lv_label_set_text(wordmark, "MAKERphone");
	lv_obj_set_align(wordmark, LV_ALIGN_TOP_MID);
	lv_obj_set_y(wordmark, 28);
}

void PhoneBootSplash::buildTagline() {
	// The "2.0" tagline, centered under the wordmark. pixelbasic7 cyan
	// (MP_HIGHLIGHT) so the version number reads as a hot-key-style
	// accent rather than competing with the wordmark above. The tagline
	// is rendered with spaces between glyphs ("2 . 0") so it feels
	// retro / mechanical at the small font size, matching the rest of
	// the MAKERphone shell's typographic voice.
	tagline = lv_label_create(obj);
	lv_obj_set_style_text_font(tagline, &pixelbasic7, 0);
	lv_obj_set_style_text_color(tagline, MP_HIGHLIGHT, 0);
	lv_label_set_text(tagline, "2 . 0");
	lv_obj_set_align(tagline, LV_ALIGN_TOP_MID);
	lv_obj_set_y(tagline, 50);
}

void PhoneBootSplash::buildHint() {
	// "press any key" affordance in dim purple, anchored at the bottom
	// so it reads as a quiet helper line under the rising sun. Kept in
	// MP_LABEL_DIM (the same dim purple used by PhoneCallEnded for its
	// equivalent any-key cue) so a user who has seen the splash a
	// thousand times still has a visible escape hatch without the line
	// fighting the wordmark above.
	hint = lv_label_create(obj);
	lv_obj_set_style_text_font(hint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hint, MP_LABEL_DIM, 0);
	lv_label_set_text(hint, "press any key");
	lv_obj_set_align(hint, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(hint, -4);
}

// ---- timer + dismiss dispatch ---------------------------------------------

void PhoneBootSplash::startDismissTimer() {
	// Idempotent: if a previous onStart() left a timer running we reuse
	// it rather than stacking a second one. A 0 ms duration disables
	// the auto-advance entirely (any-key dismiss only) - mostly useful
	// from tests that want to drive the dismiss path manually.
	if(dismissTimer != nullptr) return;
	if(durationMs == 0)         return;
	dismissTimer = lv_timer_create(onDismissTimer, durationMs, this);
}

void PhoneBootSplash::stopDismissTimer() {
	if(dismissTimer == nullptr) return;
	lv_timer_del(dismissTimer);
	dismissTimer = nullptr;
}

void PhoneBootSplash::onDismissTimer(lv_timer_t* timer) {
	auto self = static_cast<PhoneBootSplash*>(timer->user_data);
	if(self == nullptr) return;
	self->fireDismiss();
}

void PhoneBootSplash::fireDismiss() {
	// Guard against double-fire: a hardware key press and the auto-
	// dismiss timer can race within a single LVGL tick. Collapsing both
	// to a single dispatch keeps the host's DismissHandler from being
	// invoked twice (which would push two IntroScreens stacked on top
	// of each other and confuse every subsequent pop()).
	if(dismissedAlready) return;
	dismissedAlready = true;

	// Stop the timer up-front so a slow handler (e.g. one that does
	// heavyweight FSLVGL::loadCache work) can not let it re-enter
	// fireDismiss via the static callback.
	stopDismissTimer();

	// Tear ourselves down BEFORE invoking the host callback. The boot
	// splash is the very first screen of the boot flow - there is no
	// parent to pop back to, and the host callback is going to push
	// the IntroScreen as the new active screen. Mirrors the lifetime
	// pattern IntroScreen itself uses: stop(), free obj, then call the
	// next-stage callback. Stash the handler on the stack first so the
	// `this` access happens before any teardown.
	auto cb = dismissCb;
	stop();
	lv_obj_del(getLvObj());

	if(cb != nullptr) cb();
}

// ---- input -----------------------------------------------------------------

void PhoneBootSplash::buttonPressed(uint i) {
	// Any hardware key skips the splash. We deliberately do not look
	// at the button index here - the user pressing anything signals
	// "I'm done with the splash" and we honour it. Same any-key
	// dismiss pattern PhoneCallEnded uses for its 1.5 s overlay.
	(void) i;
	fireDismiss();
}
