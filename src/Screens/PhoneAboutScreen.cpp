#include "PhoneAboutScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Storage/Storage.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneSettingsScreen.cpp, PhoneBrightnessScreen.cpp). Cyan
// for the caption (informational), warm cream for the value cells (the
// payload the user actually wants to read), dim purple for the small
// section labels above each value.
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan caption
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream value
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple section label

// Body geometry. Each label/value pair sits in a 20 px tall group:
// 9 px for the small dim label, 9 px for the cream value, with a 2 px
// gap before the next group. Starting at y=24 the four groups occupy
// y = 24..104, leaving room for the soft-key bar at y = 118.
static constexpr lv_coord_t kBodyX           = 4;
static constexpr lv_coord_t kBodyW           = 152;
static constexpr lv_coord_t kGroupTopY       = 24;
static constexpr lv_coord_t kGroupHeight     = 20;   // 9 + 9 + 2
static constexpr lv_coord_t kLabelOffset     = 0;
static constexpr lv_coord_t kValueOffset     = 9;

PhoneAboutScreen::PhoneAboutScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  deviceIdLabel(nullptr),
		  deviceIdValue(nullptr),
		  firmwareLabel(nullptr),
		  firmwareValue(nullptr),
		  runtimeLabel(nullptr),
		  heapValue(nullptr),
		  uptimeValue(nullptr),
		  peersLabel(nullptr),
		  peersValue(nullptr),
		  tickTimer(nullptr) {

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneBrightnessScreen / PhoneSettingsScreen / etc. use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the labels overlay it
	// cleanly. The About screen still feels like part of the MAKERphone
	// family rather than a debug terminal.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Standard signal | clock | battery bar so the user always knows the
	// device is alive.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildBody();

	// Single-action softkey bar: only BACK is meaningful. Leave the left
	// softkey blank rather than wiring something inert -- the page is
	// purely read-only so there is nothing for an OPEN softkey to do.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");

	// Initial paint of the live fields so the user sees real numbers
	// immediately on push (rather than blanks until the first 1 Hz tick).
	refreshLiveFields();
}

