#include "j1850_sniffer.h"
#include "j1850_vpw.h"

#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include "j1850_parse.h"      // J1850_SPEED_DIVISOR for the capture hint line
#include "ride_log_format.h"
#if CONFIG_VROD_J1850
#include "j1850_driver.h"
#endif
#if CONFIG_VROD_RIDE_LOG
#include "ride_log.h"
#endif

static const char *TAG = "j1850";

#define SNIFF_GPIO ((gpio_num_t)CONFIG_VROD_J1850_RX_GPIO)
// 10.4 kbps VPW tops out below ~16k edges/s; 512 entries buffer ~30 ms
// of the densest possible traffic while the log task is writing.
#define PULSE_QUEUE_LEN 512
#define STATS_PERIOD_MS 10000

// Standard VPW: idle/recessive reads LOW at our pin, dominant reads
// HIGH — so the pin level IS the logical active level, no inversion.
// (An earlier "inverted" reading was the 500 ns glitch filter dropping
// slow recessive falling edges, not a real polarity flip; confirmed by
// the glitch-window sweep — see docs/captures/SESSION-2026-07-04.md.)
#define RX_PASSIVE_PHYS_LEVEL 0  // physical pin level while idle

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

// Live flex glitch filter, NULL when none is active. Clocked from the
// 40 MHz XTAL: the P4's 64-tick window field caps this at 1600 ns, so
// the window is set/torn-down at runtime (the sweep retunes it live).
static gpio_glitch_filter_handle_t s_filter;

static void j1850_set_glitch(uint32_t ns)
{
    if (s_filter) {
        gpio_glitch_filter_disable(s_filter);
        gpio_del_glitch_filter(s_filter);
        s_filter = NULL;
    }
    if (ns == 0) {
        ESP_LOGI(TAG, "glitch filter off");
        return;
    }
    const gpio_flex_glitch_filter_config_t fcfg = {
        .clk_src         = GLITCH_FILTER_CLK_SRC_XTAL,
        .gpio_num        = SNIFF_GPIO,
        .window_width_ns = ns,
        .window_thres_ns = ns,
    };
    esp_err_t err = gpio_new_flex_glitch_filter(&fcfg, &s_filter);
    if (err == ESP_OK) {
        ESP_ERROR_CHECK(gpio_glitch_filter_enable(s_filter));
        ESP_LOGI(TAG, "glitch filter %luns", (unsigned long)ns);
    } else {
        s_filter = NULL;  // out of range / no slot: run unfiltered
        ESP_LOGW(TAG, "glitch filter %luns rejected (%s); unfiltered", (unsigned long)ns,
                 esp_err_to_name(err));
    }
}

#if CONFIG_VROD_J1850_GLITCH_SWEEP
// Achievable windows only: 1600 ns is the XTAL-clocked hardware ceiling.
static const uint32_t SWEEP_NS[] = {0, 400, 800, 1200, 1600};
#define SWEEP_N        (sizeof(SWEEP_NS) / sizeof(SWEEP_NS[0]))
#define SWEEP_DWELL_MS 9000
#endif

// Restoring this after an ADC sample re-arms the digital input path.

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

