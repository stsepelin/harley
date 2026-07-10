#include "screen_settings.h"
#include "theme.h"
#include "ui_manager.h"
#include <stdint.h>

LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);

// Settings is now a menu of sub-pages (General / Trip / Bluetooth, plus Bench
// on sniffer builds). Each row navigates to its own screen; the controls that
// used to live here moved to screen_settings_general.c.
#define ROW_W 540

static lv_obj_t *make_row(lv_obj_t *parent, int32_t y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, ROW_W, 80);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static void caption(lv_obj_t *row, const char *text, lv_align_t align, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, &jbm_bold_33, 0);
    lv_obj_align(lbl, align, 0, 0);
}

// A navigable menu row: label on the left, chevron on the right, tap to open.
static void nav_row(lv_obj_t *scr, const char *label, int32_t y, lv_event_cb_t cb)
{
    lv_obj_t *row = make_row(scr, y);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
    caption(row, label, LV_ALIGN_LEFT_MID, VROD_TEXT);
    caption(row, ">", LV_ALIGN_RIGHT_MID, VROD_ORANGE);
}

static void general_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings_general();
}
static void trip_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings_trip();
}
static void bluetooth_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings_bluetooth();
}
#if CONFIG_VROD_J1850_SNIFFER
static void bench_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_bench();
}
#endif
static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_home();  // apply the layout chosen in settings
}

lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    nav_row(scr, "GENERAL", 150, general_cb);
    nav_row(scr, "TRIP", 250, trip_cb);
    nav_row(scr, "BLUETOOTH", 350, bluetooth_cb);
#if CONFIG_VROD_J1850_SNIFFER
    nav_row(scr, "BENCH", 450, bench_cb);
#endif

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

    return scr;
}
