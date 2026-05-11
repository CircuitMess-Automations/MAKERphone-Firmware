/*
 * mp24/main/hal/audio_i2s.c — MAX98357A I²S TX channel.
 *
 * Lifts the channel setup from /mnt/project/test_audio.c (proven on
 * this exact hardware) and trims it to a thin "init + write" API.
 * The synthesiser and any future audio sources sit on top.
 */

#include "hal/audio_i2s.h"
#include "hal/pins.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "I2S1";
static i2s_chan_handle_t s_tx     = NULL;
static bool              s_inited = false;

esp_err_t audio_i2s_init(void)
{
    if (s_inited) return ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,
                                                            I2S_ROLE_MASTER);
    /* auto_clear: when the synth task stops writing, DMA descriptors
     * auto-fill with zeros instead of looping the last sample —
     * silence on idle without any extra work. */
    chan_cfg.auto_clear = true;

    esp_err_t r = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %d", r);
        return r;
    }

    i2s_std_config_t cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S1_BCLK,
            .ws   = PIN_I2S1_WS,
            .dout = PIN_I2S1_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    r = i2s_channel_init_std_mode(s_tx, &cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %d", r);
        return r;
    }

    r = i2s_channel_enable(s_tx);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %d", r);
        return r;
    }

    s_inited = true;
    ESP_LOGI(TAG, "ready: %d Hz stereo 16-bit  BCLK=GPIO%d WS=GPIO%d DOUT=GPIO%d",
             AUDIO_SAMPLE_RATE_HZ,
             PIN_I2S1_BCLK, PIN_I2S1_WS, PIN_I2S1_DOUT);
    ESP_LOGI(TAG, "NOTE: amp SD_MODE is driven by AW9523B P1_1 — call "
                  "aw9523b_init() before this for any audio to be heard");
    return ESP_OK;
}

esp_err_t audio_i2s_write(const int16_t *samples, size_t bytes,
                          size_t *out_written, uint32_t timeout_ms)
{
    if (!s_tx) {
        if (out_written) *out_written = 0;
        return ESP_ERR_INVALID_STATE;
    }
    size_t written = 0;
    esp_err_t r = i2s_channel_write(s_tx, samples, bytes, &written,
                                    pdMS_TO_TICKS(timeout_ms));
    if (out_written) *out_written = written;
    return r;
}

bool audio_i2s_ready(void)
{
    return s_inited;
}
