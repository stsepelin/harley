#include "screen_settings_trip.h"
#include "settings_store.h"
#include "theme.h"
#include "ui_manager.h"
#include "units.h"
#include "vehicle_data.h"
#include <stdint.h>
#include <stdio.h>
#if CONFIG_VROD_J1850
#include "j1850_driver.h"
#include "odo_store.h"
#endif

LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);

#define ROW_W 540

static lv_obj_t   *s_odo_value;
static lv_obj_t   *s_trip_dist[2];
static lv_obj_t   *s_trip_econ[2];  // per-trip average economy
static lv_timer_t *s_refresh;

static void fmt_tenths(char *buf, size_t n, uint32_t m, display_units_t u)
{
    uint32_t t = units_distance_tenths(m, u);
    snprintf(buf, n, "%lu.%lu %s", (unsigned long)(t / 10), (unsigned long)(t % 10),
             units_distance_label(u));
}

static void fmt_econ(char *buf, size_t n, uint32_t x10, display_units_t u)
{
    if (x10 == 0)
        snprintf(buf, n, "-- %s", units_econ_label(u));  // too slow / no data yet
    else
        snprintf(buf, n, "%lu.%lu %s", (unsigned long)(x10 / 10), (unsigned long)(x10 % 10),
                 units_econ_label(u));
}

// Odometer + trips change as the bike rolls; poll vehicle_data so the page
// stays live (reflecting a reset / set-odometer immediately).
static void refresh(lv_timer_t *t)
{
    (void)t;
    vehicle_data_t vd;
    vehicle_data_get(&vd);
    display_units_t u = settings_store_current()->units;
    char            buf[24];

    snprintf(buf, sizeof(buf), "%lu %s", (unsigned long)units_distance_whole(vd.odometer_m, u),
             units_distance_label(u));
    lv_label_set_text(s_odo_value, buf);
    fmt_tenths(buf, sizeof(buf), vd.trip1_m, u);
    lv_label_set_text(s_trip_dist[0], buf);
    fmt_tenths(buf, sizeof(buf), vd.trip2_m, u);
    lv_label_set_text(s_trip_dist[1], buf);

    // Per-trip average economy.
    fmt_econ(buf, sizeof(buf), units_econ_x10(vd.trip1_fuel_ticks, vd.trip1_m, u), u);
    lv_label_set_text(s_trip_econ[0], buf);
    fmt_econ(buf, sizeof(buf), units_econ_x10(vd.trip2_fuel_ticks, vd.trip2_m, u), u);
    lv_label_set_text(s_trip_econ[1], buf);
}

static void reset_trip(int idx)
{
#if CONFIG_VROD_J1850
    j1850_driver_reset_trip(idx);
    odo_store_flush();  // persist the zeroed trip immediately
#else
    (void)idx;  // sim build has no producer; the button is a no-op
#endif
}

static void reset0_cb(lv_event_t *e)
{
    (void)e;
    reset_trip(0);
}
static void reset1_cb(lv_event_t *e)
{
    (void)e;
    reset_trip(1);
}
static void odo_row_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings_odoset();
}
static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings();
}

static lv_obj_t *card(lv_obj_t *scr, int32_t y, int32_t h)
{
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, ROW_W, h);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, lv_align_t align, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, &jbm_bold_33, 0);
    lv_obj_align(lbl, align, 0, 0);
    return lbl;
}

// A trip card, 2x2: name (top-left) + live distance (bottom-left); average
// economy (top-right, dim) + RESET button (bottom-right).
static void trip_card(lv_obj_t *scr, int idx, const char *name, int32_t y, lv_event_cb_t reset_cb)
{
    lv_obj_t *row = card(scr, y, 128);
    label(row, name, LV_ALIGN_TOP_LEFT, VROD_TEXT);
    s_trip_dist[idx] = label(row, "0.0", LV_ALIGN_BOTTOM_LEFT, VROD_ORANGE);
    s_trip_econ[idx] = label(row, "--", LV_ALIGN_TOP_RIGHT, VROD_TEXT_DIM);

    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, 150, 52);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);  // sits below the avg-econ line
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "RESET");
    lv_obj_set_style_text_color(bl, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(bl, &jbm_bold_33, 0);
    lv_obj_center(bl);
}

lv_obj_t *screen_settings_trip_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "TRIP");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 45);

    // ODOMETER — tap the row to set the value (one-time mileage entry).
    lv_obj_t *odo = card(scr, 130, 80);
    lv_obj_add_flag(odo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(odo, odo_row_cb, LV_EVENT_CLICKED, NULL);
    label(odo, "ODOMETER", LV_ALIGN_LEFT_MID, VROD_TEXT);
    s_odo_value = label(odo, "0", LV_ALIGN_RIGHT_MID, VROD_ORANGE);

    trip_card(scr, 0, "TRIP1", 240, reset0_cb);
    trip_card(scr, 1, "TRIP2", 380, reset1_cb);

    lv_obj_t *back = lv_button_create(scr);
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

    refresh(NULL);  // prime the labels before the first timer tick
    if (!s_refresh)
        s_refresh = lv_timer_create(refresh, 500, NULL);
    return scr;
}
