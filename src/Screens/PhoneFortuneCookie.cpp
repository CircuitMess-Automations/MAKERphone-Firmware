#include "PhoneFortuneCookie.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

// MAKERphone retro palette — kept identical to every other Phone*
// widget so the fortune cookie slots in beside PhoneCalculator (S60),
// PhoneAlarmClock (S124), PhoneTimers (S125), PhoneCurrencyConverter
// (S126), PhoneUnitConverter (S127), PhoneWorldClock (S128),
// PhoneVirtualPet (S129), PhoneMagic8Ball (S130), PhoneDiceRoller
// (S131) and PhoneCoinFlip (S132) without a visual seam. Same
// inline-#define convention every other Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple

// Cookie-specific shades, derived from the shared palette so the
// widget reads as part of the family. The cookie body is a slightly
// darker variant of MP_ACCENT (sunset-orange) and the fold stroke is
// a desaturated cream so it suggests baked dough on warm orange.
#define MP_COOKIE_BODY     lv_color_make(220, 130,  30)  // baked orange
#define MP_COOKIE_FOLD     lv_color_make(160,  92,  18)  // crease shadow
#define MP_PAPER_BG        lv_color_make(248, 232, 196)  // paper cream

// =====================================================================
// S133 — PhoneFortuneCookie — fortune table
//
// 32 short, original wisdoms in fortune-cookie cadence. The strings
// are deliberately short so they wrap inside the 132 px paper strip
// at pixelbasic7 (each line is ~7 px tall, the paper holds ~5 lines
// comfortably). Lines are written with all-caps to match the rest of
// the shell.
//
// Wording note: every entry is original phrasing composed for
// MAKERphone — none are quoted from the classic cookie canon, so the
// table can be reordered, extended, or trimmed without copyright
// concerns.
// =====================================================================

static const char* const kFortunes[PhoneFortuneCookie::FortuneCount] = {
	"A SMILE TODAY OPENS A DOOR TOMORROW.",
	"PATIENCE TURNS A SETBACK INTO A SETUP.",
	"THE BEST TIME TO PLANT A SEED IS NOW.",
	"SMALL STEPS STILL REACH THE MOUNTAIN TOP.",
	"A KIND WORD WEIGHS NOTHING AND CARRIES FAR.",
	"TRUST THE RHYTHM OF YOUR OWN PROGRESS.",
	"NEW PATHS APPEAR WHEN YOU LIFT YOUR EYES.",
	"TOMORROW REWARDS THE COURAGE OF TODAY.",
	"WHAT YOU PRACTICE QUIETLY BECOMES YOUR GIFT.",
	"EVERY LOCK HAS ITS KEY. KEEP TRYING.",
	"GENTLE PERSISTENCE WEARS DOWN ANY WALL.",
	"GOOD WORK ATTRACTS BETTER COMPANY.",
	"THE LAUGH YOU SHARE TODAY ECHOES FOR YEARS.",
	"TODAY HOLDS A LESSON DRESSED AS A SURPRISE.",
	"SLOW DOWN TO HEAR THE QUIETER ANSWERS.",
	"YOUR CURIOSITY IS A LANTERN. KEEP IT LIT.",
	"BORROW STRENGTH, BUT NEVER BORROW DOUBT.",
	"A FINISHED FIRST DRAFT BEATS A PERFECT IDEA.",
	"WISE FRIENDS ARE THE BEST INTEREST RATE.",
	"REST IS A TOOL. USE IT WITHOUT GUILT.",
	"WHAT YOU BUILD WITH KINDNESS WILL OUTLAST IT.",
	"LISTEN MORE THIS WEEK; SPEAK CLEARER NEXT.",
	"ASK FOR HELP AND YOU GAIN AN ALLY.",
	"TWO HONEST EYES SEE MORE THAN ONE BUSY MIND.",
	"A QUIET HOUR TODAY SAVES A LOUD ONE TOMORROW.",
	"GROWTH FEELS LIKE FRICTION. KEEP GOING.",
	"YOUR STORY IS STILL BEING WRITTEN.",
	"FOLLOW UP ON THE PROMISE YOU MADE LAST WEEK.",
	"A COMPLIMENT WITHHELD IS A GIFT WASTED.",
	"WHAT GIVES YOU JOY IS WORTH PROTECTING.",
	"PUT DOWN ONE WORRY YOU CARRIED THIS MONTH.",
	"TODAY'S FAVOUR WILL BE REMEMBERED FOR A YEAR.",
};

