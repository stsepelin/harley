#include "boot_screen.h"
#include "lvgl.h"

void boot_screen_show(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "V-ROD");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF6600), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "VRSCF MUSCLE");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_22, 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 30);
}
