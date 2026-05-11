/*
 * mp24/main/hal/audio_i2s.h — MAX98357A I²S TX channel management.
 *
 * Wraps the modern ESP-IDF `driver/i2s_std.h` API into a small,
 * synchronous "write some samples" interface used by hal/piezo.c and
 * (eventually) by any other audio source (ringtones, voice call PCM
 * forwarding from the GSM modem in S-MP10, etc.).
 *
 * Configuration is fixed for this revision:
 *   - 22.05 kHz sample rate (enough for the highest piezo
 *     fundamental we ever play, ~5 kHz, with Nyquist headroom; cuts
 *     buffer / DMA cost vs. 44.1 kHz)
 *   - 16-bit signed PCM
 *   - Stereo Philips format (the MAX98357A reads only one slot but
 *     2-slot mode is the path of least resistance with i2s_std)
 *   - Master role, MCLK unused (the chip generates its own from BCLK)
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

/* Bring up the I²S TX channel. Idempotent: calling twice returns
 * ESP_OK without re-initialising. */
esp_err_t audio_i2s_init(void);

/* Push a buffer of interleaved L/R int16 samples to the DMA. Blocks
 * up to `timeout_ms` waiting for buffer space; returns the number of
 * bytes actually written in `*out_written` (may be less than `bytes`
 * on timeout). */
esp_err_t audio_i2s_write(const int16_t *samples, size_t bytes,
                          size_t *out_written, uint32_t timeout_ms);

/* True once audio_i2s_init() has succeeded. */
bool audio_i2s_ready(void);
