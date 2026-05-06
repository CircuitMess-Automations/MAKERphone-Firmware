#ifndef MAKERPHONE_PHONEOWNEREMOJI_H
#define MAKERPHONE_PHONEOWNEREMOJI_H

#include <Arduino.h>

/**
 * PhoneOwnerEmoji (S188)
 *
 * Curated catalogue of small 9x9 pixel-art owner-identity glyphs that
 * the LockScreen can paint just under the status bar (next to any
 * Settings.ownerName text) so the device wears a personal "this is who
 * I am" icon every time the user wakes it. The catalogue is indexed by
 * the new Settings.ownerEmoji byte (S188); index 0 is reserved as
 * "None" so a freshly-flashed device does not paint a stray glyph
 * before the user has touched the picker.
 *
 * Each glyph is a 9x9 1-bpp grid stored as nine uint16_t rows where
 * bit 8 is the leftmost column (x = 0) and bit 0 is the rightmost
 * (x = 8). Pixels are rendered at one device-pixel-per-cell on the
 * lock screen so the on-screen footprint matches the literal 9x9
 * cell budget; the picker preview rescales the same data to a 4x
 * cell size for a chunky 36x36 px readout. The catalogue is plain-
 * old-data so the entire blob lives in flash -- adding entries does
 * not grow the SettingsData NVS slot.
 *
 * The catalogue intentionally stays small (13 entries today) so the
 * PhoneOwnerEmojiScreen pager wraps cleanly inside the 160x128
 * window and the Settings.ownerEmoji byte never approaches its
 * 256-value capacity. Future MAKERphone runs are free to append
 * entries at the bottom of the table; existing persisted values
 * keep pointing at the same glyph because every entry's index is
 * stable for the lifetime of the catalogue.
 *
 *   index  name      meaning
 *   ----------------------------------------------------------
 *     0    None      blank (factory default; not painted)
 *     1    Heart     classic feature-phone affection icon
 *     2    Star      five-point star
 *     3    Smile     happy-face round emoji
 *     4    Music     eighth-note flag
 *     5    Crown     three-spike crown with band
 *     6    Skull     classic Sony-Ericsson "missed call" skull
 *     7    Bolt      lightning streak
 *     8    Cat       cat face with whiskers + ears
 *     9    Coffee    steaming mug with handle
 *    10    Pizza     pizza slice with pepperoni
 *    11    Dice      die showing the 5 face
 *    12    Rocket    pointed rocket with exhaust flame
 */
namespace PhoneOwnerEmoji {

	/** Pixel footprint shared by every glyph in the catalogue. */
	static constexpr uint8_t Width  = 9;
	static constexpr uint8_t Height = 9;

	/** Stable in-flash entry. Bit 8 of `rows[y]` is the leftmost
	 *  column (x = 0) so the binary literal reads the way the glyph
	 *  is drawn in the source. */
	struct Glyph {
		const char* name;
		uint16_t    rows[Height];
	};

	/** Total number of catalogue entries (including index 0 = None). */
	uint8_t count();

	/** Defensive: clamp any byte to a valid catalogue index (0..count-1).
	 *  Used by every reader of Settings.ownerEmoji so a corrupt persisted
	 *  byte degrades gracefully to None / a safe in-range glyph rather
	 *  than walking past the end of the table. */
	uint8_t clampedId(uint8_t b);

	/** Returns true when the index points at a paintable glyph (i.e.
	 *  not the reserved None slot). Saves callers from open-coding the
	 *  `idx != 0` check. */
	bool isVisible(uint8_t idx);

	/** Returns the catalogue entry. Out-of-range indices clamp to 0
	 *  (None) so the caller can rely on the returned reference being
	 *  stable for the lifetime of the program. */
	const Glyph& at(uint8_t idx);

	/** Convenience reader: returns true when the (x,y) cell of the
	 *  glyph at `idx` is set. Out-of-range x / y / idx returns false
	 *  so a defensive caller can iterate over a fixed Width x Height
	 *  rectangle without needing to bounds-check the catalogue. */
	bool pixelAt(uint8_t idx, uint8_t x, uint8_t y);

}

#endif //MAKERPHONE_PHONEOWNEREMOJI_H
