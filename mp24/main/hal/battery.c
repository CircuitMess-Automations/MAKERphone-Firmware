/*
 * mp24/main/hal/battery.c — battery voltage monitor.
 *
 * ADC channel setup + curve-fitting calibration lifted from
 * /mnt/project/test_adc.c (proven on this exact hardware). Added:
 *   - Background 1 Hz sampling task
 *   - Atomic cache for voltage so readers don't block
 *   - Li-Po SOC estimation via piecewise-linear curve
 *
 * Public entry points are non-blocking and safe from any task.
 */

#include "hal/battery.h"
#include "hal/pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include <stdatomic.h>

static const char *TAG = "BATT";

static adc_oneshot_unit_handle_t s_adc_handle  = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_ok     = false;
static bool                      s_inited      = false;

/* Atomic cache: voltage stored as millivolts × 1 to keep it integral. */
static atomic_int_fast32_t s_vbat_mv = 0;
static atomic_bool         s_have_sample = false;

/* ----------------------------------------------------------------- */
/* One-shot read: average 8 samples to suppress noise; convert raw   */
/* to mV via eFuse-calibrated curve fit; multiply by divider ratio.  */
/* ----------------------------------------------------------------- */
static int sample_vbat_mv(void)
{
    const int N = 8;
    long sum = 0;
    for (int i = 0; i < N; i++) {
        int r = 0;
        if (adc_oneshot_read(s_adc_handle, ADC_CHANNEL_BATTERY, &r) != ESP_OK) {
            return -1;
        }
        sum += r;
    }
    int raw = (int)(sum / N);

    int adc_mv = 0;
    if (s_cali_ok) {
        if (adc_cali_raw_to_voltage(s_cali_handle, raw, &adc_mv) != ESP_OK) {
            adc_mv = (raw * 3100) / 4095;   /* fallback if cal call fails */
        }
    } else {
        /* No eFuse cal: rough conversion for ADC_ATTEN_DB_12 on ESP32-S3
         * (full-scale ~3.1 V at the pin → ~3.3 V is clipped). */
        adc_mv = (raw * 3100) / 4095;
    }

    /* The ADC sees half of VBAT through the 1:1 divider, so undo it. */
    return (int)((float)adc_mv * VBAT_DIVIDER_RATIO);
}

/* ----------------------------------------------------------------- */
/* Background sampler                                                */
/* ----------------------------------------------------------------- */
static void sampler_task(void *arg)
{
    (void)arg;
    for (;;) {
        int mv = sample_vbat_mv();
        if (mv >= 0) {
            atomic_store(&s_vbat_mv, mv);
            atomic_store(&s_have_sample, true);
        } else {
            ESP_LOGW(TAG, "ADC read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));   /* 1 Hz */
    }
}

/* ----------------------------------------------------------------- */
/* Public API                                                        */
/* ----------------------------------------------------------------- */

esp_err_t battery_init(void)
{
    if (s_inited) return ESP_OK;

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_BATTERY,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t r = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %d", r);
        return r;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,    /* 0..3.3 V input range */
    };
    r = adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_BATTERY, &chan_cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %d", r);
        return r;
    }

    /* Curve-fitting cal — ESP32-S3 supports only this scheme (line
     * fitting is on earlier chips). May fail on parts whose eFuse
     * never got the factory burn; we tolerate that and fall back to
     * the uncalibrated 3100 mV full-scale approximation. */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_BATTERY,
        .chan     = ADC_CHANNEL_BATTERY,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle) == ESP_OK) {
        s_cali_ok = true;
        ESP_LOGI(TAG, "curve-fitting cal active");
    } else {
        ESP_LOGW(TAG, "no eFuse cal — readings will be ~±100 mV approximate");
    }

    /* Take one synchronous sample so the dashboard doesn't show 0
     * for the first second after boot. */
    int mv = sample_vbat_mv();
    if (mv >= 0) {
        atomic_store(&s_vbat_mv, mv);
        atomic_store(&s_have_sample, true);
        ESP_LOGI(TAG, "first sample: %d mV (%.2f V)", mv, mv / 1000.0f);
    }

    /* Spawn the 1 Hz sampler. */
    BaseType_t ok = xTaskCreate(sampler_task, "batt_sampler",
                                3072, NULL, tskIDLE_PRIORITY + 2, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "sampler task spawn failed");
        return ESP_FAIL;
    }

    s_inited = true;
    return ESP_OK;
}

float battery_voltage(void)
{
    int mv = (int) atomic_load(&s_vbat_mv);
    return (float)mv / 1000.0f;
}

bool battery_ready(void)
{
    return atomic_load(&s_have_sample);
}

/* ----------------------------------------------------------------- */
/* SOC estimation — piecewise-linear Li-Po curve.                    */
/* ----------------------------------------------------------------- */

typedef struct {
    int mv;
    int pct;
} soc_point_t;

/* Knot points along a typical 1S Li-Po discharge curve under light
 * load. Coarse but good enough for a battery icon. */
static const soc_point_t k_soc_curve[] = {
    { 4200, 100 },
    { 4000,  80 },
    { 3850,  50 },
    { 3700,  25 },
    { 3500,  10 },
    { 3300,   0 },
};

int battery_percent(void)
{
    if (!atomic_load(&s_have_sample)) return 0;

    int mv = (int) atomic_load(&s_vbat_mv);

    if (mv >= k_soc_curve[0].mv) return k_soc_curve[0].pct;

    const int n = sizeof(k_soc_curve) / sizeof(k_soc_curve[0]);
    if (mv <= k_soc_curve[n - 1].mv) return k_soc_curve[n - 1].pct;

    /* Walk the table; interpolate between the bracketing knots. */
    for (int i = 0; i < n - 1; i++) {
        if (mv <= k_soc_curve[i].mv && mv >= k_soc_curve[i + 1].mv) {
            int dmv  = k_soc_curve[i].mv  - k_soc_curve[i + 1].mv;
            int dpct = k_soc_curve[i].pct - k_soc_curve[i + 1].pct;
            int off  = k_soc_curve[i].mv  - mv;
            return k_soc_curve[i].pct - (off * dpct) / dmv;
        }
    }
    return 0;
}
