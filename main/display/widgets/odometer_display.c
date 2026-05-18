#include "odometer_display.h"
#include "format.h"
#include "theme.h"
#include "widget_util.h"
#include <stdio.h>

LV_FONT_DECLARE(jbm_bold_26);

typedef struct {
    lv_obj_t       *label;
    uint32_t        last_val;
    display_units_t last_units;
    bool            has_value;
} odo_data_t;

lv_obj_t *odometer_display_create(lv_obj_t *parent)
{
    lv_obj_t *cont = widget_container_create(parent, LV_SIZE_CONTENT, 32);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(lbl, "ODO 0 km");
    lv_obj_center(lbl);

    odo_data_t *od = lv_malloc(sizeof(odo_data_t));
    od->label      = lbl;
    od->last_val   = 0;
    od->last_units = UNITS_KPH;
    od->has_value  = false;
    lv_obj_set_user_data(cont, od);
    return cont;
}

// Thousand-separated distance in the active units, e.g. "ODO 12,847 km".
void odometer_display_set(lv_obj_t *cont, uint32_t meters, display_units_t units)
{
    odo_data_t *od = lv_obj_get_user_data(cont);
    if (!od) return;

    uint32_t val = units_distance_whole(meters, units);
    if (od->has_value && od->last_val == val && od->last_units == units) return;
    od->last_val   = val;
    od->last_units = units;
    od->has_value  = true;

    char grouped[20];
    format_km_grouped(val, grouped, sizeof(grouped));

    char buf[32];
    snprintf(buf, sizeof(buf), "ODO %s %s", grouped, units_distance_label(units));
    lv_label_set_text(od->label, buf);
}
