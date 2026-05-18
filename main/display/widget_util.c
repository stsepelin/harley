#include "widget_util.h"

lv_obj_t *widget_container_create(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, w, h);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    // Widgets are visual readouts, not interactive — pass touches through to
    // the parent screen so screen-level handlers (e.g. long-press → settings)
    // actually fire.
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    return cont;
}
