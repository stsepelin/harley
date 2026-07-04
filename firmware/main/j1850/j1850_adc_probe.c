#include "j1850_adc_probe.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <limits.h>

static const char *TAG = "adc_probe";

#define PROBE_GPIO        CONFIG_VROD_J1850_ADC_GPIO
#define WINDOW_US         2000000            // 2 s min/max window
#define YIELD_US          1000               // max core hold between yields
#define NODE_B_TO_BUS(mv) ((mv) * 147 / 47)  // 10k/4.7k divider inverse

static volatile int  s_last_max_mv = -1;
static volatile int  s_last_min_mv = -1;
static volatile bool s_valid;

bool j1850_adc_probe_get(int *max_mv, int *min_mv)
{
    if (!s_valid)
        return false;
    if (max_mv)
        *max_mv = s_last_max_mv;
    if (min_mv)
        *min_mv = s_last_min_mv;
    return true;
}

static void probe_task(void *arg)
{
    (void)arg;

    adc_unit_t    unit;
    adc_channel_t chan;
    if (adc_oneshot_io_to_channel(PROBE_GPIO, &unit, &chan) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d has no ADC channel; probe disabled", PROBE_GPIO);
        vTaskDelete(NULL);
        return;
    }

    adc_oneshot_unit_handle_t         adc;
    const adc_oneshot_unit_init_cfg_t ucfg = {.unit_id = unit};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &adc));

    // 12 dB attenuation → ~0..3.3 V full scale; node B tops out ~2.4 V
    // (bus clamped 7.5 V / 3.128), comfortably in range.
    const adc_oneshot_chan_cfg_t ccfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, chan, &ccfg));

    adc_cali_handle_t                     cali     = NULL;
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = unit,
        .chan     = chan,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) != ESP_OK) {
        ESP_LOGE(TAG, "no ADC calibration; probe disabled");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "amplitude probe on GPIO%d (ADC%d ch%d), dedicated wire off node B", PROBE_GPIO,
             unit + 1, chan);

    int     win_min    = INT_MAX;
    int     win_max    = INT_MIN;
    int64_t win_start  = esp_timer_get_time();
    int64_t last_yield = win_start;

    for (;;) {
        int raw, mv;
        if (adc_oneshot_read(adc, chan, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) {
            if (mv < win_min)
                win_min = mv;
            if (mv > win_max)
                win_max = mv;
        }

        int64_t now = esp_timer_get_time();
        if (now - win_start >= WINDOW_US && win_max != INT_MIN) {
            s_last_max_mv = win_max;
            s_last_min_mv = win_min;
            s_valid       = true;
            int hi        = NODE_B_TO_BUS(win_max);
            int lo        = NODE_B_TO_BUS(win_min);
            ESP_LOGI(TAG, "amplitude: node_B max=%dmV min=%dmV | bus idle=%d.%02dV lo=%d.%02dV",
                     win_max, win_min, hi / 1000, (hi % 1000) / 10, lo / 1000, (lo % 1000) / 10);
            win_min   = INT_MAX;
            win_max   = INT_MIN;
            win_start = now;
        }

        // Sample fast to catch brief dominant pulses, but never hold the
        // core longer than ~1 ms between yields: adc_oneshot_read blocks
        // per conversion, so a fixed sample count could run several ms and
        // starve the idle task (task watchdog) and lower-priority work.
        if (now - last_yield >= YIELD_US) {
            last_yield = now;
            vTaskDelay(1);
        }
    }
}

void j1850_adc_probe_start(void)
{
    // Low priority (2): the sniffer/producers matter more; the probe
    // just fills the CPU's idle time with conversions.
    xTaskCreatePinnedToCore(probe_task, "adc_probe", 4096, NULL, 2, NULL, 0);
}
