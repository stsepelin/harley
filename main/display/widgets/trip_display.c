#include "trip_display.h"
#include "format.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(jbm_bold_26);

#define LABEL_MAX  8   // "TRIPN" with a little headroom

typedef struct {
    lv_obj_t *value;
    char      label[LABEL_MAX];
    uint32_t  last_tenths_km;   // (meters / 100); changes once per 100 m
    bool      has_value;
} trip_data_t;

lv_obj_t *trip_display_create(lv_obj_t *parent, const char *label)
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
    lv_obj_center(lbl);

    trip_data_t *td = lv_malloc(sizeof(trip_data_t));
    td->value = lbl;
    strncpy(td->label, label, LABEL_MAX - 1);
    td->label[LABEL_MAX - 1] = '\0';
    td->last_tenths_km = 0;
    td->has_value = false;
    lv_obj_set_user_data(cont, td);

    trip_display_set(cont, 0);
    return cont;
}

// One decimal of km (e.g. "TRIP1 12.3 km") — integer math, no float printf.
void trip_display_set(lv_obj_t *cont, uint32_t meters)
{
    trip_data_t *td = lv_obj_get_user_data(cont);
    if (!td) return;

    uint32_t tenths_km = meters / 100;
    if (td->has_value && td->last_tenths_km == tenths_km) return;
    td->last_tenths_km = tenths_km;
    td->has_value = true;

    char km_str[16];
    format_km_tenth(meters, km_str, sizeof(km_str));

    char buf[32];
    snprintf(buf, sizeof(buf), "%s %s km", td->label, km_str);
    lv_label_set_text(td->value, buf);
}
