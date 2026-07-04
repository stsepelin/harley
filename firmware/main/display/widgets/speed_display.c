#include "speed_display.h"
#include "lvgl.h"
#include "theme.h"
#include "widget_util.h"
#include <stdio.h>
#include <string.h>

// JetBrains Mono Bold is monospaced — every digit has the same advance — so a
// row of fixed-width per-digit slots is pixel-identical to one label, but a
// speed tick (50 -> 51) repaints only the slot whose glyph changed instead of
// the whole 144pt number. That number is the single biggest dirty rect on the
// ride screen (~244x214 of anti-aliased glyphs, the slow alpha-blend path),
// and it changes on nearly every frame under acceleration, so shrinking it to
// one digit is the largest render-budget win available for the 30 FPS target.
LV_FONT_DECLARE(jbm_bold_144);
LV_FONT_DECLARE(jbm_bold_33);

#define MAX_DIGITS 3

typedef struct {
    lv_obj_t       *digit[MAX_DIGITS];
    lv_obj_t       *unit_label;
    uint16_t        last_mph;
    display_units_t last_units;
    char            shown[MAX_DIGITS];  // glyph currently in each slot, 0 = blank
    bool            has_value;
} speed_data_t;

lv_obj_t *speed_display_create(lv_obj_t *parent)
{
    lv_obj_t *cont = widget_container_create(parent, 380, 240);

    // Digit slots live in a content-sized flex row so the number stays centred
    // as it grows; hidden leading slots are excluded from the layout.
    lv_obj_t *row = lv_obj_create(cont);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -15);

    speed_data_t *sd = lv_malloc(sizeof(speed_data_t));
    for (int i = 0; i < MAX_DIGITS; i++) {
        lv_obj_t *d = lv_label_create(row);
        lv_obj_set_style_text_color(d, lv_color_white(), 0);
        lv_obj_set_style_text_font(d, &jbm_bold_144, 0);
        lv_label_set_text(d, "");
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        sd->digit[i] = d;
        sd->shown[i] = 0;
    }
    // Prime the ones slot with "0" so the cluster reads 0 before the first
    // update (matches the old single-label default).
    lv_label_set_text(sd->digit[MAX_DIGITS - 1], "0");
    lv_obj_remove_flag(sd->digit[MAX_DIGITS - 1], LV_OBJ_FLAG_HIDDEN);
    sd->shown[MAX_DIGITS - 1] = '0';

    lv_obj_t *unit = lv_label_create(cont);
    lv_obj_set_style_text_color(unit, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(unit, &jbm_bold_33, 0);
    lv_label_set_text(unit, units_speed_label(UNITS_KPH));
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 95);

    sd->unit_label = unit;
    sd->last_mph   = 0;
    sd->last_units = UNITS_KPH;
    sd->has_value  = false;
    lv_obj_set_user_data(cont, sd);
    return cont;
}

void speed_display_set_value(lv_obj_t *cont, uint16_t mph, display_units_t units)
{
    speed_data_t *sd = lv_obj_get_user_data(cont);
    if (!sd) return;
    if (sd->has_value && sd->last_mph == mph && sd->last_units == units)
        return;

    bool units_changed = sd->last_units != units;
    sd->last_mph       = mph;
    sd->last_units = units;
    sd->has_value  = true;

    // Peg at 999: the layout has three digit slots, and truncating a longer
    // number to its leading digits would read as a confident wrong value
    // (1024 -> "102"). 4+ digits only happens on a faulty/garbage reading.
    unsigned shown_val = units_speed_display(mph, units);
    if (shown_val > 999)
        shown_val = 999;

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", shown_val);
    int n    = (int)strlen(buf);
    int lead = MAX_DIGITS - n;

    for (int i = 0; i < MAX_DIGITS; i++) {
        char want = (i < lead) ? 0 : buf[i - lead];
        if (want == sd->shown[i])
            continue;
        if (want == 0) {
            lv_obj_add_flag(sd->digit[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            char s[2] = {want, '\0'};
            lv_label_set_text(sd->digit[i], s);
            if (sd->shown[i] == 0)
                lv_obj_remove_flag(sd->digit[i], LV_OBJ_FLAG_HIDDEN);
        }
        sd->shown[i] = want;
    }

    if (units_changed) {
        lv_label_set_text(sd->unit_label, units_speed_label(units));
    }
}
