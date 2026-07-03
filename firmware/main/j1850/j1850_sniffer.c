#include "j1850_sniffer.h"
#include "j1850_vpw.h"

#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "j1850";

#define SNIFF_GPIO ((gpio_num_t)CONFIG_VROD_J1850_RX_GPIO)
// 10.4 kbps VPW tops out below ~16k edges/s; 512 entries buffer ~30 ms
// of the densest possible traffic while the log task is writing.
#define PULSE_QUEUE_LEN 512
#define STATS_PERIOD_MS 10000

typedef struct {
    uint8_t  active;  // level of the pulse that just ENDED
    uint32_t dur_us;
} pulse_evt_t;

static QueueHandle_t     s_queue;
static portMUX_TYPE      s_edge_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t           s_last_edge_us;
static int               s_level;  // bus level since the last edge
static volatile uint32_t s_overruns;
static volatile uint32_t s_edges;  // raw ISR count — noise shows here

// Restoring this after an ADC sample re-arms the digital input path.
static gpio_config_t s_pin_cfg;

// Shared with the bench screen via j1850_sniffer_get_stats().
static portMUX_TYPE          s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static j1850_sniffer_stats_t s_stats;

static void IRAM_ATTR edge_isr(void *arg)
{
    (void)arg;
    s_edges++;
    int64_t now   = esp_timer_get_time();
    int     level = gpio_get_level(SNIFF_GPIO);

    portENTER_CRITICAL_ISR(&s_edge_mux);
    pulse_evt_t evt = {
        .active = (uint8_t)s_level,
        .dur_us = (uint32_t)(now - s_last_edge_us),
    };
    s_level        = level;
    s_last_edge_us = now;
    portEXIT_CRITICAL_ISR(&s_edge_mux);

    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_queue, &evt, &woken) != pdTRUE)
        s_overruns++;
    if (woken == pdTRUE)
        portYIELD_FROM_ISR();
}

static void log_frame(const j1850_frame_t *f)
{
    // Worst case: 12 data + 12 IFR bytes at 3 chars each + decorations.
    char line[96];
    int  pos = 0;
    for (size_t i = 0; i < f->len; i++) {
        pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%02X ", f->data[i]);
    }
    pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "| CRC %s", f->crc_ok ? "OK" : "BAD");
    if (f->ifr_len > 0) {
        pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " | IFR");
        for (size_t i = 0; i < f->ifr_len; i++) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " %02X", f->ifr[i]);
        }
    }
    ESP_LOGI(TAG, "%s", line);
}

static void publish_frame(const j1850_frame_t *f, uint32_t frames, uint32_t crc_bad)
{
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.frames  = frames;
    s_stats.crc_bad = crc_bad;
    memcpy(s_stats.last_frame, f->data, f->len);
    s_stats.last_len    = f->len;
    s_stats.last_crc_ok = f->crc_ok;
    portEXIT_CRITICAL(&s_stats_mux);
}

static void sniffer_task(void *arg)
{
    (void)arg;
    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);

    uint32_t frames = 0, crc_bad = 0;
    int64_t  last_stats_us   = esp_timer_get_time();
    int64_t  flushed_edge_us = 0;

    for (;;) {
        pulse_evt_t   evt;
        j1850_frame_t frame;

        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(2)) == pdTRUE) {
            if (j1850_vpw_rx_pulse(&rx, evt.active != 0, evt.dur_us, &frame)) {
                frames++;
                if (!frame.crc_ok)
                    crc_bad++;
                log_frame(&frame);
                publish_frame(&frame, frames, crc_bad);
            }
        } else {
            // Queue idle. If the bus has sat passive past EOF since the
            // last edge, close out the pending frame with a synthetic
            // pulse — an edge-timed capture never sees the final pulse
            // end on its own. Once per edge, or idle would spam.
            portENTER_CRITICAL(&s_edge_mux);
            int64_t last  = s_last_edge_us;
            int     level = s_level;
            portEXIT_CRITICAL(&s_edge_mux);
            int64_t idle_us = esp_timer_get_time() - last;
            if (level == 0 && last != flushed_edge_us && idle_us > J1850_VPW_SOF_MAX_US + 60) {
                flushed_edge_us = last;
                if (j1850_vpw_rx_pulse(&rx, false, (uint32_t)idle_us, &frame)) {
                    frames++;
                    if (!frame.crc_ok)
                        crc_bad++;
                    log_frame(&frame);
                    publish_frame(&frame, frames, crc_bad);
                }
            }
        }

        int64_t now = esp_timer_get_time();
        if (now - last_stats_us > (int64_t)STATS_PERIOD_MS * 1000) {
            last_stats_us = now;
            // Edge rate is the wiring-health number: a real idle bus is
            // ~0; a floating pin or a DC level parked on the input
            // threshold shows up as thousands per period.
            ESP_LOGI(TAG, "stats: %lu frames, %lu bad CRC, %lu overruns, %lu edges",
                     (unsigned long)frames, (unsigned long)crc_bad, (unsigned long)s_overruns,
                     (unsigned long)s_edges);
            portENTER_CRITICAL(&s_stats_mux);
            s_stats.edges_last_period = s_edges;
            s_stats.overruns          = s_overruns;
            portEXIT_CRITICAL(&s_stats_mux);
            s_edges = 0;
        }
    }
}

