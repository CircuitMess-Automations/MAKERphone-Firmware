/*
 * mp24/main/hal/audio_i2s2.c — I²S2 slave RX (modem voice).
 *
 * The Quectel EG912U-GL drives BCLK and WS at 8 kHz / 16-bit; the
 * ESP32 sits in slave mode receiving the audio sample stream into
 * its DMA. A small internal task continuously reads samples,
 * computes a running RMS (cheap proof-of-life for the dashboard),
 * and feeds an external consumer queue.
 *
 * Sample-rate-converting the audio into the speaker's 22.05 kHz
 * stereo I²S1 stream lives in a follow-up session (S-MP10c). For
 * now, the read path exists in isolation: any task can drain
 * samples via audio_i2s2_read(), or just monitor RMS via
 * audio_i2s2_rms() to verify the bus is alive.
 */

#include "hal/audio_i2s2.h"
#include "hal/modem.h"
#include "hal/pins.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "I2S2";

#define I2S2_PORT_NUM    I2S_NUM_1   /* I²S0 is taken by the speaker amp */
#define DMA_BUF_COUNT    4
#define DMA_BUF_LEN      256         /* samples per buffer */

static i2s_chan_handle_t s_rx_chan       = NULL;
static atomic_bool       s_initialised   = false;
static atomic_bool       s_running       = false;
static _Atomic uint32_t  s_rms_x1000     = 0;   /* RMS × 1000, 0..1000 */
static TaskHandle_t      s_sampler_task  = NULL;

/* ----------------------------------------------------------------- */

static int read_samples_raw(i2s2_sample_t *buf, size_t max_samples,
                            uint32_t timeout_ms)
{
    if (!s_rx_chan) return -1;
    size_t bytes_read = 0;
    esp_err_t r = i2s_channel_read(s_rx_chan, buf,
                                   max_samples * sizeof(i2s2_sample_t),
                                   &bytes_read,
                                   pdMS_TO_TICKS(timeout_ms));
    if (r != ESP_OK && r != ESP_ERR_TIMEOUT) return -1;
    return (int)(bytes_read / sizeof(i2s2_sample_t));
}

static void sampler_task(void *arg)
{
    (void) arg;
    /* Window size of 800 samples = 100 ms at 8 kHz — gives a 10 Hz
     * RMS update rate, plenty for a dashboard meter. */
    i2s2_sample_t window[800];

    for (;;) {
        if (!atomic_load(&s_running)) {
            atomic_store(&s_rms_x1000, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int n = read_samples_raw(window, 800, 250);
        if (n <= 0) {
            atomic_store(&s_rms_x1000, 0);
            continue;
        }
        /* RMS = sqrt(mean(x²)) / INT16_MAX, scaled by 1000. Integer
         * math to avoid FPU contention on the dashboard task. */
        uint64_t sumsq = 0;
        for (int i = 0; i < n; i++) {
            int32_t s = window[i];
            sumsq += (uint64_t)(s * s);
        }
        uint64_t mean = sumsq / (uint64_t) n;
        /* sqrt of mean — integer approximation good enough for a meter. */
        uint32_t r = (uint32_t) sqrtf((float) mean);
        uint32_t scaled = (r * 1000U) / 32767U;
        if (scaled > 1000U) scaled = 1000U;
        atomic_store(&s_rms_x1000, scaled);
    }
}

/* ----------------------------------------------------------------- */

esp_err_t audio_i2s2_init(uint32_t ready_wait_ms)
{
    if (atomic_load(&s_initialised)) return ESP_OK;

    /* Wait for the modem so we can configure its side of the I²S2
     * bus before we open ours. */
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ready_wait_ms);
    while (modem_state() != MODEM_READY) {
        if (modem_state() == MODEM_FAILED) {
            ESP_LOGW(TAG, "modem FAILED — I²S2 init aborted");
            return ESP_ERR_INVALID_STATE;
        }
        if (xTaskGetTickCount() > deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /* AT+QDAI=4 → PCM master mode, 8 kHz, 16-bit, mono. Modem drives
     * BCLK + WS and writes samples to the DATA pin which our slave
     * RX channel reads. If the modem firmware is configured for a
     * different default (some carrier-specific builds ship with =5),
     * the AT command flips it. */
    if (modem_at_send("+QDAI=4", NULL, 0, 1500) != ESP_OK) {
        ESP_LOGW(TAG, "AT+QDAI=4 failed — modem may not provide PCM clocks");
        /* Continue anyway — maybe the modem was already in PCM master
         * mode and the command is a no-op. */
    }

    /* I²S channel config: slave RX, single channel (mono), default
     * 16-bit samples. */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S2_PORT_NUM, I2S_ROLE_SLAVE);
    chan_cfg.dma_desc_num = DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = DMA_BUF_LEN;
    chan_cfg.auto_clear = false;

    esp_err_t r = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(r));
        return r;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S2_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S2_CLK,
            .ws   = PIN_I2S2_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_I2S2_DATA,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    r = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s",
                 esp_err_to_name(r));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return r;
    }

    BaseType_t ok = xTaskCreate(sampler_task, "i2s2_sampler",
                                4096, NULL,
                                tskIDLE_PRIORITY + 2,
                                &s_sampler_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "sampler task spawn failed");
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ESP_FAIL;
    }

    atomic_store(&s_initialised, true);
    ESP_LOGI(TAG, "init OK — slave RX @ %d Hz, 16-bit mono, BCLK=GPIO%d "
                  "WS=GPIO%d DIN=GPIO%d",
             I2S2_SAMPLE_RATE_HZ,
             PIN_I2S2_CLK, PIN_I2S2_WS, PIN_I2S2_DATA);
    return ESP_OK;
}

esp_err_t audio_i2s2_start(void)
{
    if (!atomic_load(&s_initialised)) return ESP_ERR_INVALID_STATE;
    if (atomic_load(&s_running)) return ESP_OK;
    esp_err_t r = i2s_channel_enable(s_rx_chan);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "channel_enable failed: %s", esp_err_to_name(r));
        return r;
    }
    atomic_store(&s_running, true);
    ESP_LOGI(TAG, "running");
    return ESP_OK;
}

esp_err_t audio_i2s2_stop(void)
{
    if (!atomic_load(&s_initialised)) return ESP_ERR_INVALID_STATE;
    if (!atomic_load(&s_running)) return ESP_OK;
    atomic_store(&s_running, false);
    esp_err_t r = i2s_channel_disable(s_rx_chan);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "channel_disable returned %s", esp_err_to_name(r));
    }
    atomic_store(&s_rms_x1000, 0);
    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}

int audio_i2s2_read(i2s2_sample_t *buf, size_t max_samples,
                    uint32_t timeout_ms)
{
    if (!buf || max_samples == 0)            return -1;
    if (!atomic_load(&s_initialised))        return -1;
    if (!atomic_load(&s_running)) {
        esp_err_t r = audio_i2s2_start();
        if (r != ESP_OK) return -1;
    }
    return read_samples_raw(buf, max_samples, timeout_ms);
}

float audio_i2s2_rms(void)
{
    return (float) atomic_load(&s_rms_x1000) / 1000.0f;
}

bool audio_i2s2_active(void)
{
    return atomic_load(&s_running);
}
