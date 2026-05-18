#include "screen_settings.h"
#include "settings_store.h"
#include "theme.h"
#include "ui_manager.h"
#include "bsp/display.h"
#include <stdint.h>
#include <stdio.h>

LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);

// In-memory edit buffer. Mutated by the row handlers, applied (validated +
// saved + made current) on every change so that backing out via BACK
// always lands on the persisted state — no separate save step.
static settings_t s_pending;

static lv_obj_t *s_units_value;
static lv_obj_t *s_brightness_value;

static void units_row_clicked_cb(lv_event_t *e)
{
    (void)e;
    s_pending.units = (s_pending.units == UNITS_KPH) ? UNITS_MPH : UNITS_KPH;
    lv_label_set_text(s_units_value, units_distance_label(s_pending.units));
    settings_store_apply(&s_pending);
}

static void brightness_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(slider);
    s_pending.brightness = (uint8_t)v;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", v);
    lv_label_set_text(s_brightness_value, buf);
    // Live preview the backlight while dragging; persistence happens on
    // release to avoid spamming NVS with every animation frame.
    bsp_display_brightness_set(v);
}

static void brightness_released_cb(lv_event_t *e)
{
    (void)e;
    settings_store_apply(&s_pending);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_ride();
}

// Returns the row container with the left-aligned caption already laid
// in; caller drops the value widget on the right-hand side.
static lv_obj_t *make_row(lv_obj_t *parent, const char *caption, int32_t y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 580, 100);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 16, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, caption);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &jbm_bold_33, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    return row;
}

lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Snapshot the current persisted settings; handlers mutate this copy.
    s_pending = *settings_store_current();

    // Title.
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

    // Units row.
    lv_obj_t *units_row = make_row(scr, "UNITS", 200);
    lv_obj_add_flag(units_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(units_row, units_row_clicked_cb, LV_EVENT_CLICKED, NULL);

    s_units_value = lv_label_create(units_row);
    lv_label_set_text(s_units_value, units_distance_label(s_pending.units));
    lv_obj_set_style_text_color(s_units_value, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(s_units_value, &jbm_bold_45, 0);
    lv_obj_align(s_units_value, LV_ALIGN_RIGHT_MID, 0, 0);

    // Brightness row: taller than the units row to fit a caption+value at
    // the top and a slider at the bottom with breathing room between
    // them. Built inline rather than via make_row because the caption
    // sits at TOP_LEFT here, not LEFT_MID.
    lv_obj_t *bright_row = lv_obj_create(scr);
    lv_obj_set_size(bright_row, 580, 130);
    lv_obj_align(bright_row, LV_ALIGN_TOP_MID, 0, 320);
    lv_obj_set_style_bg_color(bright_row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(bright_row, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(bright_row, 1, 0);
    lv_obj_set_style_radius(bright_row, 12, 0);
    lv_obj_set_style_pad_all(bright_row, 16, 0);
    lv_obj_remove_flag(bright_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bright_lbl = lv_label_create(bright_row);
    lv_label_set_text(bright_lbl, "BRIGHTNESS");
    lv_obj_set_style_text_color(bright_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bright_lbl, &jbm_bold_33, 0);
    lv_obj_align(bright_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_brightness_value = lv_label_create(bright_row);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", s_pending.brightness);
    lv_label_set_text(s_brightness_value, buf);
    lv_obj_set_style_text_color(s_brightness_value, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(s_brightness_value, &jbm_bold_33, 0);
    lv_obj_align(s_brightness_value, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *slider = lv_slider_create(bright_row);
    lv_obj_set_size(slider, 540, 18);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    // Slider min matches the visible-PWM floor so the user can't drag
    // the screen into a black state. See SETTINGS_BRIGHTNESS_MIN in
    // settings.h for the why.
    lv_slider_set_range(slider, SETTINGS_BRIGHTNESS_MIN, 100);
    lv_slider_set_value(slider, s_pending.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_changed_cb,  LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, brightness_released_cb, LV_EVENT_RELEASED,      NULL);

    // BACK button — glove-friendly tap target inside the round bezel.
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_size(back, 260, 90);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -90);
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
