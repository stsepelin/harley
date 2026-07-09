#include "screen_settings_odoset.h"
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

// Steps are in whole display units (km or mi). One tap on STEP cycles them.
static const uint32_t STEPS[] = {1000, 100, 10, 1};
#define NSTEPS (sizeof(STEPS) / sizeof(STEPS[0]))

static uint32_t        s_val;      // pending value, in whole display units
static int             s_step_ix;  // index into STEPS
static display_units_t s_u;        // display unit at entry
static lv_obj_t       *s_value_lbl;
static lv_obj_t       *s_step_lbl;

static void refresh_value(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu %s", (unsigned long)s_val, units_distance_label(s_u));
    lv_label_set_text(s_value_lbl, buf);
}

static void refresh_step(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "STEP: %lu", (unsigned long)STEPS[s_step_ix]);
    lv_label_set_text(s_step_lbl, buf);
}

static void step_cb(lv_event_t *e)
{
    (void)e;
    s_step_ix = (s_step_ix + 1) % (int)NSTEPS;
    refresh_step();
}
static void plus_cb(lv_event_t *e)
{
    (void)e;
    s_val += STEPS[s_step_ix];
    refresh_value();
}
static void minus_cb(lv_event_t *e)
{
    (void)e;
    uint32_t st = STEPS[s_step_ix];
    s_val       = (s_val >= st) ? s_val - st : 0;
    refresh_value();
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    // Whole display units -> metres (exact for km; 1 mi = 1609.344 m).
    uint32_t m =
        (s_u == UNITS_MPH) ? (uint32_t)((uint64_t)s_val * 1609344u / 1000u) : s_val * 1000u;
#if CONFIG_VROD_J1850
    j1850_driver_set_odometer(m);
    odo_store_flush();
#else
    (void)m;
#endif
    ui_manager_show_settings_trip();
}
static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings_trip();  // cancel
}

static lv_obj_t *button(lv_obj_t *scr, const char *text, lv_align_t align, int32_t x, int32_t y,
                        int32_t w, uint32_t bg, uint32_t fg, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_set_size(b, w, 80);
    lv_obj_align(b, align, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_set_style_text_font(l, &jbm_bold_45, 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t *screen_settings_odoset_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Seed the pending value from the current odometer, in the display unit.
    vehicle_data_t vd;
    vehicle_data_get(&vd);
    s_u       = settings_store_current()->units;
    s_val     = units_distance_whole(vd.odometer_m, s_u);
    s_step_ix = 0;

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SET ODO");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    s_value_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_value_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_value_lbl, &jbm_bold_45, 0);
    lv_obj_align(s_value_lbl, LV_ALIGN_TOP_MID, 0, 150);
    refresh_value();

    // STEP selector (tap to cycle 1000/100/10/1).
    lv_obj_t *step = lv_button_create(scr);
    lv_obj_set_size(step, 300, 70);
    lv_obj_align(step, LV_ALIGN_TOP_MID, 0, 230);
    lv_obj_set_style_bg_color(step, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(step, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(step, 1, 0);
    lv_obj_set_style_radius(step, 12, 0);
    lv_obj_add_event_cb(step, step_cb, LV_EVENT_CLICKED, NULL);
    s_step_lbl = lv_label_create(step);
    lv_obj_set_style_text_color(s_step_lbl, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(s_step_lbl, &jbm_bold_33, 0);
    lv_obj_center(s_step_lbl);
    refresh_step();

    // -  / +  steppers.
    button(scr, "-", LV_ALIGN_TOP_MID, -150, 320, 130, VROD_ORANGE, 0x000000, minus_cb);
    button(scr, "+", LV_ALIGN_TOP_MID, 150, 320, 130, VROD_ORANGE, 0x000000, plus_cb);

    // SAVE (commit) + BACK (cancel).
    button(scr, "SAVE", LV_ALIGN_BOTTOM_MID, -140, -60, 240, VROD_ORANGE, 0x000000, save_cb);
    button(scr, "BACK", LV_ALIGN_BOTTOM_MID, 140, -60, 240, 0x2A2A2A, 0xFFFFFF, back_cb);

    return scr;
}
