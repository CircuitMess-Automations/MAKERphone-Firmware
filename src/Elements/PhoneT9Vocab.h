#ifndef MAKERPHONE_PHONET9VOCAB_H
#define MAKERPHONE_PHONET9VOCAB_H

#include <Arduino.h>
#include <stdint.h>

/**
 * PhoneT9Vocab (S175)
 *
 * Shared dictionary pack used by every word-driven screen on the
 * MAKERphone (`PhoneWordle`, `PhoneHangman`, future predictive T9 in
 * `PhoneT9Input`). Two pools are exposed:
 *
 *   - **Five-letter pool** -- exactly five uppercase ASCII letters per
 *     word, the format `PhoneWordle` expects. Targets ~200 entries so
 *     a couple of rounds in a row almost never repeat without bloating
 *     RODATA more than a few KB.
 *
 *   - **Mixed-length pool** -- 5..7 uppercase ASCII letters per word,
 *     the format `PhoneHangman` expects (its `MaxWordLen` is 7). Same
 *     target footprint.
 *
 * Both lists are curated common English nouns / adjectives / verbs --
 * a player on a feature-phone has a fair chance even without a real
 * dictionary lookup. Words are uppercase A..Z only so the consumer
 * screens can write straight into their reveal buffers without an
 * extra `toupper` pass.
 *
 * The lists live in the `.cpp` so they're compiled into RODATA exactly
 * once even if the header is included from many translation units.
 *
 * Future-proofing: a `digitsForLetter()` helper exposes the canonical
 * ITU-T E.161 digit-for-letter mapping so a later session (S179+ live
 * preview, or a true predictive T9 wrapper around `PhoneT9Input`) can
 * filter the same word lists by typed digit prefix without redefining
 * the keymap.
 */
class PhoneT9Vocab {
public:
	/** Number of five-letter words available. */
	static uint16_t fiveLetterCount();

	/**
	 * One five-letter word, all uppercase A..Z, NUL-terminated.
	 * Returns nullptr if `idx` is out of range.
	 */
	static const char* fiveLetter(uint16_t idx);

	/** Number of mixed-length (5..7) words available. */
	static uint16_t mixedCount();

	/**
	 * One mixed-length word, all uppercase A..Z, NUL-terminated.
	 * `strlen` is between 5 and 7. Returns nullptr if `idx` is out of
	 * range.
	 */
	static const char* mixed(uint16_t idx);

	/**
	 * Canonical ITU-T E.161 digit for an A..Z / a..z letter. Returns
	 * 0 for letters that don't map (none in A..Z, but defensive).
	 *
	 *      a/b/c -> 2,  d/e/f -> 3,  g/h/i -> 4,  j/k/l -> 5,
	 *      m/n/o -> 6,  p/q/r/s -> 7,  t/u/v -> 8,  w/x/y/z -> 9.
	 */
	static uint8_t digitsForLetter(char letter);
};

#endif //MAKERPHONE_PHONET9VOCAB_H
