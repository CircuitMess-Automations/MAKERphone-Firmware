#include "PhoneComposerRtttl.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// =====================================================================
// PhoneComposerRtttl — codec implementation.
//
// The serializer is straight string assembly: pick the most common
// duration + octave to put in the header, then emit each note with
// inline overrides only where it differs from the header.
//
// The parser is a permissive recursive-descent walk that accepts
// canonical RTTTL plus the dot-before-octave dialect that some
// composer apps emit. Whitespace anywhere is ignored, parsing is
// case-insensitive on tone letters, and malformed tokens are skipped
// so a partially-corrupted save slot still loads the playable parts.
//
// Zero LVGL / Arduino I/O — the only surfaces are stdio (snprintf)
// and ctype (isspace/isdigit/tolower) so the codec compiles in any
// host that exposes the C standard library.
// =====================================================================

// ---------- shared helpers ------------------------------------------------

static inline char rtttl_lower(char c) {
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static inline bool rtttl_is_tone_letter(char c) {
	const char l = rtttl_lower(c);
	return l == 'a' || l == 'b' || l == 'c' || l == 'd' ||
	       l == 'e' || l == 'f' || l == 'g' || l == 'p';
}

static inline char rtttl_canonical_tone(char c) {
	const char l = rtttl_lower(c);
	if(l == 'p') return 'P';
	if(l >= 'a' && l <= 'g') return (char)(l - 'a' + 'A');
	return 'P'; // unreachable for valid tones
}

// Quantizes any incoming length to the closest legal RTTTL length so
// a stray value never escapes the codec (the composer itself only
// produces 1/2/4/8/16/32, but defence-in-depth never hurts).
static uint8_t rtttl_normalize_length(uint8_t length) {
	static const uint8_t kLegal[] = { 1, 2, 4, 8, 16, 32 };
	for(uint8_t v : kLegal) {
		if(v == length) return length;
	}
	return 4;
}

// =====================================================================
// SERIALIZER
// =====================================================================

// Pick the most common length value used in the buffer, falling back
// to DefaultDuration when the buffer is empty. Bias toward 4 on ties
// so the canonical "d=4" header dominates short compositions.
static uint8_t picked_default_duration(const PhoneComposer::Note* notes,
									   uint8_t count) {
	if(notes == nullptr || count == 0) {
		return PhoneComposerRtttl::DefaultDuration;
	}
	static const uint8_t kLengths[] = { 1, 2, 4, 8, 16, 32 };
	uint16_t hist[6] = { 0, 0, 0, 0, 0, 0 };
	for(uint8_t i = 0; i < count; ++i) {
		const uint8_t len = rtttl_normalize_length(notes[i].length);
		for(uint8_t k = 0; k < 6; ++k) {
			if(kLengths[k] == len) { hist[k]++; break; }
		}
	}
	uint8_t bestK = 2; // index of "4"
	uint16_t bestN = hist[2];
	for(uint8_t k = 0; k < 6; ++k) {
		if(hist[k] > bestN) { bestN = hist[k]; bestK = k; }
	}
	return kLengths[bestK];
}

// Pick the most common octave among non-rest notes, falling back to
// DefaultOctave (4) for empty / all-rest buffers. Rests don't have a
// meaningful octave so they never participate in the histogram.
static uint8_t picked_default_octave(const PhoneComposer::Note* notes,
									 uint8_t count) {
	if(notes == nullptr || count == 0) {
		return PhoneComposerRtttl::DefaultOctave;
	}
	uint16_t hist[10] = { 0 };
	uint16_t toned = 0;
	for(uint8_t i = 0; i < count; ++i) {
		if(notes[i].tone == 'P') continue;
		uint8_t o = notes[i].octave;
		if(o > 9) o = 9;
		hist[o]++;
		toned++;
	}
	if(toned == 0) return PhoneComposerRtttl::DefaultOctave;
	uint8_t bestO = PhoneComposerRtttl::DefaultOctave;
	uint16_t bestN = 0;
	for(uint8_t o = 1; o < 10; ++o) {
		if(hist[o] > bestN) { bestN = hist[o]; bestO = o; }
	}
	return bestO;
}

// Sanitize a name down to the RTTTL spec: trim to MaxNameLen glyphs,
// replace ':' / ',' / non-printable with '_' so the resulting header
// stays unambiguously parseable.
static void sanitize_name(const char* in, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(in == nullptr) { out[0] = '\0'; return; }
	size_t cap = outLen - 1;
	if(cap > PhoneComposerRtttl::MaxNameLen) cap = PhoneComposerRtttl::MaxNameLen;
	size_t k = 0;
	for(size_t i = 0; in[i] != '\0' && k < cap; ++i) {
		const unsigned char c = (unsigned char)in[i];
		if(c < 0x20 || c == 0x7f || c == ':' || c == ',') {
			out[k++] = '_';
		} else if(c == ' ') {
			out[k++] = '_';
		} else {
			out[k++] = (char)c;
		}
	}
	out[k] = '\0';
}

// Helper: append a chunk to the cursor and bail out cleanly if the
// remaining buffer is too small. Returns false on overflow so the
// caller can early-exit.
static bool emit_chunk(char*& cur, char* end, const char* chunk) {
	if(chunk == nullptr) return true;
	while(*chunk != '\0') {
		if(cur >= end - 1) return false; // leave room for '\0'
		*cur++ = *chunk++;
	}
	return true;
}

size_t PhoneComposerRtttl::serialize(const PhoneComposer::Note* notes,
									 uint8_t                    count,
									 const char*                name,
									 uint16_t                   bpm,
									 char*                      out,
									 size_t                     outLen) {
	if(out == nullptr || outLen == 0) return 0;
	out[0] = '\0';
	if(notes == nullptr && count != 0) return 0;

	const uint8_t  defD = picked_default_duration(notes, count);
	const uint8_t  defO = picked_default_octave(notes, count);
	const uint16_t b    = (bpm == 0) ? DefaultBpm : bpm;

	char nameBuf[MaxNameLen + 1] = {};
	sanitize_name(name, nameBuf, sizeof(nameBuf));

	char* cur = out;
	char* end = out + outLen;

	// Header: "<name>:d=4,o=4,b=63:"
	char header[64] = {};
	snprintf(header, sizeof(header), "%s:d=%u,o=%u,b=%u:",
			 nameBuf, (unsigned)defD, (unsigned)defO, (unsigned)b);
	if(!emit_chunk(cur, end, header)) {
		out[outLen - 1] = '\0';
		return 0;
	}

	// Body: one comma-separated command per note.
	for(uint8_t i = 0; i < count; ++i) {
		PhoneComposer::Note n = notes[i];
		n.length = rtttl_normalize_length(n.length);

		char tok[12] = {};
		size_t tk = 0;

		// duration override (only when != defD)
		if(n.length != defD) {
			tk += (size_t)snprintf(tok + tk, sizeof(tok) - tk, "%u",
								   (unsigned)n.length);
		}

		// tone letter (lowercase, RTTTL convention)
		if(tk < sizeof(tok) - 1) {
			char tone = n.tone;
			if(tone == 'P') {
				tok[tk++] = 'p';
			} else {
				const char canonical = rtttl_canonical_tone(tone);
				tok[tk++] = (char)(canonical - 'A' + 'a');
			}
		}

		// sharp marker
		if(n.tone != 'P' && n.sharp && tk < sizeof(tok) - 1) {
			tok[tk++] = '#';
		}

		// octave override (only for non-rest, when != defO)
		if(n.tone != 'P' && n.octave != defO && tk < sizeof(tok) - 1) {
			tk += (size_t)snprintf(tok + tk, sizeof(tok) - tk, "%u",
								   (unsigned)n.octave);
		}

		// dotted suffix
		if(n.dotted && tk < sizeof(tok) - 1) {
			tok[tk++] = '.';
		}

		tok[tk] = '\0';

		if(!emit_chunk(cur, end, tok)) {
			out[outLen - 1] = '\0';
			return 0;
		}

		if(i + 1 < count) {
			if(!emit_chunk(cur, end, ",")) {
				out[outLen - 1] = '\0';
				return 0;
			}
		}
	}

	*cur = '\0';
	return (size_t)(cur - out);
}

// =====================================================================
// PARSER
// =====================================================================

// Skip ASCII whitespace (space, tab, CR, LF) at *p in-place. Cheap and
// allocation-free so we can use it everywhere the parser needs it.
static void skip_ws(const char*& p) {
	while(*p != '\0' && (*p == ' ' || *p == '\t' ||
						 *p == '\r' || *p == '\n')) {
		++p;
	}
}

// Read a base-10 unsigned, advancing p. Returns true if at least one
// digit was consumed; the caller then knows whether the field was
// actually present.
static bool read_uint(const char*& p, uint16_t& out) {
	if(*p < '0' || *p > '9') return false;
	uint32_t v = 0;
	while(*p >= '0' && *p <= '9') {
		v = v * 10 + (uint32_t)(*p - '0');
		if(v > 65535U) v = 65535U;
		++p;
	}
	out = (uint16_t)v;
	return true;
}

// Parse the defaults section between the two ':' separators. Tolerant
// of out-of-order keys and missing keys; absent keys keep their
// caller-provided defaults.
static void parse_defaults(const char* begin, const char* end,
						   uint8_t& defD, uint8_t& defO, uint16_t& bpm) {
	const char* p = begin;
	while(p < end) {
		skip_ws(p);
		if(p >= end) break;

		const char k = rtttl_lower(*p);
		++p;
		skip_ws(p);
		if(p >= end || *p != '=') {
			// malformed token -- skip to next comma
			while(p < end && *p != ',') ++p;
			if(p < end) ++p;
			continue;
		}
		++p; // past '='
		skip_ws(p);

		uint16_t v = 0;
		if(read_uint(p, v)) {
			if(k == 'd' && v >= 1 && v <= 64) {
				defD = (uint8_t)rtttl_normalize_length((uint8_t)v);
			} else if(k == 'o' && v >= PhoneComposerRtttl::ParseOctaveMin &&
					  v <= PhoneComposerRtttl::ParseOctaveMax) {
				defO = (uint8_t)v;
			} else if(k == 'b' && v > 0) {
				bpm = v;
			}
		}

		// advance to next setting
		skip_ws(p);
		if(p < end && *p == ',') ++p;
	}
}

// Try one command token at [tok, tokEnd). On success fills `out` and
// returns true; on malformed tokens returns false and `out` is
// untouched so the caller can drop the entry quietly.
static bool parse_command(const char* tok, const char* tokEnd,
						  uint8_t defD, uint8_t defO,
						  PhoneComposer::Note& out) {
	const char* p = tok;
	skip_ws(p);
	if(p >= tokEnd) return false;

	// optional duration
	uint16_t dur = 0;
	uint8_t  length = defD;
	bool hasDuration = false;
	if(*p >= '0' && *p <= '9') {
		if(read_uint(p, dur)) {
			hasDuration = true;
			length = rtttl_normalize_length((uint8_t)((dur > 32) ? 32 : dur));
		}
	}
	(void)hasDuration;

	skip_ws(p);
	if(p >= tokEnd) return false;
	if(!rtttl_is_tone_letter(*p)) return false;

	const char tone = rtttl_canonical_tone(*p);
	++p;

	// optional sharp
	bool sharp = false;
	if(p < tokEnd && *p == '#') {
		sharp = true;
		++p;
	}

	// dot or octave can come in either order; some implementations
	// emit "4c.5" rather than the spec's "4c5.". Accept both, plus a
	// stray trailing dot after the octave.
	bool dotted = false;
	uint16_t octave = defO;

	while(p < tokEnd) {
		skip_ws(p);
		if(p >= tokEnd) break;
		if(*p == '.') {
			dotted = true;
			++p;
			continue;
		}
		if(*p >= '0' && *p <= '9') {
			uint16_t o = 0;
			if(read_uint(p, o)) {
				if(o < PhoneComposerRtttl::ParseOctaveMin) {
					o = PhoneComposerRtttl::ParseOctaveMin;
				} else if(o > PhoneComposerRtttl::ParseOctaveMax) {
					o = PhoneComposerRtttl::ParseOctaveMax;
				}
				octave = o;
				continue;
			}
		}
		// stray character -- abort this token but salvage the work
		// done so far. The rest of the buffer is still parseable.
		break;
	}

	if(tone == 'P') {
		out.tone   = 'P';
		out.sharp  = false;
		out.octave = (uint8_t)defO;
	} else {
		out.tone   = tone;
		out.sharp  = sharp;
		out.octave = (uint8_t)octave;
	}
	out.length = length;
	out.dotted = dotted;
	return true;
}

bool PhoneComposerRtttl::parse(const char*           rtttl,
							   PhoneComposer::Note*  outNotes,
							   uint8_t               maxNotes,
							   uint8_t*              outCount,
							   char*                 outName,
							   size_t                outNameLen,
							   uint16_t*             outBpm) {
	if(outNotes == nullptr || outCount == nullptr) return false;
	*outCount = 0;
	if(outName != nullptr && outNameLen > 0) outName[0] = '\0';
	if(outBpm  != nullptr) *outBpm = DefaultBpm;
	if(rtttl == nullptr) return false;

	// Locate the two ':' separators.
	const char* sep1 = strchr(rtttl, ':');
	if(sep1 == nullptr) return false;
	const char* sep2 = strchr(sep1 + 1, ':');
	if(sep2 == nullptr) return false;

	// Section 1 -- name (optional). Trim leading/trailing whitespace
	// before copying so " jingle " becomes "jingle".
	if(outName != nullptr && outNameLen > 0) {
		const char* nameBegin = rtttl;
		const char* nameEnd   = sep1;
		while(nameBegin < nameEnd && (*nameBegin == ' ' || *nameBegin == '\t' ||
									  *nameBegin == '\r' || *nameBegin == '\n')) {
			++nameBegin;
		}
		while(nameEnd > nameBegin) {
			const char prev = *(nameEnd - 1);
			if(prev == ' ' || prev == '\t' || prev == '\r' || prev == '\n') {
				--nameEnd;
			} else {
				break;
			}
		}
		size_t span = (size_t)(nameEnd - nameBegin);
		if(span > outNameLen - 1) span = outNameLen - 1;
		if(span > MaxNameLen)     span = MaxNameLen;
		memcpy(outName, nameBegin, span);
		outName[span] = '\0';
	}

	// Section 2 -- defaults.
	uint8_t  defD = DefaultDuration;
	uint8_t  defO = DefaultOctave;
	uint16_t bpm  = DefaultBpm;
	parse_defaults(sep1 + 1, sep2, defD, defO, bpm);
	if(outBpm != nullptr) *outBpm = bpm;

	// Section 3 -- commands.
	const char* body = sep2 + 1;
	const char* p    = body;
	uint8_t produced = 0;

	while(*p != '\0' && produced < maxNotes) {
		// locate next comma (or end of string)
		const char* tokEnd = p;
		while(*tokEnd != '\0' && *tokEnd != ',') ++tokEnd;

		PhoneComposer::Note n = {};
		if(parse_command(p, tokEnd, defD, defO, n)) {
			outNotes[produced++] = n;
		}

		if(*tokEnd == '\0') break;
		p = tokEnd + 1;
	}

	*outCount = produced;
	return true;
}
