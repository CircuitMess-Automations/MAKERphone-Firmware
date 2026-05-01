#include "PhoneCameraScreen.h"

#include <stdio.h>
#include <Input/Input.h>
#include <Pins.hpp>
#include <Notes.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "PhoneGalleryScreen.h"   // S46: BTN_RIGHT pushes the gallery stub

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneMainMenu.cpp / PhoneHomeScreen.cpp / PhoneMusicPlayer.cpp).
// Cyan owns the "active" elements (mode label, crosshair, brackets), sunset
// orange owns the live REC dot + flash, dim purple owns subdued ticks,
// warm cream sits behind the frame counter caption.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange (REC dot)
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan (frame + crosshair)
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple (edge ticks)
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream (frame counter)
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple sub-caption

// ---------------------------------------------------------------------------
// Shutter sound: a 3-step "click" - high blip, brief silence, lower blip.
// Defined as a static const Melody so we never allocate on capture and the
// pointer the engine retains is valid for the entire firmware lifetime.
// gapMs is 0 because the explicit silence-step gives us all the spacing we
// need without the engine inserting an extra inter-note gap. loop is false
// so the engine reverts to idle the moment the click finishes - exactly
// what we want for a one-shot capture sound. Sound respects Settings.sound
// because the engine itself does (see PhoneRingtoneEngine.cpp).
// ---------------------------------------------------------------------------
static const PhoneRingtoneEngine::Note kShutterNotes[] = {
	{ NOTE_C7,  20 },   // high crisp opener (~2093 Hz, 20 ms)
	{ 0,         8 },   // brief silence
	{ NOTE_F6,  35 },   // lower thunk that sounds like the mirror dropping
};

static const PhoneRingtoneEngine::Melody kShutterMelody = {
	kShutterNotes,
	sizeof(kShutterNotes) / sizeof(kShutterNotes[0]),
	0,        // gapMs - explicit silence step replaces any inter-note gap
	false,    // loop  - one shot only
	"Shutter"
};

// ---------------------------------------------------------------------------
// S45 - mode-cycle "tick". A single very-short blip, deliberately quieter
// than the shutter so spamming the bumpers does not drown out the device.
// Same static-storage discipline as kShutterMelody so cycleMode never
// allocates - the array lives in .rodata for the firmware lifetime.
// ---------------------------------------------------------------------------
static const PhoneRingtoneEngine::Note kModeTickNotes[] = {
	{ NOTE_E6, 18 },   // single quick chirp (~1319 Hz, 18 ms)
};

static const PhoneRingtoneEngine::Melody kModeTickMelody = {
	kModeTickNotes,
	sizeof(kModeTickNotes) / sizeof(kModeTickNotes[0]),
	0,
	false,
	"ModeTick"
};

// ===========================================================================
// Construction / destruction
// ===========================================================================

PhoneCameraScreen::PhoneCameraScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  modeLabel(nullptr),
		  frameLabel(nullptr),
		  recDot(nullptr),
		  flash(nullptr),
		  mode(Mode::Photo),
		  frameCount(0),
		  flashActive(false),
		  clickResetTimer(nullptr) {

	// Full-screen container, no scrollbars, no inner padding - same blank
	// canvas pattern PhoneHomeScreen / PhoneDialerScreen / PhoneMusicPlayer
	// use. Children below either pin themselves with IGNORE_LAYOUT or are
	// LVGL primitives that we anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The
	// viewfinder primitives, status bar, soft-keys and HUD overlay it
	// without any opacity gymnastics on the parent. Matches the pattern
	// the rest of the phone-style screens already use.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall) so the user can
	// always see the device is alive even with the camera open.
	statusBar = new PhoneStatusBar(obj);

	// Centre: viewfinder frame (corner brackets + dotted edge ticks +
	// crosshair + REC dot). Built before the HUD so the HUD labels land
	// on top of any z-fighting siblings.
	buildViewfinder();

	// Below the viewfinder: mode label (left) + frame counter (right).
	buildHud();

	// Top of the z-order (built last): the cyan flash overlay that we
	// toggle visible on shoot() and animate back to transparent.
	buildFlash();

	// Bottom: feature-phone soft-keys. Left = CAPTURE (BTN_ENTER), right
	// = EXIT (BTN_BACK). Same single-action layout PhoneAppStubScreen
	// uses, but with a real action wired up.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("CAPTURE");
	softKeys->setRight("EXIT");

	// Initial state. setMode(mode) seeds the mode-label text + colour
	// AND the REC-dot accent in lock-step so the boot-time appearance
	// matches the cycleMode result. We pass `mode` (the default Photo
	// from the initializer list) rather than hard-coding so future
	// hosts that change `mode` before calling onStart still see a
	// consistent viewfinder on the first frame.
	setMode(mode);
	refreshFrameLabel();
}

