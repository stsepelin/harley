#include "screen_bench.h"
#include "j1850_sniffer.h"
#include "theme.h"
#include "ui_manager.h"
#include <stdio.h>

LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);
LV_FONT_DECLARE(jbm_bold_26);

// Same bezel-clearance geometry as the settings screen.
#define ROW_W 540

// Divider ratio for the back-calculated bus voltage: the pin sees
// R2/(R1+R2) of the bus node (10k/4.7k), so bus_mv = pin_mv * 147/47.
#define BUS_FROM_PIN_MV(mv) ((mv) * 147 / 47)

// 2 Hz: fast enough to watch a PSU knob, slow enough that the ADC's
// brief analog-mode window (which blinds the edge IRQ) stays harmless.
#define REFRESH_MS 500

static lv_obj_t *s_scr;
static lv_obj_t *s_pin_value;
static lv_obj_t *s_bus_value;
static lv_obj_t *s_line_value;
static lv_obj_t *s_frames_value;
static lv_obj_t *s_last_value;

// Skip-if-unchanged caches (house rule: setters never re-render for
// the same value).
static int      s_shown_mv = -2;  // -1 is a legal value ("N/A")
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
    // The screen object outlives visibility; only burn ADC samples (and
    // the edge-IRQ blind window they cost) while actually being looked at.
    if (lv_screen_active() != s_scr)
        return;

    char buf[48];

    int mv = j1850_sniffer_sample_pin_mv();
    if (mv != s_shown_mv) {
        s_shown_mv = mv;
        if (mv < 0) {
            lv_label_set_text(s_pin_value, "N/A");
            lv_label_set_text(s_bus_value, "no ADC on this pin");
        } else {
            snprintf(buf, sizeof(buf), "%d.%03dV", mv / 1000, mv % 1000);
            lv_label_set_text(s_pin_value, buf);
            int bus = BUS_FROM_PIN_MV(mv);
            snprintf(buf, sizeof(buf), "bus =%d.%02dV", bus / 1000, (bus % 1000) / 10);
            lv_label_set_text(s_bus_value, buf);
        }
    }

    j1850_sniffer_stats_t st;
    j1850_sniffer_get_stats(&st);

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
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings();
}

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

    // RX PIN — calibrated mV at the GPIO, and what that implies at the
    // bus node through the 10k/4.7k divider. 7.00V on the bench PSU
    // should read ~2.24V pin / ~7.0V bus.
    lv_obj_t *pin_row = make_row(s_scr, 130, 130);
    make_caption(pin_row, "RX PIN", LV_ALIGN_TOP_LEFT, VROD_TEXT);
    s_pin_value = make_caption(pin_row, "--", LV_ALIGN_TOP_RIGHT, VROD_ORANGE);
    s_bus_value = lv_label_create(pin_row);
    lv_label_set_text(s_bus_value, "--");
    lv_obj_set_style_text_color(s_bus_value, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(s_bus_value, &jbm_bold_26, 0);
    lv_obj_align(s_bus_value, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

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

    lv_obj_t *back = lv_button_create(s_scr);
    lv_obj_set_size(back, 260, 80);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -80);
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
