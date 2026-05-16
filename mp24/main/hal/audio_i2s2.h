/*
 * mp24/main/hal/audio_i2s2.h — I²S2 RX driver for modem voice PCM.
 *
 * I²S2 carries the audio uplink from the Quectel EG912U-GL during
 * a voice call. The schematic exposes three lines:
 *     PIN_I2S2_CLK  (GPIO 14)  uI2S2_PHONE_CLK   — BCLK
 *     PIN_I2S2_WS   (GPIO 21)  uI2S2_PHONE_WS    — LRCLK / frame sync
 *     PIN_I2S2_DATA (GPIO 13)  uI2S2_PHONE_DATA  — modem → ESP32
 *
 * Quectel default audio mode is PCM Master at 8 kHz / 16-bit mono;
 * AT+QDAI=4 keeps the modem as master so we don't have to generate
 * the clocks. The ESP32 sits in slave RX and reads samples whenever
 * a call is active.
 *
 * S-MP10b scope: bring the bus up + read samples + measure RMS so
 * we have proof of life on a real call. Mixing those samples into
 * the speaker output (via the existing I²S1 path) is S-MP10c — it
 * needs sample-rate conversion (8 kHz → 22.05 kHz) and channel
 * doubling (mono → stereo) plus a careful buffer hand-off between
 * the two I²S drivers, none of which is testable without working
 * call audio in the first place.
 *
 * The host → modem (mic) direction lives on a different physical
 * path that this board's schematic doesn't expose on I²S2; the
 * SPH0645 MEMS mic on I²S1 DATA_MIC is the planned source once
 * the routing layer arrives.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Sample format the driver delivers: 16-bit signed, mono. */
typedef int16_t i2s2_sample_t;

/* Default Quectel voice rate. AT+QDAI=4 → PCM master @ 8 kHz. */
#define I2S2_SAMPLE_RATE_HZ   8000

/* Initialise the I²S2 driver in slave RX mode and issue AT+QDAI=4
 * so the modem clocks the bus. Waits for the modem to enter READY
 * state first (caps at ready_wait_ms). Idempotent. */
esp_err_t audio_i2s2_init(uint32_t ready_wait_ms);

/* Begin reading samples. Safe to call multiple times; subsequent
 * calls are a no-op until audio_i2s2_stop() is issued. Typically
 * wired to call-state transitions: ACTIVE → start, IDLE → stop. */
esp_err_t audio_i2s2_start(void);
esp_err_t audio_i2s2_stop(void);

/* Pull up to `max_samples` samples into `buf`. Blocks up to
 * `timeout_ms`. Returns the actual sample count (0 on timeout, -1
 * on driver error). The driver is started lazily on first read if
 * audio_i2s2_start hasn't been called. */
int audio_i2s2_read(i2s2_sample_t *buf, size_t max_samples,
                    uint32_t timeout_ms);

/* RMS amplitude over the last sampling window, normalised to
 * [0.0 .. 1.0]. Internal sampler task updates this at ~10 Hz so
 * dashboard code has something cheap to render. Returns 0 when
 * inactive (call not in progress or modem not clocking). */
float audio_i2s2_rms(void);

/* True iff the driver is currently in RUNNING state (started and
 * receiving samples). */
bool audio_i2s2_active(void);
