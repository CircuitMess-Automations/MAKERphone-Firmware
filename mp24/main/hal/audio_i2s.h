/*
 * mp24/main/hal/audio_i2s.h — MAX98357A I²S TX channel + SPH0645 mic RX.
 *
 * Wraps the modern ESP-IDF `driver/i2s_std.h` API into a small,
 * synchronous "write some samples" / "read some samples" interface
 * used by hal/piezo.c, the modem voice bridge (S-MP10c), and any
 * other audio source/sink.
 *
 * Configuration is fixed for this revision:
 *   - 22.05 kHz sample rate (enough for the highest piezo
 *     fundamental we ever play, ~5 kHz, with Nyquist headroom; cuts
 *     buffer / DMA cost vs. 44.1 kHz)
 *   - 16-bit signed PCM data, 32-bit slot width (so BCLK lands inside
 *     the SPH0645 mic's 1.024–4.096 MHz range — at 22.05 kHz × 32 × 2
 *     that's ≈1.41 MHz BCLK, well within spec)
 *   - Stereo Philips frame, full-duplex (master TX + master RX on the
 *     same I²S port, sharing BCLK and WS)
 *   - The MAX98357A reads only the LEFT slot; the SPH0645 with SELECT
 *     tied to GND also outputs on the LEFT slot — so both endpoints
 *     are looking at the same channel.
 *
 * The MAX98357A SD_MODE pin is on AW9523B P1_1 and is driven HIGH by
 * aw9523b_init() — therefore audio_i2s_init() REQUIRES aw9523b_init()
 * to have been called first, or the speaker stays muted.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Audio engine constants — public so the piezo synthesiser can size
 * its accumulator math correctly. */
#define AUDIO_SAMPLE_RATE_HZ   22050
#define AUDIO_CHANNELS         2      /* stereo Philips frame layout */
#define AUDIO_BYTES_PER_SAMPLE 2      /* int16_t per channel */

/* Bring up the I²S TX and RX channels (full-duplex). Idempotent. */
esp_err_t audio_i2s_init(void);

/* Push a buffer of interleaved L/R int16 samples to the DMA. Blocks
 * up to `timeout_ms` waiting for buffer space; returns the number of
 * bytes actually written in `*out_written` (may be less than `bytes`
 * on timeout). */
esp_err_t audio_i2s_write(const int16_t *samples, size_t bytes,
                          size_t *out_written, uint32_t timeout_ms);

/* Pull a buffer of interleaved L/R int16 samples from the mic DMA.
 * SPH0645 outputs 24-bit samples MSB-first in the upper bits of the
 * 32-bit slot; when read as 16-bit, the returned values are the high
 * 16 bits of the 24-bit sample (i.e., effective 16-bit resolution).
 *
 * The mic's audio appears in the LEFT channel of the stereo stream;
 * the RIGHT channel slots are silence (don't bother analysing them).
 *
 * Blocks up to `timeout_ms` for samples to arrive. Returns
 * ESP_ERR_INVALID_STATE if audio_i2s_init() hasn't been called or
 * audio_i2s_mic_start() hasn't been called yet. */
esp_err_t audio_i2s_mic_read(int16_t *samples, size_t bytes,
                             size_t *out_read, uint32_t timeout_ms);

/* The mic RX path is created during audio_i2s_init() but is left
 * DISABLED so the DMA isn't constantly filling buffers we don't read.
 * Call _mic_start() before the first audio_i2s_mic_read(); call
 * _mic_stop() when done. Idempotent. */
esp_err_t audio_i2s_mic_start(void);
esp_err_t audio_i2s_mic_stop(void);

/* True once audio_i2s_init() has succeeded. */
bool audio_i2s_ready(void);

/* True if the mic RX channel is currently enabled. */
bool audio_i2s_mic_active(void);
