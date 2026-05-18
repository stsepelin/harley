#include "trip_display.h"
#include "theme.h"
#include "widget_util.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(jbm_bold_26);

#define LABEL_MAX  8   // "TRIPN" with a little headroom

typedef struct {
    lv_obj_t       *value;
    char            label[LABEL_MAX];
    uint32_t        last_tenths;   // changes once per 0.1 unit
    display_units_t last_units;
    bool            has_value;
} trip_data_t;

lv_obj_t *trip_display_create(lv_obj_t *parent, const char *label)
{
    lv_obj_t *cont = widget_container_create(parent, LV_SIZE_CONTENT, 32);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_center(lbl);

    trip_data_t *td = lv_malloc(sizeof(trip_data_t));
    td->value = lbl;
    strncpy(td->label, label, LABEL_MAX - 1);
    td->label[LABEL_MAX - 1] = '\0';
    td->last_tenths = 0;
    td->last_units  = UNITS_KPH;
    td->has_value   = false;
    lv_obj_set_user_data(cont, td);

    trip_display_set(cont, 0, UNITS_KPH);
    return cont;
}

// One decimal of the active unit, e.g. "TRIP1 12.3 km" or "TRIP1 7.6 mi".
void trip_display_set(lv_obj_t *cont, uint32_t meters, display_units_t units)
{
    trip_data_t *td = lv_obj_get_user_data(cont);
    if (!td) return;

    uint32_t tenths = units_distance_tenths(meters, units);
    if (td->has_value && td->last_tenths == tenths && td->last_units == units) return;
    td->last_tenths = tenths;
    td->last_units  = units;
    td->has_value   = true;

    unsigned whole = (unsigned)(tenths / 10);
    unsigned frac  = (unsigned)(tenths % 10);

    char buf[32];
    snprintf(buf, sizeof(buf), "%s %u.%u %s",
             td->label, whole, frac, units_distance_label(units));
    lv_label_set_text(td->value, buf);
}
