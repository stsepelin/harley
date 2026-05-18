#include "fuel_bar.h"
#include "lvgl.h"
#include "theme.h"

LV_FONT_DECLARE(mdi_60);

#define ICON_FUEL        "\xF3\xB0\x9F\x8A"   // U+F07CA gas pump
#define FUEL_SEGMENTS    6
#define ICON_SLOT_W      70
#define BAR_W            225
#define BAR_H            38
#define SEG_GAP          5
#define CONT_H           75

typedef struct {
    lv_obj_t *icon;
    lv_obj_t *segments[FUEL_SEGMENTS];
    uint8_t   last_level;
    bool      has_value;
} fuel_data_t;

lv_obj_t *fuel_bar_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, ICON_SLOT_W + BAR_W + 8, CONT_H);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Gas-pump icon on the left.
    lv_obj_t *icon = lv_label_create(cont);
    lv_obj_set_style_text_font(icon, &mdi_60, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(VROD_ICON), 0);
    lv_label_set_text(icon, ICON_FUEL);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

    // Segmented bar to the right of the icon, centered vertically.
    lv_obj_t *bar = lv_obj_create(cont);
    lv_obj_set_size(bar, BAR_W, BAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bar, LV_ALIGN_RIGHT_MID, 0, 0);

    int seg_w = (BAR_W - (FUEL_SEGMENTS - 1) * SEG_GAP) / FUEL_SEGMENTS;
    fuel_data_t *fd = lv_malloc(sizeof(fuel_data_t));
    fd->icon = icon;
    fd->last_level = 0;
    fd->has_value = false;
    for (int i = 0; i < FUEL_SEGMENTS; i++) {
        lv_obj_t *seg = lv_obj_create(bar);
        lv_obj_set_size(seg, seg_w, BAR_H);
        lv_obj_set_pos(seg, i * (seg_w + SEG_GAP), 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(VROD_SEGMENT_OFF), 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_pad_all(seg, 0, 0);
        lv_obj_set_style_radius(seg, 3, 0);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        fd->segments[i] = seg;
    }

    lv_obj_set_user_data(cont, fd);
    return cont;
}

void fuel_bar_set_level(lv_obj_t *cont, uint8_t level)
{
    fuel_data_t *fd = lv_obj_get_user_data(cont);
    if (!fd) return;
    if (level > FUEL_SEGMENTS) level = FUEL_SEGMENTS;
    if (fd->has_value && fd->last_level == level) return;
    fd->last_level = level;
    fd->has_value = true;
    for (int i = 0; i < FUEL_SEGMENTS; i++) {
        uint32_t color = VROD_SEGMENT_OFF;
        if (i < level) {
            color = (level <= 2) ? VROD_RED_BRIGHT : VROD_ORANGE;
        }
        lv_obj_set_style_bg_color(fd->segments[i], lv_color_hex(color), 0);
    }
    // Pump icon goes red when fuel is critically low (≤ 1 segment left).
    uint32_t icon_color = (level <= 1) ? VROD_RED_BRIGHT : VROD_ICON;
    lv_obj_set_style_text_color(fd->icon, lv_color_hex(icon_color), 0);
}
