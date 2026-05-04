#include "PhoneAsciiArt.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette -- kept identical to every other Phone*
// widget so the ASCII gallery slots in beside PhoneFortuneCookie
// (S133), PhoneCoinFlip (S132), PhoneMagic8Ball (S130),
// PhoneDiceRoller (S131) and PhoneFlashlight (S134) without a
// visual seam. Same inline-#define convention every other Phone*
// screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple

// Panel-specific shade: a slightly darkened deep-purple so the
// framed art reads as a separate "page" against the synthwave bg.
#define MP_PANEL_BG        lv_color_make( 12,   8,  24)

// =====================================================================
// S170 -- PhoneAsciiArt -- title + art tables
//
// Eight original ASCII drawings, each fewer than 7 lines tall and
// fewer than 22 chars wide so they fit cleanly inside the framed
// art panel at pixelbasic7 (line_height = 9 px, panel_height ~ 70 px,
// width ~ 132 px usable).
//
// Each drawing is bilaterally symmetric (or close to it) so that
// LV_TEXT_ALIGN_CENTER produces a clean, balanced composition even
// though pixelbasic7 is not strictly monospace.
//
// All compositions are original to MAKERphone -- not copied from any
// existing ASCII-art collection -- so the table can be reordered,
// extended, or trimmed without copyright concerns.
// =====================================================================

static const char* const kTitles[PhoneAsciiArt::SlideCount] = {
	"CAT",
	"HEART",
	"ROCKET",
	"SAILBOAT",
	"STAR",
	"SMILEY",
	"HOUSE",
	"NOTE",
};

// NOTE: we write the C strings with explicit "\n" so the line
// breaks are part of the literal -- no need for any runtime parsing.

static const char* const kArt[PhoneAsciiArt::SlideCount] = {
	// ---- CAT (sleeping) ----
	" /\\___/\\\n"
	"( o   o )\n"
	" (  ^  )\n"
	"  \\___/",

	// ---- HEART ----
	"  ##    ##\n"
	" ##########\n"
	" ##########\n"
	"  ########\n"
	"   ######\n"
	"    ####\n"
	"     ##",

	// ---- ROCKET ----
	"     /\\\n"
	"    /  \\\n"
	"   /----\\\n"
	"   |  M |\n"
	"   |____|\n"
	"   /\\  /\\\n"
	"  *  **  *",

	// ---- SAILBOAT ----
	"     |\\\n"
	"     | \\\n"
	"     |  \\\n"
	"     |___\\\n"
	"  \\_______/\n"
	"   ~~~~~~~",

	// ---- STAR ----
	"     *\n"
	"    ***\n"
	"  *******\n"
	"   *****\n"
	"   ** **\n"
	"   *   *",

	// ---- SMILEY ----
	"   _____\n"
	"  /     \\\n"
	" |  o o  |\n"
	" |   ^   |\n"
	"  \\ \\_/ /\n"
	"   '---'",

	// ---- HOUSE ----
	"      /\\\n"
	"     /  \\\n"
	"    /    \\\n"
	"   /______\\\n"
	"   | [] []|\n"
	"   | []   |\n"
	"   |______|",

	// ---- NOTE (musical) ----
	"     ###\n"
	"     # #\n"
	"     # #\n"
	"     # #\n"
	"  #### #\n"
	"  ####",
};

// ---------- geometry --------------------------------------------------
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar
//   y=12..18   "ASCII GALLERY" caption (pixelbasic7, cyan)
//   y=22..28   per-slide title         (pixelbasic7, cream)
//
//   y=32..104  framed art panel 132x72 centred at x=14
//
//   y=108..114 indicator "1 / 8"       (pixelbasic7, dim)
//   y=118..128 PhoneSoftKeyBar
//
// All coordinates centralised here so a future skin only needs to
// tweak these constants.

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kTitleY        = 22;

static constexpr lv_coord_t kPanelW        = 132;
static constexpr lv_coord_t kPanelH        = 72;
static constexpr lv_coord_t kPanelX        = (160 - kPanelW) / 2;
static constexpr lv_coord_t kPanelY        = 32;

static constexpr lv_coord_t kIndicatorY    = 108;

// ---------- public statics --------------------------------------------

const char* PhoneAsciiArt::titleAt(uint8_t idx) {
	if(idx >= SlideCount) return nullptr;
	return kTitles[idx];
}

const char* PhoneAsciiArt::artAt(uint8_t idx) {
	if(idx >= SlideCount) return nullptr;
	return kArt[idx];
}

// ---------- ctor / dtor -----------------------------------------------

PhoneAsciiArt::PhoneAsciiArt()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  titleLabel(nullptr),
		  artPanel(nullptr),
		  artLabel(nullptr),
		  indicatorLabel(nullptr) {

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern as every other Phone* utility screen.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, 160);
	lv_obj_set_pos(captionLabel, 0, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "ASCII GALLERY");

	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_TEXT, 0);
	lv_obj_set_width(titleLabel, 160);
	lv_obj_set_pos(titleLabel, 0, kTitleY);
	lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(titleLabel, "");

	buildHud();

	// Bottom soft-key bar; left advances, right exits. The labels
	// stay constant for the lifetime of the screen since the
	// primary affordance is the same on every slide.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("NEXT");
	softKeys->setRight("BACK");

	// Final paint: render slide 0.
	renderCurrent();
}

PhoneAsciiArt::~PhoneAsciiArt() = default;

// ---------- build helpers ---------------------------------------------