// ---------- geometry --------------------------------------------------
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar
//   y=12..18   "FORTUNE COOKIE" caption (pixelbasic7, cyan)
//   y=22..28   date sub-line (pixelbasic7, dim purple)
//
// CLOSED state:
//   y=42..78   cookie body 88x36 centred at x=36..124
//   y=98..104  hint ("PRESS CRACK TO OPEN")
//
// OPEN state:
//   y=34..78   paper strip 144x44 at x=8 (cream bg, cyan border)
//   y=84..90   lucky line ("LUCKY: 04 11 23 35 41")
//   y=98..104  hint ("PRESS AGAIN TO REPLAY")
//
//   y=118..128 PhoneSoftKeyBar
//
// All coordinates centralised here so a future skin only needs to
// tweak these constants.

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kDateY         = 22;
static constexpr lv_coord_t kHintY         = 98;

static constexpr lv_coord_t kCookieW       = 88;
static constexpr lv_coord_t kCookieH       = 36;
static constexpr lv_coord_t kCookieX       = (160 - kCookieW) / 2;
static constexpr lv_coord_t kCookieY       = 42;

static constexpr lv_coord_t kFoldH         = 4;
static constexpr lv_coord_t kFoldW         = 60;
static constexpr lv_coord_t kFoldX         = (160 - kFoldW) / 2;
static constexpr lv_coord_t kFoldY         = kCookieY + (kCookieH - kFoldH) / 2;

static constexpr lv_coord_t kPaperW        = 144;
static constexpr lv_coord_t kPaperH        = 44;
static constexpr lv_coord_t kPaperX        = (160 - kPaperW) / 2;
static constexpr lv_coord_t kPaperY        = 32;

static constexpr lv_coord_t kLuckyY        = 80;

// Wobble LUT, indexed by remaining frame count (0..CrackFrames-1).
// Hand-picked so the wobble reads as a deterministic-but-jittery
// crack rather than a sine. Final settle frame returns to (0, 0).
struct CookieWobble {
	int8_t dx;
	int8_t dy;
};

static constexpr CookieWobble kWobbleLUT[PhoneFortuneCookie::CrackFrames] = {
	{ +0, +0 },   // frameLeft == 0 -> settle
	{ -2,  0 },
	{ +2, +1 },
	{ -1, -1 },
	{ +2,  0 },
	{ -2, +1 },
};

// Decoy glyph cycle for the wobble. Cycles through "?" -> "!" -> "*"
// while the cookie is shaking, to fake the contents tumbling inside.
static constexpr char kDecoyGlyph[3] = { '?', '!', '*' };

// ---------- public statics --------------------------------------------

const char* PhoneFortuneCookie::fortuneAt(uint8_t idx) {
	if(idx >= FortuneCount) return nullptr;
	return kFortunes[idx];
}

uint8_t PhoneFortuneCookie::fortuneOfDay(uint32_t dayIndex) {
	// Stable mod -- FortuneCount is a power of two (32) so the
	// distribution is uniform over the 32-entry table.
	return static_cast<uint8_t>(dayIndex % FortuneCount);
}

void PhoneFortuneCookie::luckyNumbersForDay(uint32_t dayIndex,
                                            uint8_t out[LuckyNumberCount]) {
	if(out == nullptr) return;

	// Tiny LCG seeded from the day index, mixed with a fixed magic
	// so the lucky numbers do not align with the fortune index in
	// a visually-obvious way. Each draw rejects collisions, so the
	// returned array always contains LuckyNumberCount unique values
	// in [1..LuckyNumberMax].
	uint32_t state = (dayIndex ^ 0x9E3779B1UL) * 1664525UL + 1013904223UL;
	uint8_t  count = 0;
	while(count < LuckyNumberCount) {
		state = state * 1664525UL + 1013904223UL;
		const uint8_t pick = static_cast<uint8_t>((state >> 16) % LuckyNumberMax) + 1;
		bool dup = false;
		for(uint8_t i = 0; i < count; ++i) {
			if(out[i] == pick) { dup = true; break; }
		}
		if(!dup) {
			out[count++] = pick;
		}
	}

	// Insertion sort — LuckyNumberCount is tiny (5) so cost is
	// negligible and the printed line reads in ascending order.
	for(uint8_t i = 1; i < LuckyNumberCount; ++i) {
		const uint8_t key = out[i];
		int8_t j = static_cast<int8_t>(i) - 1;
		while(j >= 0 && out[j] > key) {
			out[j + 1] = out[j];
			--j;
		}
		out[j + 1] = key;
	}
}

// ---------- ctor / dtor -----------------------------------------------

