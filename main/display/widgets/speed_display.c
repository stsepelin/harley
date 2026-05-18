#include "speed_display.h"
#include "lvgl.h"
#include "theme.h"
#include "widget_util.h"
#include <stdio.h>

// JetBrains Mono Bold is monospaced by design — each digit has the same
// advance width — so a single label is enough to keep the number "tabular"
// without the per-slot trick.
LV_FONT_DECLARE(jbm_bold_144);
LV_FONT_DECLARE(jbm_bold_33);

typedef struct {
    lv_obj_t       *value_label;
    lv_obj_t       *unit_label;
    uint16_t        last_kmh;
    display_units_t last_units;
    bool            has_value;
} speed_data_t;

lv_obj_t *speed_display_create(lv_obj_t *parent)
{
    lv_obj_t *cont = widget_container_create(parent, 380, 240);

    lv_obj_t *value = lv_label_create(cont);
    lv_obj_set_style_text_color(value, lv_color_white(), 0);
    lv_obj_set_style_text_font(value, &jbm_bold_144, 0);
    lv_label_set_text(value, "0");
    lv_obj_align(value, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t *unit = lv_label_create(cont);
    lv_obj_set_style_text_color(unit, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(unit, &jbm_bold_33, 0);
    lv_label_set_text(unit, units_speed_label(UNITS_KPH));
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 95);

    speed_data_t *sd = lv_malloc(sizeof(speed_data_t));
    sd->value_label = value;
    sd->unit_label  = unit;
    sd->last_kmh    = 0;
    sd->last_units  = UNITS_KPH;
    sd->has_value   = false;
    lv_obj_set_user_data(cont, sd);
    return cont;
}

void speed_display_set_value(lv_obj_t *cont, uint16_t kmh, display_units_t units)
{
    speed_data_t *sd = lv_obj_get_user_data(cont);
    if (!sd) return;
    if (sd->has_value && sd->last_kmh == kmh && sd->last_units == units) return;

    bool units_changed = sd->last_units != units;
    sd->last_kmh   = kmh;
    sd->last_units = units;
    sd->has_value  = true;

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)units_speed_display(kmh, units));
    lv_label_set_text(sd->value_label, buf);

    if (units_changed) {
        lv_label_set_text(sd->unit_label, units_speed_label(units));
    }
}
