#include "odometer_display.h"
#include "format.h"
#include "theme.h"
#include <stdio.h>

LV_FONT_DECLARE(jbm_bold_26);

typedef struct {
    lv_obj_t *label;
    uint32_t  last_km;
    bool      has_value;
} odo_data_t;

lv_obj_t *odometer_display_create(lv_obj_t *parent)
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
    lv_label_set_text(lbl, "ODO 0 km");
    lv_obj_center(lbl);

    odo_data_t *od = lv_malloc(sizeof(odo_data_t));
    od->label = lbl;
    od->last_km = 0;
    od->has_value = false;
    lv_obj_set_user_data(cont, od);
    return cont;
}

// Thousand-separated km, e.g. "ODO 12,847 km".
void odometer_display_set(lv_obj_t *cont, uint32_t meters)
{
    odo_data_t *od = lv_obj_get_user_data(cont);
    if (!od) return;

    uint32_t km = meters / 1000;
    if (od->has_value && od->last_km == km) return;
    od->last_km = km;
    od->has_value = true;

    char grouped[20];
    format_km_grouped(km, grouped, sizeof(grouped));

    char buf[32];
    snprintf(buf, sizeof(buf), "ODO %s km", grouped);
    lv_label_set_text(od->label, buf);
}
