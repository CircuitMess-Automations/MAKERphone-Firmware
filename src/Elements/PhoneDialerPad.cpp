#include "PhoneDialerPad.h"

// Standard Sony-Ericsson / GSM ITU-T E.161 numpad keymap. Order is
// row-major so keys[0]='1', keys[1]='2'... keys[11]='#'. Letters match
// the canonical T9 mapping that Phase 5 multi-tap will rely on.
//
// '1' has no letters (some phones used '.,?!1' but we keep it clean for
// now), '0' has no letters (a few phones bind " 0+" - left out for the
// same reason), '*' and '#' have no letters. Anything we add later we
// can expose via PhoneDialerKey's letters caption with no API change.
namespace {
struct KeyDef {
	char        glyph;
	const char* letters;
};

constexpr KeyDef kKeymap[PhoneDialerPad::KeyCount] = {
		{ '1', nullptr },  { '2', "ABC" },  { '3', "DEF" },
		{ '4', "GHI"  },   { '5', "JKL" },  { '6', "MNO" },
		{ '7', "PQRS" },   { '8', "TUV" },  { '9', "WXYZ" },
		{ '*', nullptr },  { '0', nullptr },{ '#', nullptr }
};
} // namespace

PhoneDialerPad::PhoneDialerPad(lv_obj_t* parent) : LVObject(parent){
	buildContainer();
	populate();

	// Initial cursor on '5' (centre of the pad) so the user's first press
	// of an arrow key lands on a neighbouring key in any direction. Real
	// feature phones tend to start their dialer at the topmost key, but
	// since this pad will likely live behind a number-entry strip rather
	// than be the only widget on screen, centre-start feels nicer for
	// arrow-driven navigation.
	if(!keys.empty()){
		cursor = 4; // index of '5'
		keys[cursor]->setSelected(true);
	}
}

// ----- builders -----

void PhoneDialerPad::buildContainer(){
	lv_obj_set_size(obj, PadWidth, PadHeight);

	lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	lv_obj_set_style_pad_row(obj, KeyGapY, 0);
	lv_obj_set_style_pad_column(obj, KeyGapX, 0);
	lv_obj_set_style_pad_all(obj, ContainerPad, 0);

	// Transparent background + no border - the pad lives over whatever
	// wallpaper the host screen draws (PhoneSynthwaveBg, solid, etc.).
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);

	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneDialerPad::populate(){
	keys.reserve(KeyCount);
	for(uint8_t i = 0; i < KeyCount; ++i){
		const KeyDef& def = kKeymap[i];
		auto* key = new PhoneDialerKey(obj, def.glyph, def.letters);
		keys.push_back(key);
	}
}

// ----- public API -----

void PhoneDialerPad::setCursor(uint8_t index){
	if(keys.empty()) return;
	if(index >= keys.size()) return;
	if(index == cursor) return;

	const uint8_t prev = cursor;
	cursor = index;
	applySelection(prev, cursor);
	if(onSelChanged) onSelChanged(prev, cursor);
}

void PhoneDialerPad::moveCursor(int8_t dx, int8_t dy){
	if(keys.empty()) return;

	const uint8_t count = (uint8_t) keys.size();
	const uint8_t curCol = cursor % Cols;
	const uint8_t curRow = cursor / Cols;
	const uint8_t totalRows = Rows;

	int32_t newIndex = (int32_t) cursor;

	if(dx != 0){
		// Horizontal: wrap on the row by stepping flat-index but clamping
		// to the same row's column range. This avoids the surprising
		// "Right at end of row 0 lands on start of row 1" behaviour that
		// PhoneMenuGrid uses - on a numpad that would jump from '3' to '4'
		// on Right which feels wrong because '4' is also reachable via
		// Down. Keeping horizontal motion strictly within a row matches
		// what feature-phone dialers actually do.
		int32_t newCol = (int32_t) curCol + dx;
		if(wrap){
			newCol = ((newCol % (int32_t) Cols) + (int32_t) Cols) % (int32_t) Cols;
		}else{
			if(newCol < 0) newCol = 0;
			if(newCol >= (int32_t) Cols) newCol = Cols - 1;
		}
		newIndex = (int32_t) curRow * (int32_t) Cols + newCol;
	}

	if(dy != 0){
		int32_t newRow = (int32_t) curRow + dy;

		if(wrap){
			newRow = ((newRow % (int32_t) totalRows) + (int32_t) totalRows) % (int32_t) totalRows;
		}else{
			if(newRow < 0) newRow = 0;
			if(newRow >= (int32_t) totalRows) newRow = totalRows - 1;
		}

		int32_t candidate = newRow * (int32_t) Cols + curCol;
		// The numpad is full (KeyCount == Cols*Rows) so no clamp-to-last
		// case here, but we keep the bounds-check defensive in case a
		// future variant ships with fewer keys.
		if(candidate >= (int32_t) count) candidate = count - 1;
		newIndex = candidate;
	}

	if(newIndex == (int32_t) cursor) return;

	const uint8_t prev = cursor;
	cursor = (uint8_t) newIndex;
	applySelection(prev, cursor);
	if(onSelChanged) onSelChanged(prev, cursor);
}

char PhoneDialerPad::getSelectedGlyph() const {
	if(keys.empty()) return '\0';
	return keys[cursor]->getGlyph();
}

const char* PhoneDialerPad::getSelectedLetters() const {
	if(keys.empty()) return "";
	return keys[cursor]->getLetters();
}

PhoneDialerKey* PhoneDialerPad::getSelectedKey() const {
	if(keys.empty()) return nullptr;
	return keys[cursor];
}

void PhoneDialerPad::pressSelected(){
	if(keys.empty()) return;
	auto* key = keys[cursor];
	key->pressFlash();
	if(onPress) onPress(key->getGlyph(), key->getLetters());
}

bool PhoneDialerPad::pressGlyph(char glyph){
	for(uint8_t i = 0; i < keys.size(); ++i){
		if(keys[i]->getGlyph() == glyph){
			// Move the cursor onto the matched key so subsequent arrow
			// navigation starts from where the user just pressed - this
			// matches how every feature phone behaves when you press a
			// numpad key while the cursor was elsewhere.
			setCursor(i);
			keys[i]->pressFlash();
			if(onPress) onPress(keys[i]->getGlyph(), keys[i]->getLetters());
			return true;
		}
	}
	return false;
}

// ----- internal -----

void PhoneDialerPad::applySelection(uint8_t prev, uint8_t curr){
	if(prev < keys.size())  keys[prev]->setSelected(false);
	if(curr < keys.size())  keys[curr]->setSelected(true);
}
