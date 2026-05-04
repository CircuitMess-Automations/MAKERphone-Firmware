#include "PhoneImeiRevealScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneAboutScreen.cpp, PhoneSettingsScreen.cpp). Cyan for
// the caption and the big IMEI value (the focal point of the reveal),
// dim purple for the small subtitle below it.
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan caption + value
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (unused on this screen but kept for parity)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple subtitle

// Vertical anchors. Status bar y=0..10, soft-key bar y=118..128. The
// caption sits just below the status bar, the big 15-digit value is
// centred in the visible content area, and the subtitle sits a few
// pixels below the value to read as "small print under the headline".
static constexpr lv_coord_t kCaptionY    = 14;
static constexpr lv_coord_t kImeiY       = 50;
static constexpr lv_coord_t kSubtitleY   = 86;

PhoneImeiRevealScreen::PhoneImeiRevealScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  imeiLabel(nullptr),
		  subtitleLabel(nullptr) {

	// Full-screen container, no scrollbars, no inner padding - same blank
	// canvas pattern PhoneAboutScreen / PhoneSettingsScreen / etc. use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the labels overlay it
	// cleanly. Even an Easter-egg reveal should feel like part of the
	// MAKERphone family rather than a raw debug terminal.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Standard signal | clock | battery bar - keeps the device alive at
	// a glance while the user is reading the reveal.
	statusBar = new PhoneStatusBar(obj);

	buildLabels();

	// Single-action softkey bar: only BACK is meaningful. Leave the left
	// softkey blank (the page is read-only) so the bar stays uncluttered
	// and the user is not invited to do anything other than dismiss.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");
}

PhoneImeiRevealScreen::~PhoneImeiRevealScreen() {
	// Children (wallpaper, statusBar, softKeys, labels) are all parented
	// to obj - LVGL frees them recursively when the LVScreen base
	// destructor tears down obj. Nothing manual to do here.
}

void PhoneImeiRevealScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneImeiRevealScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders --------------------------------------------------------

