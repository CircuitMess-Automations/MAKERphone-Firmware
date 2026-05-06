#include "PhoneOwnerEmoji.h"

namespace {

// 9-bit row helper. The constants below read top-to-bottom so the
// glyph is visible in the source; bit 8 (0x100) is the leftmost
// column, bit 0 (0x001) is the rightmost. Each catalogue entry
// fits in a 9x9 grid -- the upper 7 bits of every uint16_t row
// are unused but preserved so the literal stays a single uint16_t
// rather than a packed bitfield.

const PhoneOwnerEmoji::Glyph kGlyphs[] = {
	// 0 - None: reserved factory default. Never painted; the renderer
	// special-cases idx == 0 via PhoneOwnerEmoji::isVisible() so an
	// all-zeros row is a perfectly valid representation here.
	{
		"NONE",
		{
			0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000,
		},
	},

	// 1 - Heart. Bilateral symmetry around column 4; the top two rows
	// carry the twin lobes, the bottom rows taper to a point at row 7.
	{
		"HEART",
		{
			0x0C6, 0x1EF, 0x1FF, 0x1FF, 0x0FE,
			0x07C, 0x038, 0x010, 0x000,
		},
	},

	// 2 - Star. Five-point with a flat top spike, drawn fat enough to
	// stay readable at 1:1 cell scale.
	{
		"STAR",
		{
			0x010, 0x010, 0x038, 0x1FF, 0x0FE,
			0x07C, 0x06C, 0x0C6, 0x183,
		},
	},

	// 3 - Smile. Round face with two vertical eye stripes and a
	// curved mouth dropping from row 5.
	{
		"SMILE",
		{
			0x07C, 0x082, 0x129, 0x129, 0x101,
			0x145, 0x139, 0x082, 0x07C,
		},
	},

	// 4 - Music. Eighth-note: stem on column 6, flag at the top right,
	// note head at the bottom left.
	{
		"MUSIC",
		{
			0x07C, 0x024, 0x024, 0x024, 0x024,
			0x024, 0x0E4, 0x1E4, 0x0C0,
		},
	},

	// 5 - Crown. Three spikes, jewelled band, flat base.
	{
		"CROWN",
		{
			0x129, 0x155, 0x1FF, 0x129, 0x129,
			0x101, 0x1FF, 0x000, 0x000,
		},
	},

	// 6 - Skull. Classic Sony-Ericsson missed-call icon: round dome,
	// hollow eye sockets, gridded teeth.
	{
		"SKULL",
		{
			0x07C, 0x082, 0x101, 0x145, 0x145,
			0x111, 0x1FF, 0x0AA, 0x0AA,
		},
	},

	// 7 - Bolt. Diagonal lightning streak from the upper right to the
	// lower left, with a soft kink at the middle.
	{
		"BOLT",
		{
			0x01C, 0x038, 0x070, 0x0E0, 0x1FC,
			0x038, 0x070, 0x0C0, 0x100,
		},
	},

	// 8 - Cat face. Pointed ears, eyes, whisker stripes, tapered chin.
	{
		"CAT",
		{
			0x183, 0x1C7, 0x17D, 0x101, 0x145,
			0x111, 0x145, 0x139, 0x0C6,
		},
	},

	// 9 - Coffee. Steam wisp curling up the left side, mug body with
	// a one-cell handle on the right.
	{
		"COFFEE",
		{
			0x040, 0x020, 0x040, 0x000, 0x1FE,
			0x103, 0x102, 0x103, 0x0FC,
		},
	},

	// 10 - Pizza. Triangle tapering up from the bottom edge with two
	// pepperoni dots punched out of the cheese.
	{
		"PIZZA",
		{
			0x010, 0x038, 0x028, 0x07C, 0x054,
			0x0FE, 0x0D6, 0x1FF, 0x1FF,
		},
	},

	// 11 - Dice. Square outline with five pips arranged in the
	// classic d6 "5" face pattern.
	{
		"DICE",
		{
			0x1FF, 0x101, 0x145, 0x111, 0x101,
			0x111, 0x145, 0x101, 0x1FF,
		},
	},

	// 12 - Rocket. Pointed nose cone, broadening fuselage, two fins,
	// twin exhaust trails at the bottom.
	{
		"ROCKET",
		{
			0x010, 0x038, 0x038, 0x07C, 0x07C,
			0x0BA, 0x139, 0x07C, 0x054,
		},
	},
};

constexpr uint8_t kGlyphCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

} // namespace

namespace PhoneOwnerEmoji {

uint8_t count() {
	return kGlyphCount;
}

uint8_t clampedId(uint8_t b) {
	if(b >= kGlyphCount) return 0;
	return b;
}

bool isVisible(uint8_t idx) {
	return clampedId(idx) != 0;
}

const Glyph& at(uint8_t idx) {
	return kGlyphs[clampedId(idx)];
}

bool pixelAt(uint8_t idx, uint8_t x, uint8_t y) {
	if(x >= Width)  return false;
	if(y >= Height) return false;
	const Glyph& g = at(idx);
	// Bit 8 = leftmost column. Convert (x) to the matching bit
	// position so the binary literal in the catalogue reads the way
	// the glyph is drawn in source.
	const uint8_t bit  = (Width - 1) - x;
	return (g.rows[y] >> bit) & 0x1;
}

} // namespace PhoneOwnerEmoji
