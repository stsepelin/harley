#include "turn_signals.h"
#include "lvgl.h"
#include "theme.h"

LV_FONT_DECLARE(mdi_96);

#define ICON_ARROW_LEFT_BOLD   "\xF3\xB0\x9C\xB1"   // U+F0731
#define ICON_ARROW_RIGHT_BOLD  "\xF3\xB0\x9C\xB4"   // U+F0734

#define CONT_W                 430        // narrower so arrows hug the gear
#define CONT_H                 110

typedef struct {
    lv_obj_t *left;
    lv_obj_t *right;
    bool      last_left;
    bool      last_right;
    bool      has_value;
} turn_data_t;

static lv_obj_t *make_arrow(lv_obj_t *parent, const char *icon, lv_align_t align)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &mdi_96, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(VROD_ARROW_OFF), 0);
    lv_label_set_text(lbl, icon);
    lv_obj_align(lbl, align, 0, 0);
    return lbl;
}

lv_obj_t *turn_signals_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, CONT_W, CONT_H);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    turn_data_t *td = lv_malloc(sizeof(turn_data_t));
    td->left  = make_arrow(cont, ICON_ARROW_LEFT_BOLD,  LV_ALIGN_LEFT_MID);
    td->right = make_arrow(cont, ICON_ARROW_RIGHT_BOLD, LV_ALIGN_RIGHT_MID);
    td->last_left = td->last_right = false;
    td->has_value = false;
    lv_obj_set_user_data(cont, td);
    return cont;
}

void turn_signals_set(lv_obj_t *cont, bool left, bool right)
{
    turn_data_t *td = lv_obj_get_user_data(cont);
    if (!td) return;
    if (td->has_value && td->last_left == left && td->last_right == right) return;
    if (!td->has_value || td->last_left != left) {
        lv_obj_set_style_text_color(td->left,
            lv_color_hex(left ? VROD_GREEN_SIGNAL : VROD_ARROW_OFF), 0);
    }
    if (!td->has_value || td->last_right != right) {
        lv_obj_set_style_text_color(td->right,
            lv_color_hex(right ? VROD_GREEN_SIGNAL : VROD_ARROW_OFF), 0);
    }
    td->last_left = left;
    td->last_right = right;
    td->has_value = true;
}
