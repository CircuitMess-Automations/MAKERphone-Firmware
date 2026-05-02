#ifndef CHATTER_FIRMWARE_PHONE_COMPOSER_RTTTL_H
#define CHATTER_FIRMWARE_PHONE_COMPOSER_RTTTL_H

#include <Arduino.h>
#include "../Screens/PhoneComposer.h"

/**
 * S122 — PhoneComposerRtttl
 *
 * RTTTL (Ring Tone Text Transfer Language) codec for the buffer of
 * PhoneComposer::Note values that S121 introduced. The codec is the
 * exchange format every later session will lean on:
 *
 *   S123 — save slots in flash + wire a converted Note stream to
 *          PhoneRingtoneEngine for live preview.
 *   S153 — per-contact custom ringtones (depends on the canonical
 *          string format being stable on disk).
 *   S160 — per-profile ringtone selection (same).
 *   S180 — per-friend custom ringtone (same).
 *   S193 — composer-fed buzzer alarm tone (same).
 *
 * RTTTL grammar (the subset we emit / accept):
 *
 *   <rtttl>    ::= [<name>] ":" [<defaults>] ":" <commands>
 *   <name>     ::= up to 10 visible chars, never contains a ':' or ','
 *   <defaults> ::= <setting> ("," <setting>)*
 *   <setting>  ::= ("d="|"o="|"b=") <decimal>
 *   <commands> ::= <command> ("," <command>)*
 *   <command>  ::= [<duration>] <tone> [<sharp>] [<octave>] [<dot>]
 *   <duration> ::= "1" | "2" | "4" | "8" | "16" | "32"
 *   <tone>     ::= "a".."g" | "p"
 *   <sharp>    ::= "#"
 *   <octave>   ::= "1".."9"     (RTTTL spec is 4..7 but the composer
 *                               itself allows 3..7, so the codec is
 *                               permissive and clamps on parse.)
 *   <dot>      ::= "."          (50 % length extension)
 *
 * The serializer always emits canonical
 * "[duration]<tone>[#][octave][.]" tokens; the parser also accepts
 * "[duration]<tone>[#][.][octave]" so RTTTL strings written by the
 * dot-before-octave dialect round-trip cleanly.
 *
 * Everything is self-contained -- no LVGL, no hardware, no global
 * state -- so a future test harness can exercise the codec against a
 * fixture table without booting the firmware.
 */
class PhoneComposerRtttl {
public:
	/** Standard RTTTL spec caps the name at 10 chars. We expose the
	 *  bound so callers can size their buffers consistently. */
	static constexpr uint8_t  MaxNameLen      = 10;

	/** Reasonable upper bound for the textual form of a 64-note
	 *  composition with worst-case "32a#5." tokens, header, and
	 *  separators. Callers can use this to size their out-buffers. */
	static constexpr size_t   SerializedCap   = 768;

	/** RTTTL "d=" default if the parser sees no override. */
	static constexpr uint8_t  DefaultDuration = 4;

	/** RTTTL "o=" default if the parser sees no override. */
	static constexpr uint8_t  DefaultOctave   = 4;

	/** RTTTL "b=" (BPM) default if the parser sees no override. The
	 *  PhoneComposer itself ignores BPM (it only stores note shape),
	 *  but we plumb it through so an S123 round-trip preserves the
	 *  user's tempo choice. */
	static constexpr uint16_t DefaultBpm      = 63;

	/** Octave clamps applied during parse so a malformed RTTTL string
	 *  cannot overflow the Note.octave field beyond what S121 expects. */
	static constexpr uint8_t  ParseOctaveMin  = 1;
	static constexpr uint8_t  ParseOctaveMax  = 9;

	/**
	 * Serialize `count` notes from `notes` into `out` as a complete
	 * RTTTL string. The header is built with `name` and `bpm`; the
	 * default duration / octave fields are picked as the most common
	 * values in the buffer so the body emits as few inline overrides
	 * as possible.
	 *
	 * - `name`     : may be nullptr (emits the empty string before
	 *                the first ':'). Truncated to `MaxNameLen` glyphs
	 *                and any ',' / ':' / control characters are
	 *                replaced with '_' so the result stays parseable.
	 * - `bpm`      : 0 -> DefaultBpm.
	 * - `out`      : caller-owned buffer. Always nul-terminated when
	 *                outLen > 0, even on early-return failure.
	 * - `outLen`   : capacity of `out` in bytes (including nul).
	 *
	 * Returns the number of bytes written excluding the trailing nul,
	 * or 0 if `out` was too small or arguments were invalid.
	 */
	static size_t serialize(const PhoneComposer::Note* notes,
							uint8_t                    count,
							const char*                name,
							uint16_t                   bpm,
							char*                      out,
							size_t                     outLen);

	/**
	 * Parse `rtttl` into a buffer of PhoneComposer::Note structs.
	 *
	 * - `outNotes` : caller-owned array of length >= `maxNotes`.
	 * - `outCount` : on success, receives the number of notes parsed
	 *                (always <= maxNotes; extra notes are dropped
	 *                and the parser still returns true).
	 * - `outName`  : optional. If non-null, receives the parsed name
	 *                truncated to `outNameLen-1` chars and nul-
	 *                terminated. Pass nullptr / 0 to skip.
	 * - `outBpm`   : optional. If non-null, receives the parsed BPM
	 *                or `DefaultBpm` if none was specified.
	 *
	 * Returns true if at least the header was structurally valid (two
	 * ':' separators present and the defaults section parsed
	 * cleanly). Malformed individual command tokens are skipped so a
	 * partial recovery is still useful for the S123 save-slot UI.
	 */
	static bool parse(const char*           rtttl,
					  PhoneComposer::Note*  outNotes,
					  uint8_t               maxNotes,
					  uint8_t*              outCount,
					  char*                 outName,
					  size_t                outNameLen,
					  uint16_t*             outBpm);
};

#endif // CHATTER_FIRMWARE_PHONE_COMPOSER_RTTTL_H