void PhoneImeiRevealScreen::buildLabels() {
	// Small "IMEI" caption in pixelbasic7 cyan. Same anchor pattern
	// PhoneAboutScreen uses for "ABOUT", PhoneCallHistory uses for
	// "CALL HISTORY", etc.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "IMEI");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);

	// Big 15-digit value in pixelbasic16, the focal point of the
	// reveal. Computed once on construction from the device's efuse
	// MAC so the same prototype always shows the same number.
	imeiLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(imeiLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(imeiLabel, MP_HIGHLIGHT, 0);
	{
		char buf[20]; // "AAAAAA BB CCCCCC D" + NUL = 19 chars (max), 20-byte buf
		formatImei((uint64_t) ESP.getEfuseMac(), buf, sizeof(buf));
		lv_label_set_text(imeiLabel, buf);
	}
	lv_obj_set_align(imeiLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(imeiLabel, kImeiY);

	// Small dim "serial number" subtitle so the reveal reads as a
	// labelled result rather than a free-floating string. Lower-case
	// to match the visual rhythm of the small captions used elsewhere
	// (we never lowercase the headline cyan caption above).
	subtitleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(subtitleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(subtitleLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(subtitleLabel, "serial number");
	lv_obj_set_align(subtitleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(subtitleLabel, kSubtitleY);

	// MP_TEXT is referenced via the macro so the compiler does not
	// warn about an unused color in this translation unit. We keep
	// the macro defined for parity with the other phone screens'
	// palette block - any future tweak (e.g. recolouring the
	// subtitle to cream) becomes a one-line change.
	(void) MP_TEXT;
}

// ----- input -----------------------------------------------------------

void PhoneImeiRevealScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_BACK:
		case BTN_ENTER:
		case BTN_RIGHT:
			// Flash the BACK softkey for tactile feedback then pop. The
			// reveal page has no commit/discard distinction so ENTER
			// behaves the same as BACK -- a friendly second way out.
			// BTN_RIGHT is also accepted because PhoneDialerScreen treats
			// it as the "BACK" softkey (the Chatter d-pad doubles as
			// soft-key buttons), and we want the same muscle memory to
			// dismiss the reveal screen.
			if(softKeys) softKeys->flashRight();
			pop();
			break;
		default:
			break;
	}
}

// ----- formatters ------------------------------------------------------

void PhoneImeiRevealScreen::formatImei(uint64_t mac, char* out, size_t outLen) {
	if(out == nullptr || outLen < 20) {
		// Defensively NUL-terminate if there is at least one byte. The
		// caller violated the contract; an empty string is the safest
		// thing we can hand back without writing past the buffer.
		if(out != nullptr && outLen > 0) out[0] = '\0';
		return;
	}

	// Pull 14 digits out of the MAC. We mask off the upper byte first
	// so the modulo operates on a 56-bit space (MACs are 48-bit, but
	// future widening is cheap to absorb). The modulo by 10^14 trims
	// any leading hex bits that would otherwise produce a non-decimal
	// digit; the zero-pad ensures every device gets exactly 14 digits
	// regardless of how small its MAC happens to be.
	const uint64_t masked = mac & 0x00FFFFFFFFFFFFFFULL;
	const uint64_t kTenPow14 = 100000000000000ULL; // 10^14
	uint64_t value = masked % kTenPow14;

	char digits14[15]; // 14 digits + NUL
	for(int8_t i = 13; i >= 0; --i) {
		digits14[i] = (char) ('0' + (value % 10));
		value /= 10;
	}
	digits14[14] = '\0';

	const uint8_t check = luhnCheckDigit(digits14);

	// Render with the classic Sony-Ericsson IMEI grouping
	// "AAAAAA BB CCCCCC D" -- TAC (6) / FAC (2) / SNR (6) / Luhn check
	// (1) -- which is the 15-digit layout the *#06# reveal printed on
	// the original Ericsson and early Sony-Ericsson handsets. 14 digits
	// from the MAC fill TAC+FAC+SNR; the 15th is the Luhn check digit.
	// Total layout: 6 + space + 2 + space + 6 + space + 1 = 18 chars
	// + NUL = 19, fitting comfortably in the 20-byte buffer.
	snprintf(out, outLen,
			 "%c%c%c%c%c%c %c%c %c%c%c%c%c%c %u",
			 digits14[0],  digits14[1],  digits14[2],
			 digits14[3],  digits14[4],  digits14[5],
			 digits14[6],  digits14[7],
			 digits14[8],  digits14[9],  digits14[10],
			 digits14[11], digits14[12], digits14[13],
			 // 15th glyph is the Luhn check digit (always 0..9), printed
			 // as an unsigned int so the format spec stays compatible
			 // with avr-gcc's snprintf.
			 (unsigned) check);
}

uint8_t PhoneImeiRevealScreen::luhnCheckDigit(const char* digits14) {
	// Standard Luhn: starting from the rightmost digit of the partial
	// number (the digit that will be paired with the to-be-computed
	// check digit), double every other digit. Our partial is 14 chars
	// long, so doubling alternates starting from index 13 (rightmost)
	// going left -> indices 13, 11, 9, ... get doubled. Sum the
	// digits of the doubled values and the un-doubled values together,
	// then the check digit is (10 - (sum mod 10)) mod 10.
	if(digits14 == nullptr) return 0;

	uint16_t sum = 0;
	for(uint8_t i = 0; i < 14; ++i) {
		uint8_t d = (uint8_t) (digits14[i] - '0');
		// Distance from the rightmost data digit. The rightmost data
		// digit (i==13) is doubled; every other digit walking left
		// from there is doubled.
		const uint8_t fromRight = (uint8_t) (13 - i);
		if((fromRight & 1u) == 0u) {
			d = (uint8_t) (d * 2);
			if(d >= 10) d = (uint8_t) (d - 9); // sum of digits of d*2
		}
		sum = (uint16_t) (sum + d);
	}
	const uint8_t check = (uint8_t) ((10 - (sum % 10)) % 10);
	return check;
}
