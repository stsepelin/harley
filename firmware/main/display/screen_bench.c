#include "screen_bench.h"
#include "j1850_sniffer.h"
#include "theme.h"
#include "ui_manager.h"
#include <stdio.h>
#if CONFIG_VROD_J1850_GLITCH_SWEEP
#include "esp_timer.h"
#endif
#if CONFIG_VROD_J1850_ADC_GPIO >= 0
#include "j1850_adc_probe.h"
#endif
#if CONFIG_VROD_RIDE_LOG
#include "ride_log.h"
#endif

LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);
LV_FONT_DECLARE(jbm_bold_26);

// Same bezel-clearance geometry as the settings screen.
#define ROW_W 540

// node B (RX divider output) → bus voltage: bus_mv = node_B_mv * 147/47
// (10k/4.7k inverse). The amplitude probe already applies this for its
// serial log; the screen re-applies it for display.
#define BUS_FROM_PIN_MV(mv) ((mv) * 147 / 47)

#define REFRESH_MS 500

static lv_obj_t *s_scr;
static lv_obj_t *s_amp_value;
static lv_obj_t *s_amp_lo;
static lv_obj_t *s_line_value;
static lv_obj_t *s_frames_value;
static lv_obj_t *s_last_value;
#if CONFIG_VROD_J1850_GLITCH_SWEEP
static lv_obj_t *s_sweep_value;
#endif
#if CONFIG_VROD_RIDE_LOG
static lv_obj_t        *s_ridelog_value;
static lv_obj_t        *s_rec_lbl;
static lv_obj_t        *s_clr_lbl;
static ride_log_state_t s_shown_rl_state  = -1;
static uint32_t         s_shown_rl_frames = UINT32_MAX;
static uint32_t         s_shown_rl_drop   = UINT32_MAX;
#endif

// Skip-if-unchanged caches (house rule: setters never re-render for
// the same value).
static int      s_shown_amp_max = -2;
static bool     s_shown_level;
static uint32_t s_shown_edges  = UINT32_MAX;
static uint32_t s_shown_frames = UINT32_MAX;
static uint32_t s_shown_crcbad;
static uint32_t s_shown_ovr;

static lv_obj_t *make_row(lv_obj_t *parent, int32_t height, int32_t y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, ROW_W, height);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *make_caption(lv_obj_t *row, const char *text, lv_align_t align, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, &jbm_bold_33, 0);
    lv_obj_align(lbl, align, 0, 0);
    return lbl;
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    // The screen object outlives visibility; skip work while hidden.
    if (lv_screen_active() != s_scr)
        return;

    char buf[48];

#if CONFIG_VROD_J1850_ADC_GPIO >= 0
    // Bus amplitude from the dedicated ADC probe (GPIO 22, second wire
    // off node B). On this active-low bus, MAX = idle resting (HIGH)
    // level — the number Stage 4 TX must overcome. MIN reads the driven
    // (LOW) level only if a sample landed inside a pulse.
    int amp_max, amp_min;
    if (j1850_adc_probe_get(&amp_max, &amp_min) && amp_max != s_shown_amp_max) {
        s_shown_amp_max = amp_max;
        int hi          = BUS_FROM_PIN_MV(amp_max);
        int lo          = BUS_FROM_PIN_MV(amp_min);
        snprintf(buf, sizeof(buf), "%d.%02dV", hi / 1000, (hi % 1000) / 10);
        lv_label_set_text(s_amp_value, buf);
        snprintf(buf, sizeof(buf), "lo %d.%02dV", lo / 1000, (lo % 1000) / 10);
        lv_label_set_text(s_amp_lo, buf);
    }
#endif

    j1850_sniffer_stats_t st;
    j1850_sniffer_get_stats(&st);

#if CONFIG_VROD_J1850_GLITCH_SWEEP
    if (st.sweep_active) {
        int rem = (int)((st.sweep_deadline_us - esp_timer_get_time()) / 1000000);
        if (rem < 0)
            rem = 0;
        snprintf(buf, sizeof(buf), "SWEEP %luns  %ds  p%lu", (unsigned long)st.sweep_ns, rem,
                 (unsigned long)st.sweep_pass);
        lv_label_set_text(s_sweep_value, buf);
    }
