#include "clock_display.h"
#include "theme.h"
#include <stdio.h>

LV_FONT_DECLARE(jbm_bold_26);

typedef struct {
    lv_obj_t *label;
    uint8_t   last_h;
    uint8_t   last_m;
    bool      has_value;
} clock_data_t;

lv_obj_t *clock_display_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(lbl, "--:--");
    lv_obj_center(lbl);

    clock_data_t *cd = lv_malloc(sizeof(clock_data_t));
    cd->label = lbl;
    cd->last_h = cd->last_m = 0;
    cd->has_value = false;
    lv_obj_set_user_data(cont, cd);
    return cont;
}

void clock_display_set(lv_obj_t *cont, uint8_t hours, uint8_t minutes)
{
    clock_data_t *cd = lv_obj_get_user_data(cont);
    if (!cd) return;
    if (cd->has_value && cd->last_h == hours && cd->last_m == minutes) return;
    cd->last_h = hours;
    cd->last_m = minutes;
    cd->has_value = true;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)hours, (unsigned)minutes);
    lv_label_set_text(cd->label, buf);
}
