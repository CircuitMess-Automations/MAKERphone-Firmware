/*
 * mp24/main/hal/audio_i2s.c — MAX98357A I²S TX + SPH0645 I²S RX.
 *
 * Lifts the TX-channel setup from /mnt/project/test_audio.c (proven
 * on this exact hardware) and adds a parallel RX channel for the
 * MEMS microphone on PIN_I2S1_DIN_MIC. Both channels run as master,
 * sharing BCLK + WS — the i2s_std driver allows full-duplex when
 * both tx_handle and rx_handle are non-NULL on i2s_new_channel.
 *
 * Slot width is explicitly 32 bits (data width still 16) so BCLK
 * lands at sample_rate × 32 × 2 channels ≈ 1.41 MHz — inside the
 * SPH0645's 1.024–4.096 MHz BCLK window. The amp is content with
 * any standard rate / slot width; only the mic constrains us.
 *
 * The RX channel is created at init time but left DISABLED. Callers
 * arm it via audio_i2s_mic_start() before the first read. This
 * keeps DMA from filling buffers we'd otherwise just drop.
 */

#include "hal/audio_i2s.h"
#include "hal/pins.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <stdatomic.h>

static const char *TAG = "I2S1";
static i2s_chan_handle_t s_tx          = NULL;
static i2s_chan_handle_t s_rx          = NULL;
static bool              s_inited      = false;
static atomic_bool       s_mic_running = false;

esp_err_t audio_i2s_init(void)
{
    if (s_inited) return ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,
                                                            I2S_ROLE_MASTER);
    /* auto_clear: when the synth task stops writing, DMA descriptors
     * auto-fill with zeros instead of looping the last sample —
     * silence on idle without any extra work. */
    chan_cfg.auto_clear = true;

    /* Full-duplex: both tx_handle and rx_handle non-NULL. The driver
     * binds them to the same port and lets them share BCLK/WS. */
    esp_err_t r = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %d", r);
        return r;
    }

    /* Common slot configuration. Explicit 32-bit slot width keeps
     * BCLK above the SPH0645's 1.024 MHz minimum. The default macro
     * leaves slot_bit_width auto, which under our 22.05 kHz / 16-bit
     * settings would land at 705 kHz BCLK — silent mic. */
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;

    i2s_std_config_t tx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S1_BCLK,
            .ws   = PIN_I2S1_WS,
            .dout = PIN_I2S1_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    r = i2s_channel_init_std_mode(s_tx, &tx_cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "tx init_std_mode failed: %d", r);
        return r;
    }

    /* RX channel: same clocks, swap DOUT for DIN. */
    i2s_std_config_t rx_cfg = tx_cfg;
    rx_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    rx_cfg.gpio_cfg.din  = PIN_I2S1_DIN_MIC;

    r = i2s_channel_init_std_mode(s_rx, &rx_cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "rx init_std_mode failed: %d", r);
        return r;
    }

    /* Enable TX immediately; RX waits for audio_i2s_mic_start(). */
    r = i2s_channel_enable(s_tx);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "tx channel_enable failed: %d", r);
        return r;
    }

    s_inited = true;
    ESP_LOGI(TAG, "ready: %d Hz stereo 16-bit/32-slot  "
                  "BCLK=GPIO%d WS=GPIO%d DOUT=GPIO%d DIN=GPIO%d",
             AUDIO_SAMPLE_RATE_HZ,
             PIN_I2S1_BCLK, PIN_I2S1_WS, PIN_I2S1_DOUT, PIN_I2S1_DIN_MIC);
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

esp_err_t audio_i2s_mic_start(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (atomic_load(&s_mic_running)) return ESP_OK;
    esp_err_t r = i2s_channel_enable(s_rx);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "mic channel_enable failed: %d", r);
        return r;
    }
    atomic_store(&s_mic_running, true);
    ESP_LOGI(TAG, "mic RX enabled");
    return ESP_OK;
}

esp_err_t audio_i2s_mic_stop(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!atomic_load(&s_mic_running)) return ESP_OK;
    esp_err_t r = i2s_channel_disable(s_rx);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "mic channel_disable returned %d", r);
    }
    atomic_store(&s_mic_running, false);
    ESP_LOGI(TAG, "mic RX disabled");
    return ESP_OK;
}

esp_err_t audio_i2s_mic_read(int16_t *samples, size_t bytes,
                             size_t *out_read, uint32_t timeout_ms)
{
    if (!s_rx || !atomic_load(&s_mic_running)) {
        if (out_read) *out_read = 0;
        return ESP_ERR_INVALID_STATE;
    }
    size_t r_bytes = 0;
    esp_err_t r = i2s_channel_read(s_rx, samples, bytes, &r_bytes,
                                   pdMS_TO_TICKS(timeout_ms));
    if (out_read) *out_read = r_bytes;
    return r;
}

bool audio_i2s_ready(void)
{
    return s_inited;
}

bool audio_i2s_mic_active(void)
{
    return atomic_load(&s_mic_running);
}
