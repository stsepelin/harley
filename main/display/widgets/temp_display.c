#include "temp_display.h"
#include "lvgl.h"
#include "theme.h"
#include <stdio.h>

LV_FONT_DECLARE(jbm_bold_33);
LV_FONT_DECLARE(mdi_36);

#define ICON_THERMOMETER  "\xF3\xB0\x94\x8F"   // U+F050F

typedef struct {
    lv_obj_t *icon;
    lv_obj_t *value;
    int8_t    last_c;
    bool      has_value;
} temp_data_t;

lv_obj_t *temp_display_create(lv_obj_t *parent)
{
    // Flex row: icon + value sit adjacent with a small gap, the pair centered
    // inside the container. Width auto-grows around the content.
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, 6, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(cont);
    lv_obj_set_style_text_font(icon, &mdi_36, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(VROD_ICON), 0);
    lv_label_set_text(icon, ICON_THERMOMETER);

    lv_obj_t *value = lv_label_create(cont);
    lv_obj_set_style_text_color(value, lv_color_white(), 0);
    lv_obj_set_style_text_font(value, &jbm_bold_33, 0);
    lv_label_set_text(value, "0\xC2\xB0""C");

    temp_data_t *td = lv_malloc(sizeof(temp_data_t));
    td->icon  = icon;
    td->value = value;
    td->last_c = 0;
    td->has_value = false;
    lv_obj_set_user_data(cont, td);
    return cont;
}

void temp_display_set_value(lv_obj_t *cont, int8_t celsius)
{
    temp_data_t *td = lv_obj_get_user_data(cont);
    if (!td) return;
    if (td->has_value && td->last_c == celsius) return;
    td->last_c = celsius;
    td->has_value = true;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", (int)celsius);
    lv_label_set_text(td->value, buf);

    bool hot = (celsius >= 110);
    lv_color_t value_c = hot ? lv_color_hex(VROD_RED_BRIGHT) : lv_color_white();
    lv_color_t icon_c  = hot ? lv_color_hex(VROD_RED_BRIGHT) : lv_color_hex(VROD_ICON);
    lv_obj_set_style_text_color(td->value, value_c, 0);
    lv_obj_set_style_text_color(td->icon,  icon_c,  0);
}