PhoneCameraScreen::~PhoneCameraScreen() {
	// Defensive: kill any in-flight click-reset timer + any lingering
	// flash animation so the screen never frees an lv_obj that LVGL
	// still has an animation reference to. The matching teardown also
	// runs in onStop(); covering both paths keeps us safe if the screen
	// is destroyed while never started (host built it, changed its mind
	// before push()).
	if(clickResetTimer != nullptr){
		lv_timer_del(clickResetTimer);
		clickResetTimer = nullptr;
	}
	if(flash != nullptr){
		lv_anim_del(flash, onFlashAnim);
	}
	// We also stop ringtone playback in case the user pops the screen
	// during the (very short) click. The engine guarantees stop() is
	// idempotent so calling it from both ~ and onStop() is safe.
	Ringtone.stop();

	// Children (wallpaper, statusBar, softKeys, viewfinder primitives,
	// labels, flash overlay) are all parented to obj - LVGL frees them
	// recursively when the screen's obj is destroyed by the LVScreen
	// base destructor. Nothing manual.
}

// ===========================================================================
// LVScreen lifecycle
// ===========================================================================

void PhoneCameraScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneCameraScreen::onStop() {
	Input::getInstance()->removeListener(this);

	// Belt-and-braces teardown - leaving the screen always silences the
	// piezo so a click cannot leak a residual tone into the parent
	// screen. Same shape as PhoneMusicPlayer::onStop().
	Ringtone.stop();

	if(clickResetTimer != nullptr){
		lv_timer_del(clickResetTimer);
		clickResetTimer = nullptr;
	}
	if(flash != nullptr){
		lv_anim_del(flash, onFlashAnim);
	}
}

// ===========================================================================
// Builders
// ===========================================================================

