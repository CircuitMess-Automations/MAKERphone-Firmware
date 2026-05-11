/*
 * mp24/main/hal/piezo.c — square-wave synthesiser.
 *
 * Architecture:
 *
 *   Producer (any task)        Consumer (synth_task)
 *   piezo_tone(freq, dur) ───► writes atomics + xTaskNotifyGive ───►
 *     reads atomics, generates square-wave PCM, writes to I²S DMA in
 *     small chunks (BUF_FRAMES at a time). When `s_end_tick` is
 *     reached or `s_freq == 0`, the task writes one chunk of silence
 *     and then blocks on ulTaskNotifyTake.
 *
 * Square-wave generation uses a Bresenham-style accumulator — no
 * trig, no float. For each output sample we add (freq * 2) to the
 * accumulator; when it overflows the sample rate we flip polarity.
 * This produces an exact-frequency square wave bounded by integer
 * rounding error at one sample boundary per half-period.
 */

#include "hal/piezo.h"
#include "hal/audio_i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <string.h>

static const char *TAG = "PIEZO";

/* Output amplitude — int16 range is ±32768. 12000 leaves comfortable
 * headroom and avoids clipping when the amp adds gain. */
#define PIEZO_AMP        12000

/* Frames per I²S write. 128 stereo frames @ 22.05 kHz = ~5.8 ms of
 * audio per write. Short enough that piezo_no_tone() takes effect
 * within one buffer (≤6 ms latency). */
#define BUF_FRAMES       128

static TaskHandle_t              s_task        = NULL;
static atomic_uint_fast32_t      s_freq        = 0;       /* 0 = silence */
static atomic_uint_fast32_t      s_end_tick    = 0;       /* 0 = no timeout */
static bool                      s_inited      = false;

/* ----------------------------------------------------------------- */

static void synth_task(void *arg)
{
    (void)arg;

    int16_t  buf[BUF_FRAMES * AUDIO_CHANNELS];
    uint32_t accum    = 0;
    int16_t  polarity = PIEZO_AMP;
    /* Track the last frequency we rendered so we can reset the
     * accumulator when the user changes notes — gives a clean
     * phase boundary at the transition. */
    uint32_t last_freq = 0;

    for (;;) {
        uint32_t freq = (uint32_t) atomic_load(&s_freq);
        uint32_t end  = (uint32_t) atomic_load(&s_end_tick);

        /* Has the user-requested duration elapsed? */
        if (freq != 0 && end != 0 && xTaskGetTickCount() >= end) {
            atomic_store(&s_freq, 0);
            atomic_store(&s_end_tick, 0);
            freq = 0;
        }

        if (freq == 0) {
            /* Silence: write one buffer of zeros, then sleep until a
             * new tone request arrives (or 100 ms passes — safety
             * tick to keep the DMA fed if auto_clear ever stalls). */
            memset(buf, 0, sizeof(buf));
            size_t written = 0;
            audio_i2s_write(buf, sizeof(buf), &written, 100);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            last_freq = 0;
            continue;
        }

        if (freq != last_freq) {
            /* Frequency change: restart at a clean zero crossing. */
            accum     = 0;
            polarity  = PIEZO_AMP;
            last_freq = freq;
        }

        /* Generate one buffer worth of samples. */
        for (int i = 0; i < BUF_FRAMES; i++) {
            /* Accumulator advances by freq*2 per sample; each time it
             * exceeds the sample rate we've completed a half-period
             * and flip the wave polarity. */
            accum += freq * 2;
            if (accum >= AUDIO_SAMPLE_RATE_HZ) {
                accum -= AUDIO_SAMPLE_RATE_HZ;
                polarity = (int16_t)-polarity;
            }
            buf[i * 2]     = polarity;
            buf[i * 2 + 1] = polarity;
        }

        size_t written = 0;
        audio_i2s_write(buf, sizeof(buf), &written, 100);
    }
}

/* ----------------------------------------------------------------- */
/* Public API                                                        */
/* ----------------------------------------------------------------- */

esp_err_t piezo_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t r = audio_i2s_init();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "audio_i2s_init failed: %d", r);
        return r;
    }

    BaseType_t ok = xTaskCreate(synth_task, "piezo", 4096, NULL,
                                tskIDLE_PRIORITY + 4, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "synth task spawn failed");
        return ESP_FAIL;
    }

    s_inited = true;
    ESP_LOGI(TAG, "ready (square-wave synth, %d Hz sample rate, amp %d)",
             AUDIO_SAMPLE_RATE_HZ, PIEZO_AMP);
    return ESP_OK;
}

void piezo_tone(uint32_t freq_hz, uint32_t dur_ms)
{
    if (freq_hz == 0) {
        piezo_no_tone();
        return;
    }
    uint32_t end = (dur_ms > 0)
        ? (uint32_t)(xTaskGetTickCount() + pdMS_TO_TICKS(dur_ms))
        : 0;
    atomic_store(&s_freq, freq_hz);
    atomic_store(&s_end_tick, end);
    if (s_task) xTaskNotifyGive(s_task);
}

void piezo_no_tone(void)
{
    atomic_store(&s_freq, 0);
    atomic_store(&s_end_tick, 0);
    if (s_task) xTaskNotifyGive(s_task);
}

bool piezo_is_playing(void)
{
    return atomic_load(&s_freq) != 0;
}

uint32_t piezo_current_freq(void)
{
    return (uint32_t) atomic_load(&s_freq);
}
