#include "screen_settings.h"
#include "ui_manager.h"
#include "theme.h"
#include <stdint.h>

LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);

static void back_pressed_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_ride();
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
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -120);

    lv_obj_t *placeholder = lv_label_create(scr);
    lv_label_set_text(placeholder,
        "Stage 1 stub\n\nUnits, brightness, trip reset\nland in Stage 2");
    lv_obj_set_style_text_color(placeholder, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(placeholder, &jbm_bold_33, 0);
    lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(placeholder, LV_ALIGN_CENTER, 0, -10);

    // Big "Back" button at the bottom — glove-friendly tap target sitting
    // well inside the round bezel.
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_size(back, 260, 90);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -90);
    lv_obj_set_style_bg_color(back, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_radius(back, 12, 0);
    lv_obj_add_event_cb(back, back_pressed_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "BACK");
    lv_obj_set_style_text_color(back_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(back_lbl, &jbm_bold_45, 0);
    lv_obj_center(back_lbl);

    return scr;
}
