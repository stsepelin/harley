#include "speed_display.h"
#include "lvgl.h"
#include "theme.h"
#include <stdio.h>

// JetBrains Mono Bold is monospaced by design — each digit has the same
// advance width — so a single label is enough to keep the number "tabular"
// without the per-slot trick.
LV_FONT_DECLARE(jbm_bold_144);
LV_FONT_DECLARE(jbm_bold_33);

typedef struct {
    lv_obj_t *value_label;
    uint16_t  last_kmh;
    bool      has_value;
} speed_data_t;

lv_obj_t *speed_display_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 380, 240);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *value = lv_label_create(cont);
    lv_obj_set_style_text_color(value, lv_color_white(), 0);
    lv_obj_set_style_text_font(value, &jbm_bold_144, 0);
    lv_label_set_text(value, "0");
    lv_obj_align(value, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t *unit = lv_label_create(cont);
    lv_obj_set_style_text_color(unit, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(unit, &jbm_bold_33, 0);
    lv_label_set_text(unit, "km/h");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 95);

    speed_data_t *sd = lv_malloc(sizeof(speed_data_t));
    sd->value_label = value;
    sd->last_kmh = 0;
    sd->has_value = false;
    lv_obj_set_user_data(cont, sd);
    return cont;
}

void speed_display_set_value(lv_obj_t *cont, uint16_t kmh)
{
    speed_data_t *sd = lv_obj_get_user_data(cont);
    if (!sd) return;
    if (sd->has_value && sd->last_kmh == kmh) return;
    sd->last_kmh = kmh;
    sd->has_value = true;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)kmh);
    lv_label_set_text(sd->value_label, buf);
}
