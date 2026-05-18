#include "gear_indicator.h"
#include "lvgl.h"
#include "theme.h"

LV_FONT_DECLARE(jbm_bold_72);
LV_FONT_DECLARE(jbm_bold_33);

typedef struct {
    lv_obj_t *value;
    gear_t    last_gear;
    bool      has_value;
} gear_data_t;

lv_obj_t *gear_indicator_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 80, 110);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // "GEAR" caption above the big number, so it's unambiguous which is which.
    lv_obj_t *caption = lv_label_create(cont);
    lv_obj_set_style_text_color(caption, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(caption, &jbm_bold_33, 0);
    lv_label_set_text(caption, "GEAR");
    lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *value = lv_label_create(cont);
    lv_obj_set_style_text_color(value, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(value, &jbm_bold_72, 0);
    lv_label_set_text(value, "N");
    lv_obj_align(value, LV_ALIGN_BOTTOM_MID, 0, 0);

    gear_data_t *gd = lv_malloc(sizeof(gear_data_t));
    gd->value = value;
    gd->last_gear = GEAR_NEUTRAL;
    gd->has_value = false;
    lv_obj_set_user_data(cont, gd);
    return cont;
}

void gear_indicator_set(lv_obj_t *cont, gear_t gear)
{
    gear_data_t *gd = lv_obj_get_user_data(cont);
    if (!gd) return;
    if (gd->has_value && gd->last_gear == gear) return;
    gd->last_gear = gear;
    gd->has_value = true;
    const char *text;
    switch (gear) {
        case GEAR_NEUTRAL: text = "N"; break;
        case GEAR_1:       text = "1"; break;
        case GEAR_2:       text = "2"; break;
        case GEAR_3:       text = "3"; break;
        case GEAR_4:       text = "4"; break;
        case GEAR_5:       text = "5"; break;
        case GEAR_6:       text = "6"; break;
        default:           text = "-"; break;
    }
    lv_label_set_text(gd->value, text);
}