#endif

    if (st.pin_level != s_shown_level || st.edges_last_period != s_shown_edges) {
        s_shown_level = st.pin_level;
        s_shown_edges = st.edges_last_period;
        snprintf(buf, sizeof(buf), "%s  %lu edges", st.pin_level ? "HIGH" : "LOW",
                 (unsigned long)st.edges_last_period);
        lv_label_set_text(s_line_value, buf);
        lv_obj_set_style_text_color(s_line_value,
                                    lv_color_hex(st.pin_level ? VROD_ORANGE : VROD_TEXT_DIM), 0);
    }

    if (st.frames != s_shown_frames || st.crc_bad != s_shown_crcbad || st.overruns != s_shown_ovr) {
        s_shown_crcbad = st.crc_bad;
        s_shown_ovr    = st.overruns;
        snprintf(buf, sizeof(buf), "%lu  bad %lu  ovr %lu", (unsigned long)st.frames,
                 (unsigned long)st.crc_bad, (unsigned long)st.overruns);
        lv_label_set_text(s_frames_value, buf);

        // New frame arrived → refresh the hex line too.
        if (st.frames != s_shown_frames && st.last_len > 0) {
            char hex[48];
            int  pos = 0;
            for (size_t i = 0; i < st.last_len && pos < (int)sizeof(hex) - 4; i++) {
                pos += snprintf(hex + pos, sizeof(hex) - (size_t)pos, "%02X ", st.last_frame[i]);
            }
            lv_label_set_text(s_last_value, hex);
            lv_obj_set_style_text_color(s_last_value,
                                        lv_color_hex(st.last_crc_ok ? VROD_TEXT : VROD_RED), 0);
        }
        s_shown_frames = st.frames;
    }

#if CONFIG_VROD_RIDE_LOG
    ride_log_stats_t rl;
    ride_log_get_stats(&rl);
    if (rl.state != s_shown_rl_state || rl.frames != s_shown_rl_frames ||
        rl.dropped != s_shown_rl_drop) {
        s_shown_rl_state  = rl.state;
        s_shown_rl_frames = rl.frames;
        s_shown_rl_drop   = rl.dropped;
        const char *name;
        uint32_t    col;
        switch (rl.state) {
        case RIDE_LOG_RECORDING:
            name = "REC";
            col  = VROD_RED;
            break;
        case RIDE_LOG_IDLE:
            name = "IDLE";
            col  = VROD_TEXT;
            break;
        case RIDE_LOG_NO_CARD:
            name = "NO CARD";
            col  = VROD_TEXT_DIM;
            break;
        case RIDE_LOG_ERROR:
            name = "ERROR";
            col  = VROD_RED;
            break;
        default:
            name = "--";
            col  = VROD_TEXT_DIM;
            break;
        }
        if (rl.state == RIDE_LOG_RECORDING || rl.state == RIDE_LOG_IDLE) {
            snprintf(buf, sizeof(buf), "%s  %luf d%lu  %lu/%luMB", name, (unsigned long)rl.frames,
                     (unsigned long)rl.dropped, (unsigned long)rl.mb_used,
                     (unsigned long)rl.mb_total);
        } else {
            snprintf(buf, sizeof(buf), "%s", name);
        }
        lv_label_set_text(s_ridelog_value, buf);
        lv_obj_set_style_text_color(s_ridelog_value, lv_color_hex(col), 0);
        lv_label_set_text(s_rec_lbl, rl.state == RIDE_LOG_RECORDING ? "STOP" : "REC");
    }
#endif
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings();
}

#if CONFIG_VROD_RIDE_LOG
static void rec_cb(lv_event_t *e)
{
    (void)e;
    ride_log_toggle();
}

// Delete all saved ride logs from the card. Shows the count on the button (or
// keeps "CLR" when refused - recording, or no card).
static void clear_cb(lv_event_t *e)
{
    (void)e;
    int n = ride_log_purge();
    if (n >= 0)
        lv_label_set_text_fmt(s_clr_lbl, "%d", n);
}
#endif

lv_obj_t *screen_bench_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "BENCH");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