void PhoneAsciiArt::buildHud() {
	// --- framed art panel ------------------------------------------
	// A single rounded rect in deep-purple with a thin cyan border
	// so the art reads as a "page" of its own against the synthwave
	// wallpaper. The body label is parented inside so any future
	// resize/repos of the panel drags the art with it for free.
	artPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(artPanel);
	lv_obj_set_size(artPanel, kPanelW, kPanelH);
	lv_obj_set_pos(artPanel, kPanelX, kPanelY);
	lv_obj_set_style_radius(artPanel, 3, 0);
	lv_obj_set_style_bg_opa(artPanel, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(artPanel, MP_PANEL_BG, 0);
	lv_obj_set_style_border_width(artPanel, 1, 0);
	lv_obj_set_style_border_color(artPanel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_pad_all(artPanel, 2, 0);
	lv_obj_clear_flag(artPanel, LV_OBJ_FLAG_SCROLLABLE);

	// Art body. Centre-aligned so each line self-balances horizontally
	// even when pixelbasic7 produces slightly different glyph widths
	// for different characters.
	artLabel = lv_label_create(artPanel);
	lv_obj_set_style_text_font(artLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(artLabel, MP_TEXT, 0);
	lv_obj_set_width(artLabel, kPanelW - 6);
	lv_obj_align(artLabel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_align(artLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(artLabel, LV_LABEL_LONG_WRAP);
	lv_label_set_text(artLabel, "");

	// Indicator below the panel: "N / 8". Dim purple so it reads as
	// metadata rather than competing with the title or art.
	indicatorLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(indicatorLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(indicatorLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(indicatorLabel, 160);
	lv_obj_set_pos(indicatorLabel, 0, kIndicatorY);
	lv_obj_set_style_text_align(indicatorLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(indicatorLabel, "");
}

// ---------- lifecycle -------------------------------------------------

void PhoneAsciiArt::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneAsciiArt::onStop() {
	Input::getInstance()->removeListener(this);
}

// S168: shake-to-advance. The L+R chord steps to the next slide
// just like pressing NEXT would. Forwards to advance() so the
// wrap-around behavior is reused unchanged.
void PhoneAsciiArt::onShake() {
	advance();
}

// ---------- navigation ------------------------------------------------

void PhoneAsciiArt::advance() {
	currentIdx = static_cast<uint8_t>((currentIdx + 1) % SlideCount);
	renderCurrent();
}

void PhoneAsciiArt::retreat() {
	currentIdx = static_cast<uint8_t>((currentIdx + SlideCount - 1) % SlideCount);
	renderCurrent();
}

void PhoneAsciiArt::jumpTo(uint8_t idx) {
	currentIdx = static_cast<uint8_t>(idx % SlideCount);
	renderCurrent();
}

// ---------- render ----------------------------------------------------

void PhoneAsciiArt::renderCurrent() {
	const char* title = titleAt(currentIdx);
	const char* body  = artAt(currentIdx);
	if(title == nullptr) title = "";
	if(body  == nullptr) body  = "";

	if(titleLabel != nullptr) {
		lv_label_set_text(titleLabel, title);
	}
	if(artLabel != nullptr) {
		lv_label_set_text(artLabel, body);
		// Re-centre after content swap -- different drawings have
		// different heights, so LV_ALIGN_CENTER keeps each piece
		// vertically balanced within the framed panel.
		lv_obj_align(artLabel, LV_ALIGN_CENTER, 0, 0);
	}
	if(indicatorLabel != nullptr) {
		char buf[12];
		snprintf(buf, sizeof(buf), "%u / %u",
		         static_cast<unsigned>(currentIdx + 1),
		         static_cast<unsigned>(SlideCount));
		lv_label_set_text(indicatorLabel, buf);
	}
}

// ---------- input -----------------------------------------------------

void PhoneAsciiArt::buttonPressed(uint i) {
	switch(i) {
		// Primary "NEXT" affordance: any centre key, the right
		// d-pad arrow, the left bumper (mirrors the left softkey),
		// or the digit row 5/ENTER advances the slideshow. This
		// matches the "press anything to flip the page" feel of
		// every physical handheld viewer.
		case BTN_RIGHT:
		case BTN_ENTER:
		case BTN_5:
		case BTN_L:
			if(softKeys) softKeys->flashLeft();
			advance();
			break;

		case BTN_LEFT:
			// Secondary "PREV" affordance on the d-pad. No softkey
			// flash (there is no PREV softkey by design -- one
			// affordance, one action), but we still wrap around.
			retreat();
			break;

		case BTN_0:
			// Quick "rewind" -- handy when the user has cycled past
			// their favorite drawing and wants to start over.
			jumpTo(0);
			break;

		case BTN_R:
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		// Other digits are absorbed silently so they do not fall
		// through to anything else (defence-in-depth -- a stray
		// dial-pad poke should not exit the gallery accidentally).
		case BTN_1:
		case BTN_2:
		case BTN_3:
		case BTN_4:
		case BTN_6:
		case BTN_7:
		case BTN_8:
		case BTN_9:
			break;

		default:
			break;
	}
}

void PhoneAsciiArt::buttonReleased(uint i) {
	switch(i) {
		case BTN_BACK:
			if(!backLongFired) {
				pop();
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneAsciiArt::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK = short tap = exit. The flag suppresses
			// the matching short-press fire-back on release.
			backLongFired = true;
			pop();
			break;

		default:
			break;
	}
}