PhoneFortuneCookie::PhoneFortuneCookie()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  dateLabel(nullptr),
		  hintLabel(nullptr),
		  cookieBody(nullptr),
		  cookieFold(nullptr),
		  cookieMark(nullptr),
		  paperStrip(nullptr),
		  fortuneLabel(nullptr),
		  luckyLabel(nullptr) {

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
	lv_label_set_text(captionLabel, "FORTUNE COOKIE");

	dateLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(dateLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(dateLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(dateLabel, 160);
	lv_obj_set_pos(dateLabel, 0, kDateY);
	lv_obj_set_style_text_align(dateLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(dateLabel, "TODAY'S WISDOM");

	buildHud();

	// Bottom soft-key bar; left label flips between CRACK / AGAIN
	// depending on state, right label is always BACK.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("CRACK");
	softKeys->setRight("BACK");

	// Final paint: snapshot the clock and render the closed face.
	refreshFromClock();
	renderClosed();
}

PhoneFortuneCookie::~PhoneFortuneCookie() {
	stopCrackTimer();
}

// ---------- build helpers ---------------------------------------------

void PhoneFortuneCookie::buildHud() {
	// --- closed cookie body -----------------------------------------
	// A single rounded rect in baked-orange. The fold stroke and the
	// "?" glyph are nested inside so wobbling the body drags both
	// along with it for free.
	cookieBody = lv_obj_create(obj);
	lv_obj_remove_style_all(cookieBody);
	lv_obj_set_size(cookieBody, kCookieW, kCookieH);
	lv_obj_set_pos(cookieBody, kCookieX, kCookieY);
	lv_obj_set_style_radius(cookieBody, kCookieH / 2, 0);
	lv_obj_set_style_bg_opa(cookieBody, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(cookieBody, MP_COOKIE_BODY, 0);
	lv_obj_set_style_border_width(cookieBody, 1, 0);
	lv_obj_set_style_border_color(cookieBody, MP_ACCENT, 0);
	lv_obj_set_style_pad_all(cookieBody, 0, 0);
	lv_obj_clear_flag(cookieBody, LV_OBJ_FLAG_SCROLLABLE);

	// Fold/crease line across the cookie. A short horizontal bar in
	// a darker brown so the cookie reads as a "folded" shell rather
	// than a flat lozenge.
	cookieFold = lv_obj_create(cookieBody);
	lv_obj_remove_style_all(cookieFold);
	lv_obj_set_size(cookieFold, kFoldW, kFoldH);
	lv_obj_align(cookieFold, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_radius(cookieFold, kFoldH / 2, 0);
	lv_obj_set_style_bg_opa(cookieFold, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(cookieFold, MP_COOKIE_FOLD, 0);
	lv_obj_set_style_border_width(cookieFold, 0, 0);
	lv_obj_set_style_pad_all(cookieFold, 0, 0);
	lv_obj_clear_flag(cookieFold, LV_OBJ_FLAG_SCROLLABLE);

	// "?" glyph (or decoy during wobble) inside the cookie. Sits on
	// top of the fold so the user reads "cookie with a riddle inside".
	cookieMark = lv_label_create(cookieBody);
	lv_obj_set_style_text_font(cookieMark, &pixelbasic16, 0);
	lv_obj_set_style_text_color(cookieMark, MP_BG_DARK, 0);
	lv_obj_align(cookieMark, LV_ALIGN_CENTER, 0, 0);
	lv_label_set_text(cookieMark, "?");

	// --- open paper strip -------------------------------------------
	// Sits at fixed coords; toggled hidden/visible by render*().
	paperStrip = lv_obj_create(obj);
	lv_obj_remove_style_all(paperStrip);
	lv_obj_set_size(paperStrip, kPaperW, kPaperH);
	lv_obj_set_pos(paperStrip, kPaperX, kPaperY);
	lv_obj_set_style_radius(paperStrip, 2, 0);
	lv_obj_set_style_bg_opa(paperStrip, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(paperStrip, MP_PAPER_BG, 0);
	lv_obj_set_style_border_width(paperStrip, 1, 0);
	lv_obj_set_style_border_color(paperStrip, MP_HIGHLIGHT, 0);
	lv_obj_set_style_pad_all(paperStrip, 3, 0);
	lv_obj_clear_flag(paperStrip, LV_OBJ_FLAG_SCROLLABLE);

	fortuneLabel = lv_label_create(paperStrip);
	lv_obj_set_style_text_font(fortuneLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(fortuneLabel, MP_BG_DARK, 0);
	lv_obj_set_width(fortuneLabel, kPaperW - 8);
	lv_obj_align(fortuneLabel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_align(fortuneLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(fortuneLabel, LV_LABEL_LONG_WRAP);
	lv_label_set_text(fortuneLabel, "");

	// "LUCKY: 04 11 23 35 41" line. Lives outside the paper so the
	// paper itself is purely the wisdom and the lucky line reads
	// like a separate signature.
	luckyLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(luckyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(luckyLabel, MP_TEXT, 0);
	lv_obj_set_width(luckyLabel, 160);
	lv_obj_set_pos(luckyLabel, 0, kLuckyY);
	lv_obj_set_style_text_align(luckyLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(luckyLabel, "");

	// Hint stays static for the screen lifetime, but the text it
	// shows depends on whether the cookie is open or closed. Built
	// here so renderClosed()/renderOpen() can just rewrite the text.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(hintLabel, 160);
	lv_obj_set_pos(hintLabel, 0, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "PRESS CRACK TO OPEN");
}

// ---------- lifecycle -------------------------------------------------

void PhoneFortuneCookie::onStart() {
	Input::getInstance()->addListener(this);
	// Re-snapshot the clock — if the user came back the next morning
	// the day-of-fortune should advance without needing to re-push
	// the screen.
	refreshFromClock();
	closeCookie();
}

void PhoneFortuneCookie::onStop() {
	Input::getInstance()->removeListener(this);
	stopCrackTimer();
	cracking = false;
}

// ---------- clock + state -------------------------------------------

void PhoneFortuneCookie::refreshFromClock() {
	const uint32_t epoch = PhoneClock::nowEpoch();
	currentDayIdx = epoch / 86400UL;
	currentIdx    = fortuneOfDay(currentDayIdx);

	// Date label: "MAY 03 2026" style. Use PhoneClock's
	// monthName helper so the calendar conventions stay in one place.
	uint16_t y; uint8_t mo, d, h, mi, s, dow;
	(void)PhoneClock::now(y, mo, d, h, mi, s, dow);

	const char* monthStr = PhoneClock::monthName(mo);
	if(monthStr == nullptr || monthStr[0] == '\0') monthStr = "---";

	char buf[32];
	snprintf(buf, sizeof(buf), "%s %02u  %u",
	         monthStr,
	         static_cast<unsigned>(d),
	         static_cast<unsigned>(y));
	if(dateLabel != nullptr) {
		lv_label_set_text(dateLabel, buf);
	}

	// Fortune text is bound to the fortune index; the paper label
	// is updated here so renderOpen() can render without recomputing.
	const char* text = fortuneAt(currentIdx);
	if(text == nullptr) text = "STAY CURIOUS.";
	if(fortuneLabel != nullptr) {
		lv_label_set_text(fortuneLabel, text);
	}

	// Lucky numbers line.
	uint8_t lucky[LuckyNumberCount];
	luckyNumbersForDay(currentDayIdx, lucky);
	char luckyBuf[40];
	int written = snprintf(luckyBuf, sizeof(luckyBuf), "LUCKY:");
	for(uint8_t i = 0; i < LuckyNumberCount && written > 0; ++i) {
		const int rem = static_cast<int>(sizeof(luckyBuf)) - written;
		if(rem <= 0) break;
		const int n = snprintf(luckyBuf + written, static_cast<size_t>(rem),
		                       " %02u", static_cast<unsigned>(lucky[i]));
		if(n < 0) break;
		written += n;
	}
	if(luckyLabel != nullptr) {
		lv_label_set_text(luckyLabel, luckyBuf);
	}
}

// ---------- render ----------------------------------------------------

void PhoneFortuneCookie::renderClosed() {
	open      = false;
	cracking  = false;

	if(cookieBody != nullptr) {
		lv_obj_clear_flag(cookieBody, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_pos(cookieBody, kCookieX, kCookieY);
	}
	if(cookieMark != nullptr) {
		lv_obj_set_style_text_color(cookieMark, MP_BG_DARK, 0);
		lv_label_set_text(cookieMark, "?");
	}
	if(paperStrip != nullptr)  lv_obj_add_flag(paperStrip,  LV_OBJ_FLAG_HIDDEN);
	if(luckyLabel != nullptr)  lv_obj_add_flag(luckyLabel,  LV_OBJ_FLAG_HIDDEN);
	if(hintLabel  != nullptr)  lv_label_set_text(hintLabel, "PRESS CRACK TO OPEN");
	if(softKeys   != nullptr)  softKeys->setLeft("CRACK");
}

void PhoneFortuneCookie::renderOpen() {
	open      = true;
	cracking  = false;

	if(cookieBody != nullptr)  lv_obj_add_flag(cookieBody,  LV_OBJ_FLAG_HIDDEN);
	if(paperStrip != nullptr)  lv_obj_clear_flag(paperStrip, LV_OBJ_FLAG_HIDDEN);
	if(luckyLabel != nullptr)  lv_obj_clear_flag(luckyLabel, LV_OBJ_FLAG_HIDDEN);
	if(hintLabel  != nullptr)  lv_label_set_text(hintLabel,  "PRESS AGAIN TO REPLAY");
	if(softKeys   != nullptr)  softKeys->setLeft("AGAIN");
}

void PhoneFortuneCookie::renderCrackFrame(uint8_t frameLeft) {
	if(cookieBody == nullptr) return;
	const uint8_t i = (frameLeft >= CrackFrames) ? 0
	                                             : (uint8_t)(frameLeft % CrackFrames);
	const CookieWobble w = kWobbleLUT[i];
	lv_obj_set_pos(cookieBody,
	               (lv_coord_t)(kCookieX + w.dx),
	               (lv_coord_t)(kCookieY + w.dy));

	// Cycle the decoy glyph so the user perceives "stuff tumbling
	// inside the cookie" before the paper falls out. We index off
	// frameLeft so the cycle is deterministic per frame.
	if(cookieMark != nullptr) {
		const char glyph = kDecoyGlyph[frameLeft % 3];
		const char str[2] = { glyph, '\0' };
		lv_label_set_text(cookieMark, str);
	}
}

// ---------- crack animation ------------------------------------------

void PhoneFortuneCookie::beginCrack() {
	if(cracking) return;          // mashing 5 must not extend the wobble
	cracking       = true;
	crackFrameLeft = CrackFrames;

	// Make sure the cookie is visible at the start of the wobble in
	// case we're "replaying" from the open state.
	if(cookieBody != nullptr) {
		lv_obj_clear_flag(cookieBody, LV_OBJ_FLAG_HIDDEN);
	}
	if(paperStrip != nullptr) lv_obj_add_flag(paperStrip, LV_OBJ_FLAG_HIDDEN);
	if(luckyLabel != nullptr) lv_obj_add_flag(luckyLabel, LV_OBJ_FLAG_HIDDEN);

	// Ensure today's fortune index is current — if the user left
	// the screen open across midnight the next CRACK should reveal
	// the new day's wisdom.
	refreshFromClock();

	// Render the first wobble frame immediately so the press feels
	// instantaneous; the timer takes over from frame 2 onwards.
	renderCrackFrame(crackFrameLeft);

	startCrackTimer();
}

void PhoneFortuneCookie::closeCookie() {
	stopCrackTimer();
	renderClosed();
}

void PhoneFortuneCookie::startCrackTimer() {
	if(crackTimer != nullptr) return;
	crackTimer = lv_timer_create(&PhoneFortuneCookie::onCrackTickStatic,
	                             CrackPeriodMs, this);
}

void PhoneFortuneCookie::stopCrackTimer() {
	if(crackTimer == nullptr) return;
	lv_timer_del(crackTimer);
	crackTimer = nullptr;
}

void PhoneFortuneCookie::onCrackTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneFortuneCookie*>(timer->user_data);
	if(self == nullptr) return;

	if(self->crackFrameLeft <= 1) {
		// Final frame: settle on the open state, hide the cookie,
		// drop the timer so we go back to zero per-frame cost.
		self->renderOpen();
		self->crackFrameLeft = 0;
		self->stopCrackTimer();
		return;
	}

	self->crackFrameLeft--;
	self->renderCrackFrame(self->crackFrameLeft);
}

// ---------- input -----------------------------------------------------

void PhoneFortuneCookie::buttonPressed(uint i) {
	switch(i) {
		// Any of the SHAKE-equivalent keys triggers a crack.
		case BTN_1:
		case BTN_2:
		case BTN_3:
		case BTN_4:
		case BTN_5:
		case BTN_6:
		case BTN_7:
		case BTN_8:
		case BTN_9:
		case BTN_ENTER:
			if(softKeys) softKeys->flashLeft();
			beginCrack();
			break;

		case BTN_L:
			// Left bumper mirrors the left softkey.
			if(softKeys) softKeys->flashLeft();
			beginCrack();
			break;

		case BTN_0:
			// Quick "close" reset — useful when the user wants to
			// re-enter the closed-cookie state without leaving.
			if(cracking) break;
			closeCookie();
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

		case BTN_LEFT:
		case BTN_RIGHT:
			// No horizontal cursor today; absorb the keys so they
			// don't fall through to anything else.
			break;

		default:
			break;
	}
}

void PhoneFortuneCookie::buttonReleased(uint i) {
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

void PhoneFortuneCookie::buttonHeld(uint i) {
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