// Capture-prep hints for the three fields still blocked on the bike. Speed:
// emit the NATIVE decoded value (raw / DIV, mph-native) so it compares to the
// stock speedometer (native miles) on a ride — DIV magnitude is provisional.
// Temp: emit
// the RAW byte plus the three candidate scalings so a two-point capture
// (cold ~ambient + fully warm) can solve the formula. Gear: emit the raw byte
// + decoded ladder position so a shift through 1-6 confirms the table (only
// N/1st/2nd seen so far). See j1850_parse.h and ride_log_format.h.
static void log_hint(const j1850_frame_t *f)
{
    static const uint8_t SPEED[] = {0x48, 0x29, 0x10, 0x02};
    static const uint8_t TEMP[]  = {0xA8, 0x49, 0x10, 0x10};
    static const uint8_t LOAD[]  = {0xA8, 0x3B, 0x10, 0x03};
    if (f->len >= 7 && memcmp(f->data, SPEED, 4) == 0) {
        unsigned raw = ((unsigned)f->data[4] << 8) | f->data[5];
        ESP_LOGI(TAG,
                 "speed: raw=%u -> %u mph (counts/%d, km/h-native; DIV provisional, GPS to lock)",
                 raw, raw / J1850_SPEED_DIVISOR, J1850_SPEED_DIVISOR);
    } else if (f->len >= 6 && memcmp(f->data, TEMP, 4) == 0) {
        unsigned r = f->data[4];
        ESP_LOGI(TAG, "temp: raw=0x%02X (%u) -> %d C (raw-40, ride-1 confirmed)", r, r,
                 (int)r - 40);
    } else if (f->len >= 6 && memcmp(f->data, LOAD, 4) == 0) {
        // A8 3B 10 is engine load/throttle, not gear (no gear sensor). Gear is
        // computed from the RPM:speed ratio (gear_calc), not this frame.
        ESP_LOGI(TAG, "load: raw=0x%02X (A8 3B 10 param; not gear)", f->data[4]);
    }
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

    // Corruption metric: clean VPW strictly alternates active/passive, so
    // two consecutive pulses at the SAME held level = a missed or spurious
    // edge (ringing/noise). On clean short leads this should be ~0; a
    // nonzero rate is the signal-integrity smell we're chasing.
    uint32_t same_level  = 0;
    int      prev_active = -1;

    // Bench bring-up diagnostic: dump the first RAW_DUMP_N real pulses as
    // (held-level, width-us) so we can tell VPW (clusters at 64/128/200)
    // from a timebase bug (scaled widths) from noise (chaotic tiny). Self-
    // limits, then the sniffer logs frames normally.
    uint32_t raw_dumped = 0;
#define RAW_DUMP_N 64

#if CONFIG_VROD_J1850_GLITCH_SWEEP
    size_t   sweep_i    = 0;
    uint32_t sweep_pass = 0;
    bool     win_clean[SWEEP_N];
    for (size_t i = 0; i < SWEEP_N; i++)
        win_clean[i] = true;
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.sweep_active      = true;
    s_stats.sweep_ns          = SWEEP_NS[0];
    s_stats.sweep_pass        = 0;
    s_stats.sweep_deadline_us = esp_timer_get_time() + (int64_t)SWEEP_DWELL_MS * 1000;
    portEXIT_CRITICAL(&s_stats_mux);
#endif

    for (;;) {
        pulse_evt_t   evt;
        j1850_frame_t frame;

        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(2)) == pdTRUE) {
            if (prev_active >= 0 && (int)(evt.active != 0) == prev_active)
                same_level++;
            prev_active = (evt.active != 0);
            if (raw_dumped < RAW_DUMP_N) {
                raw_dumped++;
                ESP_LOGI(TAG, "raw %2lu: %s %5lu us", (unsigned long)raw_dumped,
                         evt.active ? "HIGH" : "low ", (unsigned long)evt.dur_us);
            }
            if (j1850_vpw_rx_pulse(&rx, evt.active != 0, evt.dur_us, &frame)) {
                frames++;
                if (!frame.crc_ok)
                    crc_bad++;
                log_frame(&frame);
                log_hint(&frame);
                publish_frame(&frame, frames, crc_bad);
#if CONFIG_VROD_RIDE_LOG
                ride_log_frame(&frame);
#endif
#if CONFIG_VROD_J1850
                j1850_driver_feed(&frame);
#endif
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
            if (level == RX_PASSIVE_PHYS_LEVEL && last != flushed_edge_us &&
                idle_us > J1850_VPW_SOF_MAX_US + 60) {
                flushed_edge_us = last;
                if (j1850_vpw_rx_pulse(&rx, false, (uint32_t)idle_us, &frame)) {
                    frames++;
                    if (!frame.crc_ok)
                        crc_bad++;
                    log_frame(&frame);
                    log_hint(&frame);
                    publish_frame(&frame, frames, crc_bad);
#if CONFIG_VROD_RIDE_LOG
                    ride_log_frame(&frame);
#endif
#if CONFIG_VROD_J1850
                    j1850_driver_feed(&frame);
#endif
                }
            }
        }

        int64_t       now = esp_timer_get_time();
        const int64_t period_us =
#if CONFIG_VROD_J1850_GLITCH_SWEEP
            (int64_t)SWEEP_DWELL_MS * 1000;
#else
            (int64_t)STATS_PERIOD_MS * 1000;
#endif
        if (now - last_stats_us > period_us) {
            last_stats_us = now;
            // Edge rate is the wiring-health number: a real idle bus is
            // ~0; a floating pin or a DC level parked on the input
            // threshold shows up as thousands per period.
            portENTER_CRITICAL(&s_stats_mux);
            s_stats.edges_last_period = s_edges;
            s_stats.overruns          = s_overruns;
            portEXIT_CRITICAL(&s_stats_mux);
#if CONFIG_VROD_J1850_GLITCH_SWEEP
            ESP_LOGI(TAG, "filter=%luns [pass %lu]: frames=%lu badCRC=%lu ovr=%lu same-level=%lu",
                     (unsigned long)SWEEP_NS[sweep_i], (unsigned long)sweep_pass,
                     (unsigned long)frames, (unsigned long)crc_bad, (unsigned long)s_overruns,
                     (unsigned long)same_level);
            if (crc_bad != 0 || same_level != 0)
                win_clean[sweep_i] = false;
            if (++sweep_i >= SWEEP_N) {
                sweep_i = 0;
                sweep_pass++;
                uint32_t best = 0;
                for (size_t i = 0; i < SWEEP_N; i++)
                    if (win_clean[i] && SWEEP_NS[i] > best)
                        best = SWEEP_NS[i];
                ESP_LOGI(TAG, "sweep pass %lu done: largest all-clean window = %luns",
                         (unsigned long)sweep_pass, (unsigned long)best);
            }
            j1850_set_glitch(SWEEP_NS[sweep_i]);
            j1850_vpw_rx_init(&rx);  // drop any frame straddling the change
            frames      = 0;
            crc_bad     = 0;
            s_overruns  = 0;
            prev_active = -1;
            portENTER_CRITICAL(&s_stats_mux);
            s_stats.sweep_ns          = SWEEP_NS[sweep_i];
            s_stats.sweep_pass        = sweep_pass;
            s_stats.sweep_deadline_us = now + period_us;
            portEXIT_CRITICAL(&s_stats_mux);
#else
            ESP_LOGI(TAG, "stats: %lu frames, %lu bad CRC, %lu overruns, %lu edges, %lu same-level",
                     (unsigned long)frames, (unsigned long)crc_bad, (unsigned long)s_overruns,
                     (unsigned long)s_edges, (unsigned long)same_level);
#endif
            s_edges    = 0;
            same_level = 0;
        }
    }
}

void j1850_sniffer_start(void)
{
    s_queue = xQueueCreate(PULSE_QUEUE_LEN, sizeof(pulse_evt_t));
    configASSERT(s_queue);

    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << SNIFF_GPIO,
        .mode         = GPIO_MODE_INPUT,
        // The RX divider drives the pin at all times — no pulls.
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    // Hardware glitch filter suppresses pulses under the configured
    // window before they reach the interrupt matrix. On this bare
    // resistive RX front end a window >=~500 ns eats the slow recessive
    // falling edge, so the tuned value is small (0 while unresolved).
    // The sweep build retunes it live from the task.
#if CONFIG_VROD_J1850_GLITCH_SWEEP
    j1850_set_glitch(SWEEP_NS[0]);
#else
    j1850_set_glitch(CONFIG_VROD_J1850_GLITCH_NS);
#endif

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
