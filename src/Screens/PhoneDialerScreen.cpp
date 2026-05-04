#include "PhoneDialerScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdint.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneDialerPad.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

#include "PhoneImeiRevealScreen.h"
#include "PhoneFirmwareInfoScreen.h"
#include "PhoneFlashlight.h"
#include "PhoneFortuneCookie.h"

// S173 - "S-N-A-K-E" Easter egg launches the Snake game directly from
// the dialer when the user types 76253 (S=7, N=6, A=2, K=5, E=3 on a
// classic feature-phone keypad). The launch sequence mirrors the
// engine-style path PhoneGamesScreen uses for case 2 (Snake), so we
// pull in the same three dependencies it does:
//   Loop/LoopManager.h  - defer the launch out of the LVGL event chain
//   FSLVGL              - drop the resource cache before the game starts
//   Games/Snake/Snake.h - the game class itself
#include <Loop/LoopManager.h>
#include "../FSLVGL.h"
#include "../Games/Snake/Snake.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneMainMenu.cpp / PhoneHomeScreen.cpp / PhoneAppStubScreen.cpp).
// Keeping the typed digits in cyan and the empty-buffer hint in dim
// purple matches the rest of the phone-style screens, so a stray newcomer
// dialer feels visually at home next to home / menu.
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple backdrop
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple border
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan typed digits
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple placeholder
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream wisdom

// S172 - class-static day-index of the last time the dialer popped its
// fortune-of-the-day strip. UINT32_MAX is the "never shown" sentinel so
// the very first open after a fresh boot greets the user; subsequent
// opens on the SAME wall-clock day stay quiet (the user only wanted the
// once-a-day ritual, not a per-tap pop-up). The static storage class is
// deliberate: PhoneDialerScreen is allocated/destroyed per push, but the
// "have I shown today's fortune yet" answer lives at the firmware level.
uint32_t PhoneDialerScreen::s_fortuneLastDayIdx = UINT32_MAX;

PhoneDialerScreen::PhoneDialerScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  pad(nullptr),
		  bufferLabel(nullptr),
		  hintLabel(nullptr) {

	// Zero the buffer up front so getBuffer() returns a valid c-string
	// even before the user has typed anything (an empty handler call site
	// would otherwise see uninitialised memory).
	buffer[0] = '\0';

	// Full-screen container, no scrollbars, no inner padding - same blank
	// canvas pattern PhoneHomeScreen / PhoneMainMenu use. Children below
	// either pin themselves with IGNORE_LAYOUT or are LVGL primitives that
	// we anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The pad,
	// status bar, soft-keys and buffer label all overlay it without any
	// opacity gymnastics on the parent. Same z-order pattern as every
	// other Phone* screen.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	// Buffer label + placeholder hint between the status bar and the pad.
	buildBufferLabel();

	// Centerpiece: the 3x4 numpad atom (S10). Anchored centred horizontally
	// and just under the buffer label.
	buildPad();

	// Bottom: feature-phone soft-keys. CALL on the left is the Phase-D
	// entry point (S24-S28 add the actual call screens behind it). BACK
	// on the right is the standard back-out softkey - a *short* press
	// also doubles as backspace on the buffer (see buttonReleased), and a
	// long-press exits the dialer back to whichever screen pushed us.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("CALL");
	softKeys->setRight("BACK");

	// Wire the pad's onPress so direct numpad presses (BTN_0..BTN_9 +
	// '*' and '#') append to the buffer with the same visual flash as
	// arrow-key navigation. The pad itself takes care of the press
	// flash + cursor movement; the screen only owns the buffer state.
	pad->setOnPress([this](char glyph, const char* /*letters*/) {
		this->appendGlyph(glyph);
	});

	// Long-press detection on BTN_BACK so a hold exits the dialer (and a
	// short press is interpreted as backspace). Same 600 ms threshold as
	// the rest of the MAKERphone shell, so the gesture feels identical
	// from any screen.
	setButtonHoldTime(BTN_BACK, 600);

	// S167 - hold BTN_5 to launch the PhoneFlashlight (S134) without
	// having to dive through the utility-apps grid. Same 600 ms hold
	// threshold every other long-press in the shell uses (BTN_BACK
	// here, BTN_0 / BTN_1..BTN_9 on PhoneHomeScreen for speed-dial),
	// so the gesture feels identical from any screen. The short-press
	// path still routes BTN_5 to pad->pressGlyph('5') the moment the
	// user taps the key, so normal dialling latency is unaffected;
	// see buttonHeld(BTN_5) for the rollback + flashlight push.
	setButtonHoldTime(BTN_5, 600);

	// S172 - build the (initially-hidden) fortune-of-the-day overlay
	// strip. Lives on top of the keypad in z-order so a `show...` call
	// in onStart() can pop it without re-creating LVGL objects on every
	// dialer entry. Hidden via LV_OBJ_FLAG_HIDDEN until the day-index
	// check in onStart() decides this is a fresh wall-clock day.
	buildFortuneOverlay();
}

