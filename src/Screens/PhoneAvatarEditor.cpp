#include "PhoneAvatarEditor.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget
// so the editor reads visually as part of the same family.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// 160 x 128 layout. Vertical budget:
//   y =   0..  9  PhoneStatusBar (10 px)
//   y =  11.. 18  caption "AVATAR EDITOR" (pixelbasic7 cyan)
//   y =  20..115  paint surface (96x96 centred near the left edge)
//   y = 116..125  coordinate readout (pixelbasic7)
//   y = 118..127  PhoneSoftKeyBar (10 px) - sits on top of the
//                 readout's vertical band (the readout is offset
//                 sideways so the softkey labels still read).
//
// The 96x96 paint surface lives at x = 12..107 and the palette swatch
// strip at x = 124..152 leaves enough slack on either side to show
// the cursor frame without clipping. The readout label is parked at
// x = 24, y = 116 so it tucks above the SAVE / BACK soft-keys.

static constexpr lv_coord_t kCaptionY    = 12;
static constexpr lv_coord_t kCanvasX     = 12;
static constexpr lv_coord_t kCanvasY     = 22;
static constexpr lv_coord_t kCellPx      = 3;
static constexpr lv_coord_t kCanvasPxW   = PhoneAvatarEditor::CanvasW * kCellPx; // 96
static constexpr lv_coord_t kCanvasPxH   = PhoneAvatarEditor::CanvasH * kCellPx; // 96
static constexpr lv_coord_t kPaletteX    = 124;
static constexpr lv_coord_t kPaletteY0   = 26;
static constexpr lv_coord_t kSwatchSize  = 14;
static constexpr lv_coord_t kSwatchGap   = 4;
static constexpr lv_coord_t kReadoutX    = 4;
static constexpr lv_coord_t kReadoutY    = 119;

static constexpr uint32_t kBackHoldMs = 600;

// Cache of saved bitmaps. Capacity is small on purpose -- the
// editor is intentionally a "favourite friends only" surface, and
// the cache slot count maps comfortably onto the homescreen
// speed-dial pool. The slots are flat-arrays (not std::map) so they
// link cleanly under the Arduino target without dragging in
// libstdc++ container code.
struct AvatarSlot {
	UID_t   uid    = 0;
	bool    used   = false;
	uint8_t packed[256] = {0};
};
static AvatarSlot g_avatarCache[PhoneAvatarEditor::MaxCachedAvatars];

// Pack a row-major CanvasH * CanvasW grid of palette indices into a
// 256-byte buffer (2 bits per cell, MSB-first inside each byte).
// 32 * 32 = 1024 cells * 2 bits = 2048 bits = 256 bytes.
static void packBitmap(const uint8_t (&grid)[PhoneAvatarEditor::CanvasH][PhoneAvatarEditor::CanvasW],
		uint8_t* out256) {
	memset(out256, 0, 256);
	uint16_t bitIdx = 0;
	for(uint8_t y = 0; y < PhoneAvatarEditor::CanvasH; ++y) {
		for(uint8_t x = 0; x < PhoneAvatarEditor::CanvasW; ++x) {
			const uint8_t v = grid[y][x] & 0x03;
			const uint16_t byteIdx = bitIdx >> 3;
			const uint8_t  shift   = 6 - (uint8_t)(bitIdx & 7);
			out256[byteIdx] |= (uint8_t)(v << shift);
			bitIdx += 2;
		}
	}
}

// Inverse of packBitmap.
static void unpackBitmap(const uint8_t* in256,
		uint8_t (&grid)[PhoneAvatarEditor::CanvasH][PhoneAvatarEditor::CanvasW]) {
	uint16_t bitIdx = 0;
	for(uint8_t y = 0; y < PhoneAvatarEditor::CanvasH; ++y) {
		for(uint8_t x = 0; x < PhoneAvatarEditor::CanvasW; ++x) {
			const uint16_t byteIdx = bitIdx >> 3;
			const uint8_t  shift   = 6 - (uint8_t)(bitIdx & 7);
			grid[y][x] = (uint8_t)((in256[byteIdx] >> shift) & 0x03);
			bitIdx += 2;
		}
	}
}

