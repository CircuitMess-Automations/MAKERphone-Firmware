/*
 * mp24/main/hal/piezo.h — square-wave synthesiser over the I²S amp.
 *
 * Drop-in replacement for the LEDC-PWM Piezo class on the original
 * Chatter hardware. The MAX98357A on MP2.4 reads I²S frames at
 * 22.05 kHz / 16-bit and amplifies into a single speaker, so to
 * emulate the buzzer we synthesise a square wave at the requested
 * frequency directly into the I²S stream.
 *
 * Semantics match Arduino's `tone()`:
 *   piezo_tone(freq, dur_ms)  — start a tone; if dur_ms > 0, silence
 *                               automatically when the duration elapses
 *   piezo_tone(0, *)          — equivalent to piezo_no_tone()
 *   piezo_no_tone()           — immediate silence
 *
 * All entry points are async and safe from any FreeRTOS task context.
 * They are NOT safe from inside an ISR (use a deferred mechanism).
 *
 * Session S-MP05+ will add a thin C++ shim in components/circuitos_shim/
 * exposing `Piezo::tone()` / `Piezo::noTone()` / `Piezo::getInstance()`
 * forwarding to these C functions, so legacy BuzzerService /
 * PhoneRingtoneEngine / PhoneSystemTones can compile unchanged.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Initialise the synthesiser. Calls audio_i2s_init() internally and
 * spawns the synth task. Idempotent. Requires aw9523b_init() to have
 * been called first (SD_MODE must be HIGH). */
esp_err_t piezo_init(void);

/* Start playing a square wave at `freq_hz`. If `dur_ms > 0`, the tone
 * automatically stops after the duration; if `dur_ms == 0`, the tone
 * plays until piezo_no_tone() or another piezo_tone() call.
 *
 * A new piezo_tone() call preempts any tone currently playing. */
void piezo_tone(uint32_t freq_hz, uint32_t dur_ms);

/* Stop any currently-playing tone. */
void piezo_no_tone(void);

/* True if a tone is currently sounding. */
bool piezo_is_playing(void);

/* Currently-playing frequency (Hz), or 0 if silent. */
uint32_t piezo_current_freq(void);