PhoneDialerScreen::~PhoneDialerScreen() {
	// S172 - belt-and-braces cleanup: kill the auto-dismiss timer if
	// the screen is being torn down while the overlay is still up
	// (e.g. a programmatic pop fired while the fortune was visible).
	// The lv_obj_t children themselves are parented to `obj` and are
	// freed by LVGL when the LVScreen base destructor runs.
	if(fortuneTimer != nullptr) {
		lv_timer_del(fortuneTimer);
		fortuneTimer = nullptr;
	}

	// Children (wallpaper, statusBar, softKeys, pad, labels) are all
	// parented to obj - LVGL frees them recursively when the screen's
	// obj is destroyed by the LVScreen base destructor. Nothing manual.
}

void PhoneDialerScreen::onStart() {
	Input::getInstance()->addListener(this);

	// S172 - "Daily fortune in dialer (first open per day)".
	//
	// Compute today's day-index from the wall clock. PhoneClock::nowEpoch
	// returns seconds-since-epoch, so dividing by 86400 yields a stable
	// day counter that rolls forward every midnight. We compare against
	// the class-static s_fortuneLastDayIdx (UINT32_MAX initially) and
	// only show the fortune when the value has actually changed - so
	// re-entering the dialer multiple times on the same day stays quiet,
	// while the first open after midnight always greets the user.
	const uint32_t dayIdx = PhoneClock::nowEpoch() / 86400UL;
	if(dayIdx != s_fortuneLastDayIdx) {
		s_fortuneLastDayIdx = dayIdx;
		showDailyFortune();
	}
}

void PhoneDialerScreen::onStop() {
	Input::getInstance()->removeListener(this);

	// S172 - hide the fortune-of-the-day overlay and kill its
	// auto-dismiss timer the moment the dialer is torn off the
	// stack. This keeps a stale timer pointer from pinging an
	// already-popped screen if the user navigates away while the
	// strip is still up. Idempotent and cheap on the hot path
	// (the typical exit path has the overlay hidden already).
	hideDailyFortune();
}

// ----- builders -----