// Reserve / locate a cache slot for `uid`. Returns nullptr only when
// uid == 0; non-zero uids that miss the cache claim the first
// !used slot, falling back to slot 0 (overwriting it) once the
// cache is full. The eviction is intentionally simple -- the cache
// is so small that picking a smarter policy buys nothing.
static AvatarSlot* claimSlot(UID_t uid) {
	if(uid == 0) return nullptr;
	for(uint8_t i = 0; i < PhoneAvatarEditor::MaxCachedAvatars; ++i) {
		if(g_avatarCache[i].used && g_avatarCache[i].uid == uid) {
			return &g_avatarCache[i];
		}
	}
	for(uint8_t i = 0; i < PhoneAvatarEditor::MaxCachedAvatars; ++i) {
		if(!g_avatarCache[i].used) {
			g_avatarCache[i].uid  = uid;
			g_avatarCache[i].used = true;
			memset(g_avatarCache[i].packed, 0, 256);
			return &g_avatarCache[i];
		}
	}
	g_avatarCache[0].uid  = uid;
	g_avatarCache[0].used = true;
	memset(g_avatarCache[0].packed, 0, 256);
	return &g_avatarCache[0];
}

static AvatarSlot* findSlot(UID_t uid) {
	if(uid == 0) return nullptr;
	for(uint8_t i = 0; i < PhoneAvatarEditor::MaxCachedAvatars; ++i) {
		if(g_avatarCache[i].used && g_avatarCache[i].uid == uid) {
			return &g_avatarCache[i];
		}
	}
	return nullptr;
}

// ----- ctor / dtor -----

PhoneAvatarEditor::PhoneAvatarEditor(UID_t inUid)
		: LVScreen(),
		  uid(inUid) {
	// Pre-load any cached bitmap for this uid so re-opening the
	// editor for the same contact restarts on the saved state.
	if(uid != 0) {
		const AvatarSlot* slot = findSlot(uid);
		if(slot != nullptr) {
			unpackBitmap(slot->packed, bitmap);
		}
	}
	buildLayout();
}

PhoneAvatarEditor::~PhoneAvatarEditor() {
	// All children are parented to obj; LVScreen's destructor frees
	// obj and LVGL recursively frees the per-cell rectangles. The
	// per-uid cache is global and intentionally outlives the screen.
}

void PhoneAvatarEditor::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneAvatarEditor::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- public API -----

void PhoneAvatarEditor::setOnSave(ActionHandler cb) { saveCb = cb; }
void PhoneAvatarEditor::setOnBack(ActionHandler cb) { backCb = cb; }

void PhoneAvatarEditor::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneAvatarEditor::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

const uint8_t* PhoneAvatarEditor::findSavedBitmap(UID_t uid) {
	const AvatarSlot* slot = findSlot(uid);
	return slot ? slot->packed : nullptr;
}

// ----- builders -----

void PhoneAvatarEditor::buildLayout() {
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	// Caption "AVATAR EDITOR" beneath the status bar so the screen
	// identity is legible even before any pixel is painted.
	captionLbl = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLbl, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLbl, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLbl, "AVATAR EDITOR");
	lv_obj_set_align(captionLbl, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLbl, kCaptionY);

	buildCanvas();
	buildPalette();
	buildReadout();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("SAVE");
	softKeys->setRight("BACK");

	setButtonHoldTime(BTN_BACK, kBackHoldMs);

	// If the cache had a saved bitmap, repaint the cells that came
	// from it now that the grid container exists. (When uid was 0
	// or no cache hit, every bitmap[][] entry is 0 and this loop is
	// a fast no-op.)
	for(uint8_t y = 0; y < CanvasH; ++y) {
		for(uint8_t x = 0; x < CanvasW; ++x) {
			if(bitmap[y][x] != 0) {
				const uint8_t v = bitmap[y][x];
				bitmap[y][x] = 0; // reset so paintCell creates a fresh node
				paintCell(x, y, v);
			}
		}
	}

	refreshPalette();
	refreshCursor();
	refreshReadout();
}