#if CONFIG_VROD_J1850_GLITCH_SWEEP
    // Glitch-window sweep status: current window + seconds left + pass,
    // so the rider knows when to blip the throttle mid-window.
    s_sweep_value = lv_label_create(s_scr);
    lv_label_set_text(s_sweep_value, "SWEEP --");
    lv_obj_set_style_text_color(s_sweep_value, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(s_sweep_value, &jbm_bold_26, 0);
    lv_obj_align(s_sweep_value, LV_ALIGN_TOP_MID, 0, 102);
#endif

    // BUS AMP — idle (HIGH) resting level + driven (LOW) level from the
    // dedicated ADC probe on GPIO 22. Needs CONFIG_VROD_J1850_ADC_GPIO
    // and a second wire off node B; shows "probe off" otherwise.
    lv_obj_t *amp_row = make_row(s_scr, 130, 130);
    make_caption(amp_row, "BUS AMP", LV_ALIGN_TOP_LEFT, VROD_TEXT);
#if CONFIG_VROD_J1850_ADC_GPIO >= 0
    s_amp_value = make_caption(amp_row, "--", LV_ALIGN_TOP_RIGHT, VROD_ORANGE);
    s_amp_lo    = lv_label_create(amp_row);
    lv_label_set_text(s_amp_lo, "lo --");
#else
    s_amp_value = make_caption(amp_row, "off", LV_ALIGN_TOP_RIGHT, VROD_TEXT_DIM);
    s_amp_lo    = lv_label_create(amp_row);
    lv_label_set_text(s_amp_lo, "set ADC_GPIO");
#endif
    lv_obj_set_style_text_color(s_amp_lo, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(s_amp_lo, &jbm_bold_26, 0);
    lv_obj_align(s_amp_lo, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // LINE — live logic level + edge count over the last 10 s window.
    lv_obj_t *line_row = make_row(s_scr, 80, 280);
    make_caption(line_row, "LINE", LV_ALIGN_LEFT_MID, VROD_TEXT);
    s_line_value = make_caption(line_row, "--", LV_ALIGN_RIGHT_MID, VROD_TEXT_DIM);

    // FRAMES — decoder counters, same as the serial stats line.
    lv_obj_t *frames_row = make_row(s_scr, 80, 380);
    make_caption(frames_row, "FRAMES", LV_ALIGN_LEFT_MID, VROD_TEXT);
    s_frames_value = make_caption(frames_row, "0", LV_ALIGN_RIGHT_MID, VROD_ORANGE);

    // LAST — most recent frame, red when its CRC failed.
    lv_obj_t *last_row = make_row(s_scr, 100, 480);
    make_caption(last_row, "LAST", LV_ALIGN_TOP_LEFT, VROD_TEXT);
    s_last_value = lv_label_create(last_row);
    lv_label_set_text(s_last_value, "--");
    lv_obj_set_style_text_color(s_last_value, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(s_last_value, &jbm_bold_26, 0);
    lv_obj_align(s_last_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);

#if CONFIG_VROD_RIDE_LOG
    // Ride-log status + REC/STOP toggle for laptop-free capture. The status
    // line sits in the gap above the buttons; the toggle sits beside BACK.
    s_ridelog_value = lv_label_create(s_scr);
    lv_label_set_text(s_ridelog_value, "--");
    lv_obj_set_style_text_color(s_ridelog_value, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(s_ridelog_value, &jbm_bold_26, 0);
    lv_obj_align(s_ridelog_value, LV_ALIGN_TOP_MID, 0, 590);

    // Bottom row: REC | CLEAR | BACK (kept inside the round bezel's chord).
    lv_obj_t *rec = lv_button_create(s_scr);
    lv_obj_set_size(rec, 170, 80);
    lv_obj_align(rec, LV_ALIGN_BOTTOM_MID, -175, -80);
    lv_obj_set_style_bg_color(rec, lv_color_hex(VROD_RED), 0);
    lv_obj_set_style_radius(rec, 12, 0);
    lv_obj_add_event_cb(rec, rec_cb, LV_EVENT_CLICKED, NULL);
    s_rec_lbl = lv_label_create(rec);
    lv_label_set_text(s_rec_lbl, "REC");
    lv_obj_set_style_text_color(s_rec_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_rec_lbl, &jbm_bold_45, 0);
    lv_obj_center(s_rec_lbl);

    lv_obj_t *clr = lv_button_create(s_scr);
    lv_obj_set_size(clr, 170, 80);
    lv_obj_align(clr, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_color(clr, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(clr, 12, 0);
    lv_obj_add_event_cb(clr, clear_cb, LV_EVENT_CLICKED, NULL);
    s_clr_lbl = lv_label_create(clr);
    lv_label_set_text(s_clr_lbl, "CLR");
    lv_obj_set_style_text_color(s_clr_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_clr_lbl, &jbm_bold_45, 0);
    lv_obj_center(s_clr_lbl);

    lv_obj_t *back = lv_button_create(s_scr);
    lv_obj_set_size(back, 170, 80);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 175, -80);
#else
    lv_obj_t *back = lv_button_create(s_scr);
    lv_obj_set_size(back, 260, 80);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -80);
#endif
    lv_obj_set_style_bg_color(back, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_radius(back, 12, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "BACK");
    lv_obj_set_style_text_color(back_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(back_lbl, &jbm_bold_45, 0);
    lv_obj_center(back_lbl);

    lv_timer_create(refresh_cb, REFRESH_MS, NULL);
    return s_scr;
}
