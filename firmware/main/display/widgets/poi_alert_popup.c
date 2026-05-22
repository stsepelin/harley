#include "poi_alert_popup.h"
#include "lvgl.h"
#include "theme.h"
#include "widget_util.h"
#include <stdio.h>

LV_FONT_DECLARE(jbm_bold_33);

// Width matches the notification banner so the two visually align on
// the same column. Height holds one JBM-Bold-33 line plus the top/bottom
// pad — narrow enough to keep clear of the gear digit at y≈250 from top.
#define POPUP_W    400
#define POPUP_H    70
#define POPUP_PAD  10

// Round to nearest 10 m below 1 km — riders don't need single-metre
// precision and a stable readout is easier to glance at. Above 1 km we
// show "1.0km" style. Distances are uint32_t in metres.
static void format_distance(char *out, size_t out_sz, uint32_t m)
{
    if (m >= 1000) {
        snprintf(out, out_sz, "%u.%ukm", (unsigned)(m / 1000),
                                          (unsigned)((m % 1000) / 100));
    } else {
        uint32_t rounded = (m + 5) / 10 * 10;
        snprintf(out, out_sz, "%um", (unsigned)rounded);
    }
}

static const char *kind_tag(uint8_t kind)
{
    switch (kind) {
    case POI_KIND_SPEED:     return "CAM";
    case POI_KIND_RED_LIGHT: return "RED";
    case POI_KIND_SECTION:   return "SECT";
    default:                 return "POI";
    }
}

typedef struct {
    lv_obj_t *label;
    bool      last_active;
    uint8_t   last_kind;
    uint8_t   last_limit;
    uint32_t  last_distance_m;
} popup_data_t;

lv_obj_t *poi_alert_popup_create(lv_obj_t *parent)
{
    lv_obj_t *cont = widget_container_create(parent, POPUP_W, POPUP_H);
    lv_obj_set_style_bg_color    (cont, lv_color_hex(VROD_RED_BRIGHT), 0);
    lv_obj_set_style_bg_opa      (cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(VROD_TEXT), 0);
    lv_obj_set_style_border_width(cont, 2, 0);
    lv_obj_set_style_radius      (cont, 14, 0);
    lv_obj_set_style_pad_all     (cont, POPUP_PAD, 0);

    lv_obj_t *label = lv_label_create(cont);
    lv_obj_set_style_text_font (label, &jbm_bold_33, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    popup_data_t *pd = lv_malloc(sizeof(*pd));
    *pd = (popup_data_t){ .label = label, .last_active = false };
    lv_obj_set_user_data(cont, pd);

    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    return cont;
}

void poi_alert_popup_update(lv_obj_t *popup, const poi_alert_t *alert)
{
    if (!popup || !alert) return;
    popup_data_t *pd = lv_obj_get_user_data(popup);
    if (!pd) return;

    if (!alert->active || !alert->cam) {
        if (pd->last_active) {
            lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
            pd->last_active = false;
        }
        return;
    }

    // Round to the same 10 m bucket the label shows — otherwise every
    // metre of movement would invalidate the label even though the text
    // is unchanged.
    uint32_t bucket = (alert->distance_m < 1000)
        ? (alert->distance_m / 10 * 10)
        : (alert->distance_m / 100 * 100);

    if (pd->last_active
        && pd->last_kind     == alert->cam->kind
        && pd->last_limit    == alert->cam->limit_kmh
        && pd->last_distance_m == bucket) {
        return;
    }

    char dist_buf[16];
    format_distance(dist_buf, sizeof(dist_buf), alert->distance_m);

    char text[48];
    if (alert->cam->kind == POI_KIND_SPEED && alert->cam->limit_kmh > 0) {
        snprintf(text, sizeof(text), "%s %s  %u",
                 kind_tag(alert->cam->kind), dist_buf, alert->cam->limit_kmh);
    } else {
        snprintf(text, sizeof(text), "%s %s",
                 kind_tag(alert->cam->kind), dist_buf);
    }
    lv_label_set_text(pd->label, text);

    if (!pd->last_active) {
        lv_obj_remove_flag(popup, LV_OBJ_FLAG_HIDDEN);
    }
    pd->last_active     = true;
    pd->last_kind       = alert->cam->kind;
    pd->last_limit      = alert->cam->limit_kmh;
    pd->last_distance_m = bucket;
}