void j1850_sniffer_start(void)
{
    s_queue = xQueueCreate(PULSE_QUEUE_LEN, sizeof(pulse_evt_t));
    configASSERT(s_queue);

    s_pin_cfg = (gpio_config_t){
        .pin_bit_mask = 1ULL << SNIFF_GPIO,
        .mode         = GPIO_MODE_INPUT,
        // The RX divider drives the pin at all times — no pulls.
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&s_pin_cfg));

    // Hardware glitch filter: silicon suppresses pulses under ~500 ns
    // before they reach the interrupt matrix. Real VPW symbols are
    // >=34 us, so nothing legitimate is touched — but RF pickup and
    // threshold chatter (floating pin, or a DC test level parked near
    // VIH) stop hammering core 0 with edge interrupts, which otherwise
    // visibly lags the sim/UI producers. Best-effort: if no filter
    // slot is free we run unfiltered, same as before.
    gpio_glitch_filter_handle_t            filter = NULL;
    const gpio_flex_glitch_filter_config_t fcfg   = {
        .clk_src         = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num        = SNIFF_GPIO,
        .window_width_ns = 500,
        .window_thres_ns = 500,
    };
    esp_err_t ferr = gpio_new_flex_glitch_filter(&fcfg, &filter);
    if (ferr == ESP_OK) {
        ESP_ERROR_CHECK(gpio_glitch_filter_enable(filter));
    } else {
        ESP_LOGW(TAG, "no glitch filter (%s); running unfiltered", esp_err_to_name(ferr));
    }

    s_level        = gpio_get_level(SNIFF_GPIO);
    s_last_edge_us = esp_timer_get_time();

    // The BSP may or may not have installed the shared ISR service
    // already (touch controller); either answer is fine.
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(gpio_isr_handler_add(SNIFF_GPIO, edge_isr, NULL));

    // Core 0 with the other producers, above the event watcher (5):
    // pulse-queue backpressure is the one deadline in this build.
    xTaskCreatePinnedToCore(sniffer_task, "j1850_sniff", 4096, NULL, 7, NULL, 0);
    ESP_LOGI(TAG, "passive sniffer on GPIO%d (read-only build)", CONFIG_VROD_J1850_RX_GPIO);
}

void j1850_sniffer_get_stats(j1850_sniffer_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_mux);
    out->pin_level = gpio_get_level(SNIFF_GPIO) != 0;
}

int j1850_sniffer_sample_pin_mv(void)
{
    static adc_oneshot_unit_handle_t s_adc;
    static adc_cali_handle_t         s_cali;
    static adc_unit_t                s_unit;
    static adc_channel_t             s_chan;
    static bool                      s_tried, s_ok;

    if (!s_tried) {
        s_tried = true;
        if (adc_oneshot_io_to_channel(SNIFF_GPIO, &s_unit, &s_chan) != ESP_OK) {
            ESP_LOGW(TAG, "GPIO%d has no ADC channel; bench voltage unavailable",
                     CONFIG_VROD_J1850_RX_GPIO);
            return -1;
        }
        const adc_oneshot_unit_init_cfg_t ucfg = {.unit_id = s_unit};
        if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK)
            return -1;
        const adc_cali_curve_fitting_config_t ccfg = {
            .unit_id  = s_unit,
            .chan     = s_chan,
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&ccfg, &s_cali) != ESP_OK)
            return -1;
        s_ok = true;
    }
    if (!s_ok)
        return -1;

    // Configuring the channel flips the pad to its analog function; the
    // digital input (and the edge IRQ with it) is dead until gpio_config
    // restores it below. Sub-millisecond window, bench-screen only.
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    int raw = 0, mv = -1;
    if (adc_oneshot_config_channel(s_adc, s_chan, &chan_cfg) == ESP_OK &&
        adc_oneshot_read(s_adc, s_chan, &raw) == ESP_OK) {
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK)
            mv = -1;
    }
    ESP_ERROR_CHECK(gpio_config(&s_pin_cfg));
    return mv;
}