void PhoneDialerScreen::buildBufferLabel() {
	// Big typed digits in pixelbasic16 - the focal point of the screen
	// when the user is dialling. Centred horizontally, sitting directly
	// under the 10 px status bar. Right-aligned text within an 80 px wide
	// label so a long number scrolls in from the right (matching the
	// classic Sony-Ericsson dialer behaviour where the most recently
	// typed digit is always visible at the right edge).
	bufferLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(bufferLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(bufferLabel, MP_HIGHLIGHT, 0);
	lv_label_set_long_mode(bufferLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(bufferLabel, 140);
	lv_obj_set_style_text_align(bufferLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(bufferLabel, "");
	lv_obj_set_align(bufferLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(bufferLabel, 12);

	// Placeholder hint that sits in the same slot when the buffer is
	// empty. pixelbasic7 + dim purple so it reads as "secondary" - we
	// only show one of (bufferLabel, hintLabel) at a time. The hint
	// gives the user a clear "type a number" prompt the moment the
	// dialer opens; refreshBufferLabel() flips the visibility.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "ENTER NUMBER");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 18);
}

void PhoneDialerScreen::buildPad() {
	pad = new PhoneDialerPad(obj);

	// Anchor centred horizontally and pin the top edge directly under
	// the buffer label. Pad geometry (114 wide, 88 tall) leaves a 2 px
	// gap above the soft-key bar (118 - (28 + 88) = 2 px) and a 23 px
	// margin on each horizontal side.
	lv_obj_set_align(pad->getLvObj(), LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(pad->getLvObj(), (160 - PhoneDialerPad::PadWidth) / 2, 28);
}

// ----- buffer maintenance -----

void PhoneDialerScreen::appendGlyph(char c) {
	// Only digits + '*' + '#' are accepted. The pad keymap is already
	// constrained to that set, but we double-check defensively so a
	// future caller-driven append (e.g. setBuffer) can route through
	// here if desired.
	const bool ok = (c >= '0' && c <= '9') || c == '*' || c == '#';
	if(!ok) return;

	// Hard cap to MaxDigits so the buffer cannot grow off-screen. We
	// silently drop further presses instead of beeping; a future
	// session can wire a buzzer chirp here if the UX needs it.
	if(bufferLen >= MaxDigits) return;

	buffer[bufferLen++] = c;
	buffer[bufferLen]   = '\0';
	refreshBufferLabel();

	// S164 - magic-code Easter egg. The classic GSM IMEI check is
	// `*#06#`; on a real Sony-Ericsson handset typing those five
	// glyphs immediately reveals the device's serial number rather
	// than dialling them. We mirror the gesture here: if the buffer
	// (after this append) exactly matches the code, clear the buffer
	// so the user lands back on a fresh dialer when they pop, and
	// push the PhoneImeiRevealScreen overlay. The detection is on
	// exact match so a buffer that happens to *contain* the code as
	// a suffix (e.g. mid-edit before a backspace) does not fire -
	// the Sony-Ericsson behaviour was equally strict and that is
	// what sells the joke.
	if(bufferLen == 5 && buffer[0] == '*' && buffer[1] == '#'
			&& buffer[2] == '0' && buffer[3] == '6' && buffer[4] == '#') {
		clearBuffer();
		auto* reveal = new PhoneImeiRevealScreen();
		this->push(reveal);
	}

	// S165 - second magic-code Easter egg. The classic Sony-Ericsson
	// service code `*#0000#` opens a tiny phone-information page (model,
	// build date, hardware rev). We mirror the gesture here exactly the
	// same way as `*#06#` above: if the buffer (after this append)
	// matches the seven-glyph code, clear the buffer so the user lands
	// on a fresh dialer when they pop, and push the FW-info reveal.
	// Detection is on exact match for the same reason `*#06#` is
	// strict -- a buffer that merely contains the code as a suffix
	// mid-edit (e.g. before a backspace) does not fire, which is
	// what the original handsets did and what sells the joke.
	if(bufferLen == 7 && buffer[0] == '*' && buffer[1] == '#'
			&& buffer[2] == '0' && buffer[3] == '0'
			&& buffer[4] == '0' && buffer[5] == '0'
			&& buffer[6] == '#') {
		clearBuffer();
		auto* info = new PhoneFirmwareInfoScreen();
		this->push(info);
	}

	// S173 - "S-N-A-K-E" Easter egg. Typing the keypad sequence that
	// spells SNAKE (S=7, N=6, A=2, K=5, E=3 on a classic feature-phone
	// keypad) instantly launches the Snake game, no detour through
	// the games grid required. Detection is on exact match (the
	// whole buffer is "76253") for the same reason *#06# above is
	// strict: a buffer that merely contains the code as a suffix
	// mid-edit (e.g. before a backspace) does not fire, which keeps
	// the gesture deliberate. clearBuffer() runs *before* the actual
	// engine launch so the user lands on a clean dialer when Snake
	// pops them back here on exit.
	if(bufferLen == 5 && buffer[0] == '7' && buffer[1] == '6'
			&& buffer[2] == '2' && buffer[3] == '5' && buffer[4] == '3') {
		clearBuffer();
		launchSnakeShortcut();
	}
}

void PhoneDialerScreen::backspace() {
	if(bufferLen == 0) return;
	bufferLen--;
	buffer[bufferLen] = '\0';
	refreshBufferLabel();
}

void PhoneDialerScreen::refreshBufferLabel() {
	// Show the placeholder hint only while the buffer is empty - the moment
	// the user types anything, the hint hides and the typed digits take its
	// place. We use lv_obj_add_flag(LV_OBJ_FLAG_HIDDEN) rather than
	// lv_obj_clear_flag because LVGL hides the object outright (no layout
	// gymnastics) which is the cleanest fit for this single-slot toggle.
	if(bufferLen == 0) {
		lv_label_set_text(bufferLabel, "");
		if(hintLabel)   lv_obj_clear_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_label_set_text(bufferLabel, buffer);
		if(hintLabel)   lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

// ----- public API -----

void PhoneDialerScreen::setOnCall(SoftKeyHandler cb) {
	callCb = cb;
}

void PhoneDialerScreen::setOnExit(SoftKeyHandler cb) {
	exitCb = cb;
}

void PhoneDialerScreen::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneDialerScreen::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneDialerScreen::setBuffer(const char* text) {
	bufferLen = 0;
	buffer[0] = '\0';
	if(text != nullptr) {
		while(*text != '\0' && bufferLen < MaxDigits) {
			const char c = *text++;
			const bool ok = (c >= '0' && c <= '9') || c == '*' || c == '#';
			if(ok) {
				buffer[bufferLen++] = c;
			}
		}
		buffer[bufferLen] = '\0';
	}
	refreshBufferLabel();
}

void PhoneDialerScreen::clearBuffer() {
	bufferLen = 0;
	buffer[0] = '\0';
	refreshBufferLabel();
}

void PhoneDialerScreen::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneDialerScreen::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}


// ----- S167 helpers -----

void PhoneDialerScreen::launchFlashlight() {
	// Roll back any digits the buffer accumulated since BTN_5 was first
	// pressed. In practice this is exactly the speculative '5' the
	// short-press handler appended, but key-repeat or a stuck shift
	// register could have queued more presses while the user was still
	// holding the key, so we loop until we are back to the snapshot.
	// backspace() is a NO-OP on an empty buffer, so the worst-case
	// edge (buffer was already empty and the speculative append also
	// failed because of MaxDigits) is harmless.
	while(bufferLen > fivePreBufferLen) {
		backspace();
	}

	// Tactile feedback: flash the left soft-key the same way the CALL
	// button flashes, so the user gets a click cue at the moment of
	// hand-off to the flashlight screen. The flashlight's own ctor
	// will repaint the chrome on the next frame.
	if(softKeys) softKeys->flashLeft();

	// Push the flashlight on top of the dialer. PhoneFlashlight is
	// fully self-contained: it snapshots screenBrightness on entry,
	// restores it on exit, and pops itself when the user presses
	// BTN_BACK / BTN_R / BTN_0. The dialer's buffer / cursor state
	// is preserved underneath so the user lands back on whatever they
	// were typing the moment the torch is dismissed.
	auto* flashlight = new PhoneFlashlight();
	this->push(flashlight);
}

// ----- S173 SNAKE Easter-egg launcher -----

void PhoneDialerScreen::launchSnakeShortcut() {
	// Mirror PhoneGamesScreen's engine-launch sequence for case 2
	// (Snake) so the typed-cheat-code path and the games-grid path
	// stay byte-for-byte equivalent. The host is the dialer itself:
	// when Snake's GameEngine tears the game down it calls
	// `host->start()`, which restarts this PhoneDialerScreen with
	// the same buffer/cursor state we left it in (we have already
	// called clearBuffer() at the call site, so the user lands on
	// a fresh dialer). All three deferred steps are necessary:
	//   - stop() detaches us from input/loop so the running
	//     dialer cannot fight the game for the keypad.
	//   - LoopManager::defer pushes the actual game construction
	//     to the next loop tick, clear of the LVGL event chain
	//     that fired the buffer-match in the first place.
	//   - FSLVGL::unloadCache frees LVGL's image cache before
	//     Snake's renderer takes over the framebuffer.
	auto* host = this;
	stop();
	LoopManager::defer([host](uint32_t /*dt*/) {
		FSLVGL::unloadCache();
		// Snake has no splash blit in PhoneGamesScreen's engine path
		// (Games[2].splash == nullptr), so we skip the splash-render
		// + 2 s wait that the SPACE entry uses. The S173 Easter-egg
		// gesture is supposed to feel *instant* anyway - a 2 s splash
		// would steal the punchline.
		Game* game = new Snake::Snake(host);
		if(game == nullptr) return;
		game->load();
		while(!game->isLoaded()) {
			delay(1);
		}
		game->start();
	});
}

// ----- S172 fortune-of-the-day overlay -----

void PhoneDialerScreen::buildFortuneOverlay() {
	// 144x64 backdrop strip centred on the 160x128 display, anchored
	// just under the digit-buffer label (y=30) so the longest fortune
	// (~50 chars wraps to two lines at pixelbasic7) reads without
	// crowding the soft-key bar at y=118. Background is a near-opaque
	// dark purple - the keypad behind shows through faintly so the
	// overlay reads as a "the dialer is still here, this is just a
	// greeting" rather than a hard takeover. 1 px cyan border ties
	// it visually to the typed-digit cyan above.
	fortuneOverlay = lv_obj_create(obj);
	lv_obj_remove_style_all(fortuneOverlay);
	lv_obj_set_size(fortuneOverlay, 144, 64);
	lv_obj_set_align(fortuneOverlay, LV_ALIGN_TOP_MID);
	lv_obj_set_y(fortuneOverlay, 30);
	lv_obj_set_style_bg_color(fortuneOverlay, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(fortuneOverlay, 235, 0);
	lv_obj_set_style_border_color(fortuneOverlay, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(fortuneOverlay, 1, 0);
	lv_obj_set_style_border_opa(fortuneOverlay, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(fortuneOverlay, 2, 0);
	lv_obj_set_style_pad_all(fortuneOverlay, 4, 0);
	lv_obj_set_scrollbar_mode(fortuneOverlay, LV_SCROLLBAR_MODE_OFF);

	// Hidden + non-interactive in z-stack until showDailyFortune flips
	// the visibility flag. CLICKABLE off so a stray finger event the
	// host might dispatch (no real touch input on Chatter, but defensive)
	// cannot eat focus from the keypad behind.
	lv_obj_add_flag(fortuneOverlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(fortuneOverlay, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(fortuneOverlay, LV_OBJ_FLAG_SCROLLABLE);

	// "FORTUNE OF THE DAY" caption strip in cyan pixelbasic7 - same
	// visual language as the typed-digit highlight. Anchored to the
	// top of the overlay so the wisdom underneath has the maximum
	// available vertical real estate to wrap into.
	fortuneCaption = lv_label_create(fortuneOverlay);
	lv_obj_set_style_text_font(fortuneCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(fortuneCaption, MP_HIGHLIGHT, 0);
	lv_label_set_text(fortuneCaption, "FORTUNE OF THE DAY");
	lv_obj_set_align(fortuneCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(fortuneCaption, 0);

	// Wisdom body in cream pixelbasic7, centred and word-wrapped to
	// the overlay's interior width (144 - 2*4 padding = 136 px).
	// LV_LABEL_LONG_WRAP + a fixed width is the only reliable way
	// to get LVGL 8.x to wrap a label into multiple visual rows.
	fortuneText = lv_label_create(fortuneOverlay);
	lv_obj_set_style_text_font(fortuneText, &pixelbasic7, 0);
	lv_obj_set_style_text_color(fortuneText, MP_TEXT, 0);
	lv_obj_set_style_text_align(fortuneText, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(fortuneText, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(fortuneText, 132);
	lv_label_set_text(fortuneText, "");
	lv_obj_set_align(fortuneText, LV_ALIGN_TOP_MID);
	lv_obj_set_y(fortuneText, 12);

	// Bottom-of-strip dismiss hint in dim purple pixelbasic7 so the
	// user knows the strip is transient. Centred just inside the
	// bottom edge of the overlay.
	fortuneFooter = lv_label_create(fortuneOverlay);
	lv_obj_set_style_text_font(fortuneFooter, &pixelbasic7, 0);
	lv_obj_set_style_text_color(fortuneFooter, MP_LABEL_DIM, 0);
	lv_label_set_text(fortuneFooter, "ANY KEY TO DISMISS");
	lv_obj_set_align(fortuneFooter, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(fortuneFooter, 0);
}

void PhoneDialerScreen::showDailyFortune() {
	if(fortuneOverlay == nullptr) return;
	if(fortuneVisible) return;   // already up - no-op (don't reset timer)

	// Pull the day's wisdom from the same 32-entry rotation
	// PhoneFortuneCookie (S133) uses, so the dialer greeting and the
	// fortune-cookie utility tell the user the same story on any
	// given day. The dayIndex math here mirrors the one in onStart()
	// to keep the two call sites trivially auditable.
	const uint32_t dayIdx     = PhoneClock::nowEpoch() / 86400UL;
	const uint8_t  fortuneIdx = PhoneFortuneCookie::fortuneOfDay(dayIdx);
	const char*    text       = PhoneFortuneCookie::fortuneAt(fortuneIdx);
	if(text == nullptr) text  = "HAVE A BEAUTIFUL DAY.";

	if(fortuneText) lv_label_set_text(fortuneText, text);

	// Reveal the strip. Bring it to the front of the z-stack so it
	// sits cleanly above the keypad even if a future intermediate
	// element (e.g. an Easter-egg overlay) ever lands between the
	// pad and the screen container.
	lv_obj_clear_flag(fortuneOverlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(fortuneOverlay);
	fortuneVisible = true;

	// One-shot auto-dismiss timer. lv_timer_set_repeat_count(t, 1)
	// turns the recurring lv_timer_create into a fire-once that
	// LVGL prunes for us, so we don't have to track the lifecycle
	// from the dismiss callback. We still null-out fortuneTimer
	// inside hideDailyFortune so the dtor / onStop path is idempotent
	// regardless of whether the timer ran or was pre-empted.
	if(fortuneTimer != nullptr) {
		lv_timer_del(fortuneTimer);
		fortuneTimer = nullptr;
	}
	fortuneTimer = lv_timer_create(&PhoneDialerScreen::onFortuneAutoDismissStatic,
	                               FortuneAutoDismissMs, this);
	lv_timer_set_repeat_count(fortuneTimer, 1);
}

void PhoneDialerScreen::hideDailyFortune() {
	// Idempotent. Both the auto-dismiss timer and the user's any-key
	// gesture funnel through here, and so does onStop(), so we have
	// to handle the "already hidden" case cleanly. lv_timer_del on
	// a nullptr is undefined - the explicit check is mandatory.
	if(fortuneTimer != nullptr) {
		lv_timer_del(fortuneTimer);
		fortuneTimer = nullptr;
	}
	if(fortuneOverlay != nullptr) {
		lv_obj_add_flag(fortuneOverlay, LV_OBJ_FLAG_HIDDEN);
	}
	fortuneVisible = false;
}

void PhoneDialerScreen::onFortuneAutoDismissStatic(lv_timer_t* timer) {
	if(timer == nullptr) return;
	auto* self = static_cast<PhoneDialerScreen*>(timer->user_data);
	if(self == nullptr) return;

	// Drop the local pointer first so hideDailyFortune does not also
	// try to lv_timer_del a timer that LVGL is already tearing down
	// for us via the repeat_count == 1 contract.
	self->fortuneTimer = nullptr;
	self->hideDailyFortune();
}

// ----- input -----

void PhoneDialerScreen::buttonPressed(uint i) {
	// S172 - any key dismisses the fortune-of-the-day strip the moment
	// the user touches the keypad. The press itself still falls through
	// the switch below and is processed normally, so a user who opens
	// the dialer and immediately starts typing does not lose the first
	// digit. We deliberately do NOT consume the event - the greeting
	// is opportunistic, not modal.
	if(fortuneVisible) {
		hideDailyFortune();
	}

	switch(i) {
		// Direct numpad input - route through the pad so the user sees
		// the same press-flash + cursor-jump as if they had navigated
		// with arrow keys + ENTER. PhoneDialerPad::pressGlyph fires the
		// onPress callback we wired in the ctor, which in turn appends
		// the glyph to the buffer.
		case BTN_0: if(pad) pad->pressGlyph('0'); break;
		case BTN_1: if(pad) pad->pressGlyph('1'); break;
		case BTN_2: if(pad) pad->pressGlyph('2'); break;
		case BTN_3: if(pad) pad->pressGlyph('3'); break;
		case BTN_4: if(pad) pad->pressGlyph('4'); break;
		case BTN_5: {
			// S167 - snapshot the buffer length BEFORE the speculative
			// append so a subsequent long-press (buttonHeld) can roll
			// the buffer back to where it was when the user first
			// touched the key. Snapshotting is one comparison + one
			// store, so the normal short-press path stays free.
			fivePreBufferLen = bufferLen;
			fiveLongFired    = false;
			if(pad) pad->pressGlyph('5');
			break;
		}
		case BTN_6: if(pad) pad->pressGlyph('6'); break;
		case BTN_7: if(pad) pad->pressGlyph('7'); break;
		case BTN_8: if(pad) pad->pressGlyph('8'); break;
		case BTN_9: if(pad) pad->pressGlyph('9'); break;

		// L / R bumpers cycle through '*' and '#' since the Chatter has
		// no dedicated *,# keys. This keeps the dialer fully driveable
		// from hardware-only input on the prototype board. The cursor
		// also hops onto the matched key so a follow-up arrow-press
		// starts from the expected position.
		case BTN_L: if(pad) pad->pressGlyph('*'); break;
		case BTN_R: if(pad) pad->pressGlyph('#'); break;

		// ENTER on the centre A button: press whatever the pad cursor is
		// currently focused on. Same press-flash + onPress dispatch path
		// as the direct-numpad case above.
		case BTN_ENTER:
			if(pad) pad->pressSelected();
			break;

		// Arrow-key navigation across the pad. The board's BTN_LEFT and
		// BTN_RIGHT serve double duty as up/down navigation (BTN_UP is
		// aliased to BTN_LEFT, BTN_DOWN to BTN_RIGHT in Pins.hpp), but
		// here we use them as the dialer's softkeys: LEFT = CALL, RIGHT
		// = BACK. Pad navigation is therefore done with the same keys
		// once the user is dialling - 2/4/6/8 act as the up/left/right/
		// down arrows on every classic feature phone.
		case BTN_LEFT:
			// "CALL" softkey - flash before invoking the host handler so
			// the user gets a click cue even if the handler immediately
			// pushes the next screen. With an empty buffer we still flash
			// (the user clearly tried to call) but skip the callback so a
			// future Active-Call screen does not have to defensively
			// check for empty input.
			if(softKeys) softKeys->flashLeft();
			if(bufferLen > 0 && callCb) callCb(this);
			break;

		case BTN_RIGHT:
			// "BACK" softkey - flash on press, but defer the actual
			// short-press action (backspace) to buttonReleased so a
			// long-press that already fired (exit) does not double-fire
			// on key release.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneDialerScreen::buttonReleased(uint i) {
	// Short-press dispatch for BTN_RIGHT lives here so a hold-then-release
	// does not also backspace the buffer when the long-press already
	// exited the dialer.
	switch(i) {
		case BTN_RIGHT:
			if(!backLongFired) {
				if(bufferLen > 0) {
					// Buffer has content - short press = backspace last
					// digit. The user can long-press to exit only when
					// they have finished editing.
					backspace();
				} else {
					// Empty buffer + short BACK press = exit the dialer.
					// This matches feature-phone muscle memory: a fresh
					// dialer with nothing typed exits on the first BACK,
					// without forcing the user to long-press just because
					// they opened it by mistake.
					if(exitCb) {
						exitCb(this);
					} else {
						pop();
					}
				}
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneDialerScreen::buttonHeld(uint i) {
	switch(i) {
		case BTN_RIGHT:
			// Long-press BACK = exit the dialer regardless of buffer
			// state. Useful when the user has typed half a number and
			// wants to bail out without backspacing twenty times.
			backLongFired = true;
			if(softKeys) softKeys->flashRight();
			if(exitCb) {
				exitCb(this);
			} else {
				pop();
			}
			break;

		case BTN_5:
			// S167 - hold-to-flashlight quick shortcut. The matching
			// short-press handler in buttonPressed already speculatively
			// appended '5' to the buffer; launchFlashlight() rolls that
			// append back so the user lands on a clean dialer when
			// they pop the torch, then pushes the flashlight. The
			// fiveLongFired flag is set first so any future deferred
			// short-press handler we wire up cannot double-fire on
			// key release.
			fiveLongFired = true;
			launchFlashlight();
			break;

		default:
			break;
	}
}