lv_obj_t* PhoneCameraScreen::makeRect(lv_coord_t x, lv_coord_t y,
									  lv_coord_t w, lv_coord_t h,
									  lv_color_t c) {
	// Tiny helper: a flat-coloured rectangle parented to the screen, with
	// IGNORE_LAYOUT so we can pin it absolutely. Used heavily by the
	// viewfinder builder where we need a couple of dozen tiny rects to
	// draw brackets + ticks + crosshair. Keeps the call sites short.
	lv_obj_t* r = lv_obj_create(obj);
	lv_obj_remove_style_all(r);
	lv_obj_add_flag(r, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(r, w, h);
	lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(r, c, 0);
	lv_obj_set_style_border_width(r, 0, 0);
	lv_obj_set_style_radius(r, 0, 0);
	lv_obj_set_style_pad_all(r, 0, 0);
	lv_obj_set_pos(r, x, y);
	lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
	return r;
}

void PhoneCameraScreen::buildViewfinder() {
	// Pre-compute the four corners of the viewfinder rectangle once so
	// the bracket / tick offsets below stay readable.
	const lv_coord_t x0 = VfX;
	const lv_coord_t y0 = VfY;
	const lv_coord_t x1 = VfX + VfW - 1;     // inclusive right edge
	const lv_coord_t y1 = VfY + VfH - 1;     // inclusive bottom edge

	const lv_color_t bracketC = MP_HIGHLIGHT;   // cyan corners pop over wallpaper
	const lv_color_t tickC    = MP_LABEL_DIM;   // dim purple ticks recede
	const lv_color_t crossC   = MP_HIGHLIGHT;   // cyan crosshair matches brackets
	const lv_color_t recC     = MP_ACCENT;      // sunset orange "live" dot

	// ----- corner brackets (4 corners x 2 arms each = 8 rects) -----
	// Each bracket is an L-shape: one horizontal arm + one vertical arm,
	// each CornerLen px long and CornerThk px thick. Drawn as two
	// separate rects so the corner pixel is shared cleanly without a
	// custom shape.
	makeRect(x0,                        y0,                        CornerLen, CornerThk, bracketC); // TL horiz
	makeRect(x0,                        y0,                        CornerThk, CornerLen, bracketC); // TL vert
	makeRect(x1 - CornerLen + 1,        y0,                        CornerLen, CornerThk, bracketC); // TR horiz
	makeRect(x1,                        y0,                        CornerThk, CornerLen, bracketC); // TR vert
	makeRect(x0,                        y1,                        CornerLen, CornerThk, bracketC); // BL horiz
	makeRect(x0,                        y1 - CornerLen + 1,        CornerThk, CornerLen, bracketC); // BL vert
	makeRect(x1 - CornerLen + 1,        y1,                        CornerLen, CornerThk, bracketC); // BR horiz
	makeRect(x1,                        y1 - CornerLen + 1,        CornerThk, CornerLen, bracketC); // BR vert

	// ----- dotted edge ticks -----
	// EdgeTicksHoriz dots evenly spaced along the top + bottom edges
	// (between the corner brackets), and EdgeTicksVert dots along each
	// vertical edge. Each dot is 2x1 (horiz) or 1x2 (vert) for a tiny
	// dash look that reads as dotted at the 160x128 display density.
	const lv_coord_t innerLeft   = x0 + CornerLen + 2;   // first dot starts past the bracket arm
	const lv_coord_t innerRight  = x1 - CornerLen - 2;
	const lv_coord_t innerTop    = y0 + CornerLen + 2;
	const lv_coord_t innerBottom = y1 - CornerLen - 2;

	if(EdgeTicksHoriz > 1 && innerRight > innerLeft) {
		const lv_coord_t span = innerRight - innerLeft;
		for(uint8_t i = 0; i < EdgeTicksHoriz; ++i){
			const lv_coord_t tx = innerLeft + (lv_coord_t)((span * i) / (EdgeTicksHoriz - 1));
			makeRect(tx, y0, 2, 1, tickC);   // top edge dash
			makeRect(tx, y1, 2, 1, tickC);   // bottom edge dash
		}
	}
	if(EdgeTicksVert > 1 && innerBottom > innerTop) {
		const lv_coord_t span = innerBottom - innerTop;
		for(uint8_t i = 0; i < EdgeTicksVert; ++i){
			const lv_coord_t ty = innerTop + (lv_coord_t)((span * i) / (EdgeTicksVert - 1));
			makeRect(x0, ty, 1, 2, tickC);   // left edge dash
			makeRect(x1, ty, 1, 2, tickC);   // right edge dash
		}
	}

	// ----- centre crosshair / reticle -----
	// Two thin rects (horizontal + vertical) plus a 2x2 centre dot. The
	// crosshair is offset from the actual centre so the centre dot sits
	// exactly on the middle pixel for a 132x78 viewfinder.
	const lv_coord_t cx = VfX + VfW / 2;
	const lv_coord_t cy = VfY + VfH / 2;
	makeRect(cx - CrossArm,     cy,             CrossArm,         1, crossC);  // left arm
	makeRect(cx + 1,            cy,             CrossArm,         1, crossC);  // right arm
	makeRect(cx,                cy - CrossArm,  1,         CrossArm, crossC);  // top arm
	makeRect(cx,                cy + 1,         1,         CrossArm, crossC);  // bottom arm
	makeRect(cx - 1,            cy - 1,         2,                2, crossC);  // centre dot

	// ----- "live" REC indicator -----
	// Small 3x3 dot just inside the top-left bracket so the user reads
	// "this viewfinder is live" the moment they look at it. Positioned
	// to not overlap the bracket arms - 4 px in from each edge gives
	// clear separation. Initial colour is the Photo accent (sunset
	// orange); S45 retints it on every mode cycle through setMode().
	recDot = makeRect(x0 + CornerLen + 4, y0 + 3, 3, 3, recC);
}

void PhoneCameraScreen::buildHud() {
	// Mode label - left-anchored under the viewfinder, cyan + pixelbasic7.
	modeLabel = lv_label_create(obj);
	lv_obj_add_flag(modeLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(modeLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(modeLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(modeLabel, "PHOTO");
	lv_obj_set_pos(modeLabel, VfX, HudY);

	// Frame counter - right-anchored under the viewfinder, warm cream +
	// pixelbasic7. We pre-size the label to a fixed width so the right
	// edge stays put even when the count grows from "0/24" to "12/24".
	frameLabel = lv_label_create(obj);
	lv_obj_add_flag(frameLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(frameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(frameLabel, MP_TEXT, 0);
	lv_label_set_long_mode(frameLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(frameLabel, 36);
	lv_obj_set_style_text_align(frameLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(frameLabel, "0/24");
	// 36 px wide label, right edge at VfX + VfW (== 14 + 132 == 146).
	lv_obj_set_pos(frameLabel, VfX + VfW - 36, HudY);
}

void PhoneCameraScreen::buildFlash() {
	// Cyan rectangle covering the entire viewfinder. We make it
	// transparent at idle and crank it to LV_OPA_COVER on shoot(); an
	// LVGL animation then ramps the opacity back to 0 over FlashFadeMs
	// ms for the classic "snap" feel.
	flash = makeRect(VfX, VfY, VfW, VfH, MP_HIGHLIGHT);
	// Border-radius zero already from makeRect(). Start invisible.
	lv_obj_set_style_bg_opa(flash, LV_OPA_TRANSP, 0);
	lv_obj_add_flag(flash, LV_OBJ_FLAG_HIDDEN);
}

// ===========================================================================
// Public API
// ===========================================================================

void PhoneCameraScreen::setMode(Mode m) {
	mode = m;
	refreshModeLabel();
	// S45: REC dot + mode label share the same per-mode accent so the
	// active mode is unambiguous even at a glance. Guard recDot because
	// setMode may legitimately be called before buildViewfinder()
	// finishes if a host wires it up early - the constructor calls
	// refreshModeLabel/refreshFrameLabel directly so this guard mirrors
	// the existing nullptr-tolerant style of those helpers.
	const lv_color_t accent = modeAccent(m);
	if(recDot != nullptr){
		lv_obj_set_style_bg_color(recDot, accent, 0);
	}
	if(modeLabel != nullptr){
		lv_obj_set_style_text_color(modeLabel, accent, 0);
	}
}

void PhoneCameraScreen::cycleMode(int8_t dir) {
	// Wrap Photo <-> Effect <-> Selfie in either direction. We compute
	// modulo 3 by hand because C++'s % on a negative left operand is
	// implementation-defined for the sign of the result up through
	// C++03; explicit branch keeps us portable across whatever Arduino
	// core the toolchain pins. Non +/-1 dirs are normalised to +1 so
	// callers cannot accidentally produce huge jumps.
	const uint8_t modeCount = 3;
	const uint8_t cur = (uint8_t)mode;
	uint8_t next;
	if(dir < 0){
		next = (cur == 0) ? (modeCount - 1) : (cur - 1);
	}else{
		next = (cur + 1) % modeCount;
	}
	setMode((Mode)next);

	// Audio feedback for the bumper press. Engine is non-blocking and
	// idempotent on play(), so back-to-back cycles never stack.
	playModeTickSound();
}

lv_color_t PhoneCameraScreen::modeAccent(Mode m) {
	// Cyan for Photo (matches the crosshair, the default "feels neutral"
	// look), sunset orange for Effect (warm/saturated, hints at the
	// future filter pipeline), warm cream for Selfie (softer, reads
	// closer to skin tones at the 160x128 density). All three are
	// already part of the shared MAKERphone palette so we never invent
	// new colours, and the recDot/modeLabel pair always pull from the
	// same source so they stay visually locked together.
	switch(m){
		case Mode::Photo:  return MP_HIGHLIGHT;   // cyan
		case Mode::Effect: return MP_ACCENT;      // sunset orange
		case Mode::Selfie: return MP_TEXT;        // warm cream
		default:           return MP_HIGHLIGHT;
	}
}

void PhoneCameraScreen::shoot() {
	// Increment frame counter (capped at FrameBudget so the caption
	// never reads "25/24"). When we hit the cap we still play the
	// flash + click - the mode label could later swap to "FULL" once
	// S46 wires up persistent storage; for S44 we stop short of that.
	if(frameCount < FrameBudget){
		frameCount++;
	}
	refreshFrameLabel();

	// Flash overlay: jump to fully opaque, then animate the bg_opa
	// property back down to zero. We use an LVGL anim with a custom
	// exec callback because lv_obj_set_style_bg_opa() takes (target,
	// opa, selector) and lv_anim drives the (target, value) pair.
	if(flash != nullptr){
		lv_anim_del(flash, onFlashAnim);   // kill any in-flight fade

		lv_obj_clear_flag(flash, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_bg_opa(flash, LV_OPA_COVER, 0);
		flashActive = true;

		lv_anim_t a;
		lv_anim_init(&a);
		lv_anim_set_var(&a, flash);
		lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
		lv_anim_set_time(&a, FlashFadeMs);
		lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
		lv_anim_set_exec_cb(&a, onFlashAnim);
		lv_anim_start(&a);
	}

	// Soft-key caption flicks "CAPTURE" -> "SAVED" for the duration of
	// the flash so the user gets explicit feedback that the simulated
	// photo was committed. A one-shot lv_timer flips it back. We tear
	// down any previous reset timer so spamming the shutter never
	// stacks timers.
	if(softKeys != nullptr){
		softKeys->setLeft("SAVED");
		if(clickResetTimer != nullptr){
			lv_timer_del(clickResetTimer);
			clickResetTimer = nullptr;
		}
		// Slightly longer than the flash so the cyan flash and the
		// "SAVED" caption don't both finish at the exact same frame -
		// stagger reads better.
		clickResetTimer = lv_timer_create(onClickResetTick, FlashFadeMs + 80, this);
		lv_timer_set_repeat_count(clickResetTimer, 1);
	}

	// Audio LAST so the shutter click overlaps the visual flash rather
	// than preceding it. The Ringtone engine is non-blocking so the
	// call returns immediately.
	playShutterSound();
}

// ===========================================================================
// Helpers
// ===========================================================================

void PhoneCameraScreen::refreshModeLabel() {
	if(modeLabel == nullptr) return;
	const char* text;
	switch(mode){
		case Mode::Photo:  text = "PHOTO";  break;
		case Mode::Effect: text = "EFFECT"; break;
		case Mode::Selfie: text = "SELFIE"; break;
		default:           text = "PHOTO";  break;
	}
	lv_label_set_text(modeLabel, text);
}

void PhoneCameraScreen::refreshFrameLabel() {
	if(frameLabel == nullptr) return;
	char buf[12];
	snprintf(buf, sizeof(buf), "%u/%u",
			 (unsigned)frameCount, (unsigned)FrameBudget);
	lv_label_set_text(frameLabel, buf);
}

void PhoneCameraScreen::playShutterSound() {
	// Hand the static const Melody to the global engine. The engine
	// retains the pointer until playback finishes (or is stopped); the
	// Melody lives in .rodata so the lifetime is the whole firmware.
	// Calling play() on top of an existing playback simply replaces it,
	// which is exactly what we want for back-to-back shutter taps.
	Ringtone.play(kShutterMelody);
}

void PhoneCameraScreen::playModeTickSound() {
	// One-shot bumper tick. Same delivery path as the shutter sound -
	// drop the static const Melody on the engine and return. Sound is
	// gated by Settings.sound at the engine layer so a muted device
	// stays silent on bumper presses too.
	Ringtone.play(kModeTickMelody);
}

void PhoneCameraScreen::onFlashAnim(void* var, int32_t v) {
	// Static animation exec - matches the file-scope helpers in
	// PhoneIncomingCall and PhoneIconTile. var is the flash overlay
	// lv_obj_t*; v is the current opacity in the [0..255] range LVGL
	// hands us via lv_anim_set_values().
	auto target = static_cast<lv_obj_t*>(var);
	if(target == nullptr) return;
	lv_obj_set_style_bg_opa(target, (lv_opa_t)v, 0);
	if(v <= LV_OPA_TRANSP){
		lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneCameraScreen::onClickResetTick(lv_timer_t* t) {
	if(t == nullptr) return;
	auto* self = static_cast<PhoneCameraScreen*>(t->user_data);
	if(self == nullptr){
		lv_timer_del(t);
		return;
	}
	if(self->softKeys != nullptr){
		self->softKeys->setLeft("CAPTURE");
	}
	self->flashActive      = false;
	self->clickResetTimer  = nullptr;
	lv_timer_del(t);
}

// ===========================================================================
// Input
// ===========================================================================

void PhoneCameraScreen::buttonPressed(uint i) {
	switch(i){
		case BTN_ENTER:
			// The single big "shutter" action. Same key the rest of
			// the phone uses for primary confirmation, so the camera
			// stays consistent with the device-wide muscle memory.
			shoot();
			break;
		case BTN_BACK:
			// Leave the camera. onStop() will silence the engine and
			// tear down the click-reset timer.
			pop();
			break;
		case BTN_L:
			// S45: previous mode (Photo <- Effect <- Selfie, wraps).
			cycleMode(-1);
			break;
		case BTN_R:
			// S45: next mode (Photo -> Effect -> Selfie, wraps).
			cycleMode(+1);
			break;
		case BTN_RIGHT:
			// S46: d-pad DOWN opens the gallery stub. The d-pad
			// vertical axis was previously ignored on this screen
			// (mode cycling lives on the shoulder bumpers BTN_L/BTN_R),
			// so this is purely additive - shutter, mode and exit
			// still react identically. push() reparents the gallery
			// under us so its BTN_BACK lands the user back on the
			// viewfinder with the current mode/frameCount intact.
			push(new PhoneGalleryScreen());
			break;
		default:
			// Any other key (digits, BTN_LEFT) is intentionally
			// ignored - the dialer-pad muscle memory does not apply
			// inside the camera viewfinder. (BTN_RIGHT is now wired
			// up to the gallery stub above.)
			break;
	}
}