void PhoneAvatarEditor::buildCanvas() {
	// Frame: a 1 px MP_HIGHLIGHT-bordered slab containing the grid
	// holder. The cursor is a sibling of the grid (rendered above)
	// so it never has to dodge the lazily-created cell rectangles.
	frame = lv_obj_create(obj);
	lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(frame, 0, 0);
	lv_obj_set_style_radius(frame, 1, 0);
	lv_obj_set_style_border_width(frame, 1, 0);
	lv_obj_set_style_border_color(frame, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(frame, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
	lv_obj_set_size(frame, kCanvasPxW + 2, kCanvasPxH + 2);
	lv_obj_set_pos(frame, kCanvasX - 1, kCanvasY - 1);

	gridHolder = lv_obj_create(frame);
	lv_obj_clear_flag(gridHolder, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(gridHolder, 0, 0);
	lv_obj_set_style_radius(gridHolder, 0, 0);
	lv_obj_set_style_border_width(gridHolder, 0, 0);
	lv_obj_set_style_bg_opa(gridHolder, LV_OPA_TRANSP, 0);
	lv_obj_set_size(gridHolder, kCanvasPxW, kCanvasPxH);
	lv_obj_set_pos(gridHolder, 0, 0);

	cursor = lv_obj_create(frame);
	lv_obj_clear_flag(cursor, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(cursor, 0, 0);
	lv_obj_set_style_radius(cursor, 0, 0);
	lv_obj_set_style_border_width(cursor, 1, 0);
	lv_obj_set_style_border_color(cursor, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(cursor, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_opa(cursor, LV_OPA_TRANSP, 0);
	lv_obj_set_size(cursor, kCellPx + 2, kCellPx + 2);
}

void PhoneAvatarEditor::buildPalette() {
	for(uint8_t i = 0; i < PaletteSize; ++i) {
		lv_obj_t* sw = lv_obj_create(obj);
		lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_pad_all(sw, 0, 0);
		lv_obj_set_style_radius(sw, 1, 0);
		lv_obj_set_style_border_width(sw, 1, 0);
		lv_obj_set_style_border_color(sw, MP_DIM, 0);
		lv_obj_set_style_border_opa(sw, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(sw, paletteColor(i), 0);
		lv_obj_set_style_bg_opa(sw, (i == 0) ? LV_OPA_30 : LV_OPA_COVER, 0);
		lv_obj_set_size(sw, kSwatchSize, kSwatchSize);
		lv_obj_set_pos(sw, kPaletteX, kPaletteY0 + i * (kSwatchSize + kSwatchGap));
		swatches[i] = sw;
	}
}

void PhoneAvatarEditor::buildReadout() {
	readoutLbl = lv_label_create(obj);
	lv_obj_set_style_text_font(readoutLbl, &pixelbasic7, 0);
	lv_obj_set_style_text_color(readoutLbl, MP_LABEL_DIM, 0);
	lv_label_set_text(readoutLbl, "");
	lv_obj_set_pos(readoutLbl, kReadoutX, kReadoutY);
}

// ----- drawing -----

void PhoneAvatarEditor::paintCell(uint8_t x, uint8_t y, uint8_t colorIndex) {
	if(x >= CanvasW || y >= CanvasH) return;
	colorIndex &= 0x03;

	if(colorIndex == 0) {
		// Erase: drop the lv_obj if it exists and clear the bitmap.
		if(cells[y][x] != nullptr) {
			lv_obj_del(cells[y][x]);
			cells[y][x] = nullptr;
		}
		bitmap[y][x] = 0;
		return;
	}

	bitmap[y][x] = colorIndex;
	if(cells[y][x] == nullptr) {
		lv_obj_t* px = lv_obj_create(gridHolder);
		lv_obj_clear_flag(px, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(px, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_pad_all(px, 0, 0);
		lv_obj_set_style_radius(px, 0, 0);
		lv_obj_set_style_border_width(px, 0, 0);
		lv_obj_set_style_bg_opa(px, LV_OPA_COVER, 0);
		lv_obj_set_size(px, kCellPx, kCellPx);
		lv_obj_set_pos(px, x * kCellPx, y * kCellPx);
		cells[y][x] = px;
	}
	lv_obj_set_style_bg_color(cells[y][x], paletteColor(colorIndex), 0);
}

void PhoneAvatarEditor::clearAllCells() {
	for(uint8_t y = 0; y < CanvasH; ++y) {
		for(uint8_t x = 0; x < CanvasW; ++x) {
			if(cells[y][x] != nullptr) {
				lv_obj_del(cells[y][x]);
				cells[y][x] = nullptr;
			}
			bitmap[y][x] = 0;
		}
	}
}

void PhoneAvatarEditor::seedFromAvatarHash() {
	// Trace-over stencil. Drops a tiny deterministic head-and-shoulders
	// pattern derived from `uid` so the user has somewhere to start
	// instead of an empty grid. The shape is intentionally generic --
	// the user is meant to overpaint it. We pick an oval head and a
	// shoulder line in palette index 1 (cream) and dot eyes + mouth in
	// palette index 3 (orange accent) so the seed reads as a face on
	// every theme.
	clearAllCells();
	const uint8_t hash = (uint8_t)((uid * 131u) ^ 0xA5u);

	// Head oval: 18 wide, 22 tall, centred around (16, 14).
	const int8_t cx = 16;
	const int8_t cy = 14;
	const int8_t rx = 8;
	const int8_t ry = 11;
	for(int8_t y = cy - ry; y <= cy + ry; ++y) {
		for(int8_t x = cx - rx; x <= cx + rx; ++x) {
			if(x < 0 || y < 0 || x >= CanvasW || y >= CanvasH) continue;
			const int32_t dx = x - cx;
			const int32_t dy = y - cy;
			const int32_t v  = dx * dx * (ry * ry) + dy * dy * (rx * rx);
			if(v <= rx * rx * ry * ry) {
				paintCell((uint8_t) x, (uint8_t) y, 1);
			}
		}
	}

	// Eyes + mouth (orange accent).
	const uint8_t eyeY = 13;
	paintCell(13, eyeY, 3);
	paintCell(19, eyeY, 3);
	paintCell(14, 19, 3);
	paintCell(15, 19, 3);
	paintCell(16, 19, 3);
	paintCell(17, 19, 3);
	paintCell(18, 19, 3);

	// Shoulders: a flat band along y=27..29.
	for(uint8_t x = 4; x < 28; ++x) {
		paintCell(x, 27, 2);
		paintCell(x, 28, 2);
	}
	(void) hash; // retained for future seed-driven variants.
}

// ----- input model -----

void PhoneAvatarEditor::moveCursor(int8_t dx, int8_t dy) {
	int16_t nx = (int16_t) cursorX + dx;
	int16_t ny = (int16_t) cursorY + dy;
	if(nx < 0) nx = 0;
	if(ny < 0) ny = 0;
	if(nx >= (int16_t) CanvasW) nx = CanvasW - 1;
	if(ny >= (int16_t) CanvasH) ny = CanvasH - 1;
	cursorX = (uint8_t) nx;
	cursorY = (uint8_t) ny;
	refreshCursor();
	refreshReadout();
}

void PhoneAvatarEditor::cyclePalette(int8_t direction) {
	int16_t next = (int16_t) activeColor + direction;
	while(next < 0) next += PaletteSize;
	while(next >= (int16_t) PaletteSize) next -= PaletteSize;
	activeColor = (uint8_t) next;
	refreshPalette();
	refreshReadout();
}

void PhoneAvatarEditor::refreshCursor() {
	if(cursor == nullptr) return;
	lv_obj_set_pos(cursor,
				   cursorX * kCellPx - 1,
				   cursorY * kCellPx - 1);
}

void PhoneAvatarEditor::refreshPalette() {
	for(uint8_t i = 0; i < PaletteSize; ++i) {
		if(swatches[i] == nullptr) continue;
		const bool active = (i == activeColor);
		lv_obj_set_style_border_color(swatches[i],
									  active ? MP_ACCENT : MP_DIM, 0);
		lv_obj_set_style_border_width(swatches[i],
									  active ? 2 : 1, 0);
	}
}

void PhoneAvatarEditor::refreshReadout() {
	if(readoutLbl == nullptr) return;
	char buf[40];
	snprintf(buf, sizeof(buf), "%2u,%2u  %s",
			 (unsigned) cursorX,
			 (unsigned) cursorY,
			 paletteName(activeColor));
	lv_label_set_text(readoutLbl, buf);
}

void PhoneAvatarEditor::invokeSave() {
	if(softKeys) softKeys->flashLeft();

	// Pack into the per-uid cache when we have a real uid. Scratch
	// mode (uid == 0) skips the cache write but still fires the
	// caller-supplied callback so a host can route the save to its
	// own destination.
	if(uid != 0) {
		AvatarSlot* slot = claimSlot(uid);
		if(slot != nullptr) {
			packBitmap(bitmap, slot->packed);
		}
	}

	if(saveCb) {
		saveCb(this);
	} else {
		pop();
	}
}

// ----- buttons -----

void PhoneAvatarEditor::buttonPressed(uint i) {
	switch(i) {
		case BTN_2: moveCursor( 0, -1); break;
		case BTN_8: moveCursor( 0, +1); break;
		case BTN_4: moveCursor(-1,  0); break;
		case BTN_6: moveCursor(+1,  0); break;

		case BTN_5:
		case BTN_ENTER:
			paintCell(cursorX, cursorY, activeColor);
			break;

		case BTN_0:
			paintCell(cursorX, cursorY, 0);
			break;

		case BTN_L:
			cyclePalette(-1);
			break;

		case BTN_R:
			cyclePalette(+1);
			break;

		case BTN_1:
			clearAllCells();
			break;

		case BTN_3:
			seedFromAvatarHash();
			break;

		case BTN_LEFT:
			invokeSave();
			break;

		case BTN_RIGHT:
			// Defer the short-press behaviour to buttonReleased so a
			// hold can pre-empt it.
			backHoldFired = false;
			break;

		case BTN_BACK:
			backHoldFired = false;
			break;

		default:
			break;
	}
}

void PhoneAvatarEditor::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		backHoldFired = true;
		if(softKeys) softKeys->flashRight();
		if(backCb) {
			backCb(this);
		} else {
			pop();
		}
	}
}

void PhoneAvatarEditor::buttonReleased(uint i) {
	if(i == BTN_BACK || i == BTN_RIGHT) {
		if(backHoldFired) return;
		if(softKeys) softKeys->flashRight();
		if(backCb) {
			backCb(this);
		} else {
			pop();
		}
	}
}

// ----- palette helpers -----

lv_color_t PhoneAvatarEditor::paletteColor(uint8_t index) {
	switch(index & 0x03) {
		case 0:  return MP_DIM;       // erase / transparent (rendered dim in the swatch only)
		case 1:  return MP_TEXT;      // cream pen
		case 2:  return MP_HIGHLIGHT; // cyan pen
		case 3:  return MP_ACCENT;    // sunset orange pen
		default: return MP_TEXT;
	}
}

const char* PhoneAvatarEditor::paletteName(uint8_t index) {
	switch(index & 0x03) {
		case 0:  return "ERASE";
		case 1:  return "CREAM";
		case 2:  return "CYAN";
		case 3:  return "ORANGE";
		default: return "PEN";
	}
}