PhoneAboutScreen::~PhoneAboutScreen() {
	// Clean up the live-refresh timer if onStop() did not (e.g. the
	// screen is destroyed without ever being started). All other
	// children are parented to obj and freed by the base destructor.
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneAboutScreen::onStart() {
	Input::getInstance()->addListener(this);

	// Start the 1 Hz live-refresh tick. Idempotent: if a previous
	// onStart() left a timer running we reuse it rather than stacking
	// a second one. lv_timer_create pins user_data to `this` so the
	// static callback can route back to the correct instance.
	if(tickTimer == nullptr) {
		tickTimer = lv_timer_create(onTickTimer, kRefreshPeriodMs, this);
	}
	// Repaint immediately so the user does not see stale numbers if
	// onStart() is the first time the screen is touched after a long
	// detach.
	refreshLiveFields();
}

void PhoneAboutScreen::onStop() {
	Input::getInstance()->removeListener(this);
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

// ----- builders --------------------------------------------------------

void PhoneAboutScreen::buildCaption() {
	// "ABOUT" caption in pixelbasic7 cyan, just under the status bar -
	// same anchor pattern PhoneSettingsScreen uses for "SETTINGS",
	// PhoneCallHistory uses for "CALL HISTORY", etc.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "ABOUT");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneAboutScreen::buildBody() {
	// Helper-style local: build a single label+value group at the given
	// row index. Implemented inline rather than as a method to keep the
	// header uncluttered -- this is the only place that needs it.
	auto makePair = [this](lv_obj_t** outLabel, lv_obj_t** outValue,
						   const char* labelText, lv_coord_t rowIdx) {
		const lv_coord_t y = kGroupTopY + rowIdx * kGroupHeight;

		lv_obj_t* lab = lv_label_create(obj);
		lv_obj_set_style_text_font(lab, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lab, MP_LABEL_DIM, 0);
		lv_label_set_text(lab, labelText);
		lv_obj_set_pos(lab, kBodyX, y + kLabelOffset);

		lv_obj_t* val = lv_label_create(obj);
		lv_obj_set_style_text_font(val, &pixelbasic7, 0);
		lv_obj_set_style_text_color(val, MP_TEXT, 0);
		lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
		lv_obj_set_width(val, kBodyW);
		lv_label_set_text(val, "");
		lv_obj_set_pos(val, kBodyX, y + kValueOffset);

		*outLabel = lab;
		*outValue = val;
	};

	// Group 1: DEVICE ID -- never changes, written once.
	makePair(&deviceIdLabel, &deviceIdValue, "DEVICE ID", 0);
	{
		char buf[13];
		formatDeviceId((uint64_t) ESP.getEfuseMac(), buf, sizeof(buf));
		lv_label_set_text(deviceIdValue, buf);
	}

	// Group 2: FIRMWARE -- single static string, baked in at build time.
	makePair(&firmwareLabel, &firmwareValue, "FIRMWARE", 1);
	lv_label_set_text(firmwareValue, kFirmwareVersion);

	// Group 3: FREE HEAP / UPTIME -- live-updated. The label spans the
	// full row, but the value row is split into left (heap, 80 px) and
	// right (uptime, 72 px) so both live fields read together without
	// burning a separate row each.
	{
		const lv_coord_t y = kGroupTopY + 2 * kGroupHeight;

		runtimeLabel = lv_label_create(obj);
		lv_obj_set_style_text_font(runtimeLabel, &pixelbasic7, 0);
		lv_obj_set_style_text_color(runtimeLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(runtimeLabel, "FREE HEAP / UPTIME");
		lv_obj_set_pos(runtimeLabel, kBodyX, y + kLabelOffset);

		heapValue = lv_label_create(obj);
		lv_obj_set_style_text_font(heapValue, &pixelbasic7, 0);
		lv_obj_set_style_text_color(heapValue, MP_TEXT, 0);
		lv_label_set_text(heapValue, "");
		lv_obj_set_pos(heapValue, kBodyX, y + kValueOffset);

		uptimeValue = lv_label_create(obj);
		lv_obj_set_style_text_font(uptimeValue, &pixelbasic7, 0);
		lv_obj_set_style_text_color(uptimeValue, MP_TEXT, 0);
		lv_label_set_text(uptimeValue, "");
		// Right-anchored uptime so the HH:MM:SS column has a stable
		// right edge regardless of heap-readout width.
		lv_obj_set_pos(uptimeValue, kBodyX + 88, y + kValueOffset);
	}

	// Group 4: PEERS -- recomputed only here. Storage.Friends.all() is
	// a non-trivial scan and the paired set rarely changes, so polling
	// it 1/sec for the live-refresh tick would be wasted work.
	makePair(&peersLabel, &peersValue, "PEERS", 3);
	{
		char buf[16];
		const uint16_t count = computePeerCount();
		// "1 paired" / "3 paired" -- pluralisation is the same word here
		// (no English '-s' suffix) so we keep the message single-form for
		// simplicity. A future i18n session can swap this for a localized
		// string without changing the layout.
		snprintf(buf, sizeof(buf), "%u paired", (unsigned) count);
		lv_label_set_text(peersValue, buf);
	}
}

// ----- live refresh ----------------------------------------------------

void PhoneAboutScreen::refreshLiveFields() {
	if(heapValue != nullptr) {
		char buf[12];
		formatHeapKB((uint32_t) ESP.getFreeHeap(), buf, sizeof(buf));
		lv_label_set_text(heapValue, buf);
	}
	if(uptimeValue != nullptr) {
		char buf[12];
		formatUptime((uint32_t) (millis() / 1000u), buf, sizeof(buf));
		lv_label_set_text(uptimeValue, buf);
	}
}

void PhoneAboutScreen::onTickTimer(lv_timer_t* timer) {
	auto* self = static_cast<PhoneAboutScreen*>(timer->user_data);
	if(self == nullptr) return;
	self->refreshLiveFields();
}

// ----- input -----------------------------------------------------------

void PhoneAboutScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_BACK:
		case BTN_ENTER:
			// Flash the BACK softkey for tactile feedback then pop. The
			// About page has no commit/discard distinction so ENTER
			// behaves the same as BACK -- a friendly second way out.
			if(softKeys) softKeys->flashRight();
			pop();
			break;
		default:
			break;
	}
}

// ----- formatters ------------------------------------------------------

void PhoneAboutScreen::formatDeviceId(uint64_t mac, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	// The efuse MAC is 48 bits. Print it as 12 uppercase hex digits with
	// no separators so the value reads as a single contiguous identifier
	// (matching the way every other peer-aware screen passes UID_t around).
	// Any unused upper bits of the uint64 are masked off explicitly so we
	// don't emit phantom leading digits if a future ESP-IDF build returns
	// a wider type.
	const uint64_t masked = mac & 0xFFFFFFFFFFFFULL;
	const uint32_t hi = (uint32_t) (masked >> 24);
	const uint32_t lo = (uint32_t) (masked & 0xFFFFFFULL);
	snprintf(out, outLen, "%06lX%06lX",
			 (unsigned long) hi, (unsigned long) lo);
}

void PhoneAboutScreen::formatUptime(uint32_t seconds, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	uint32_t capped = seconds;
	const uint32_t maxSec = 99u * 3600u + 59u * 60u + 59u;  // 99:59:59
	if(capped > maxSec) capped = maxSec;
	const uint32_t h = capped / 3600u;
	const uint32_t m = (capped / 60u) % 60u;
	const uint32_t s = capped % 60u;
	snprintf(out, outLen, "%02u:%02u:%02u",
			 (unsigned) h, (unsigned) m, (unsigned) s);
}

void PhoneAboutScreen::formatHeapKB(uint32_t bytes, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	// Round down so the readout never claims more memory than is actually
	// free. 1 KB = 1024 bytes per the ESP-IDF convention.
	const uint32_t kb = bytes / 1024u;
	snprintf(out, outLen, "%u KB", (unsigned) kb);
}

uint16_t PhoneAboutScreen::computePeerCount() {
	// Storage.Friends.all() always includes the device's own efuse MAC
	// as the first entry (a self-record, not a contact) -- match the
	// way InboxScreen + ProfileScreen strip the self entry so the peer
	// count reads as "people you've actually paired with".
	const auto frens = Storage.Friends.all();
	const size_t total = frens.size();
	if(total <= 1) return 0;
	const size_t peers = total - 1;
	if(peers > 0xFFFFu) return 0xFFFFu;
	return (uint16_t) peers;
}
