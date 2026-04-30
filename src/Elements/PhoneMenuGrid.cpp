#include "PhoneMenuGrid.h"

PhoneMenuGrid::PhoneMenuGrid(lv_obj_t* parent, const std::vector<Entry>& entries, uint8_t cols)
		: LVObject(parent), cols(cols == 0 ? 1 : cols){

	buildContainer();
	populate(entries);

	// Initial selection: first tile gets the halo pulse so the user can
	// see at-a-glance where the cursor lives. setCursor() does NOT fire
	// the onSelChanged callback for this initial paint - host wires up
	// its callback AFTER construction anyway.
	if(!tiles.empty()){
		cursor = 0;
		tiles[0]->setSelected(true);
	}
}

void PhoneMenuGrid::buildContainer(){
	// Container is exactly wide enough to fit `cols` tiles plus their
	// horizontal gaps and the outer padding on either side. Height is
	// content-sized so the grid grows as rows are added.
	const uint16_t containerW =
			(uint16_t) cols * TileW
			+ (uint16_t) (cols - 1) * TileGapX
			+ 2 * ContainerPad;

	lv_obj_set_size(obj, containerW, LV_SIZE_CONTENT);

	lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	lv_obj_set_style_pad_row(obj, TileGapY, 0);
	lv_obj_set_style_pad_column(obj, TileGapX, 0);
	lv_obj_set_style_pad_all(obj, ContainerPad, 0);

	// Transparent background + no border - the grid is meant to overlay
	// any wallpaper (PhoneSynthwaveBg, solid color, etc.) without painting
	// over it. The host screen owns the background.
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);

	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneMenuGrid::populate(const std::vector<Entry>& entries){
	tiles.reserve(entries.size());
	for(const Entry& e : entries){
		auto* tile = new PhoneIconTile(obj, e.icon, e.label);
		tiles.push_back(tile);
	}
}

uint8_t PhoneMenuGrid::getRows() const {
	const uint8_t count = (uint8_t) tiles.size();
	if(count == 0 || cols == 0) return 0;
	return (uint8_t) ((count + cols - 1) / cols);
}

void PhoneMenuGrid::setCursor(uint8_t index){
	if(tiles.empty()) return;
	if(index >= tiles.size()) return;
	if(index == cursor) return;

	const uint8_t prev = cursor;
	cursor = index;
	applySelection(prev, cursor);
	if(onSelChanged) onSelChanged(prev, cursor);
}

void PhoneMenuGrid::moveCursor(int8_t dx, int8_t dy){
	if(tiles.empty()) return;

	const uint8_t count = (uint8_t) tiles.size();
	const uint8_t curCol = cursor % cols;
	const uint8_t curRow = cursor / cols;
	const uint8_t totalRows = getRows();

	// Treat horizontal movement on the count line: a flat-index delta of
	// dx wrapping at `count`. This makes Right at the end of a row land
	// on the start of the NEXT row (matching feature-phone behaviour) and
	// Right at the very last item land back at index 0 if wrap is on.
	int32_t newIndex = (int32_t) cursor;

	if(dx != 0){
		newIndex = (int32_t) cursor + dx;
		if(wrap){
			// Modulo on `count`, normalised to a positive remainder.
			newIndex = ((newIndex % (int32_t) count) + (int32_t) count) % (int32_t) count;
		}else{
			if(newIndex < 0) newIndex = 0;
			if(newIndex >= (int32_t) count) newIndex = count - 1;
		}
	}

	if(dy != 0){
		// Vertical movement is by `cols` per row. Wrap-around lands on
		// the same column of the wrapped row; if that cell is empty
		// (last row underfilled) we step the cursor up to the last
		// available item in that column - feels more natural than
		// landing on void.
		int32_t newRow = (int32_t) curRow + dy;

		if(wrap){
			newRow = ((newRow % (int32_t) totalRows) + (int32_t) totalRows) % (int32_t) totalRows;
		}else{
			if(newRow < 0) newRow = 0;
			if(newRow >= (int32_t) totalRows) newRow = totalRows - 1;
		}

		int32_t candidate = newRow * (int32_t) cols + curCol;
		if(candidate >= (int32_t) count){
			// Underfilled wrapped row - clamp to last tile, which is the
			// rightmost item of the (incomplete) last row.
			candidate = count - 1;
		}
		newIndex = candidate;
	}

	if(newIndex == (int32_t) cursor) return;

	const uint8_t prev = cursor;
	cursor = (uint8_t) newIndex;
	applySelection(prev, cursor);
	if(onSelChanged) onSelChanged(prev, cursor);
}

void PhoneMenuGrid::applySelection(uint8_t prev, uint8_t curr){
	if(prev < tiles.size())  tiles[prev]->setSelected(false);
	if(curr < tiles.size())  tiles[curr]->setSelected(true);
}

PhoneIconTile::Icon PhoneMenuGrid::getSelectedIcon() const {
	// Default to Phone if the grid is empty - same icon as the first roadmap
	// app, so callers that forget to range-check their input get a sane
	// fallback rather than an out-of-bounds read.
	if(tiles.empty()) return PhoneIconTile::Icon::Phone;
	return tiles[cursor]->getIcon();
}

PhoneIconTile* PhoneMenuGrid::getSelectedTile() const {
	if(tiles.empty()) return nullptr;
	return tiles[cursor];
}
