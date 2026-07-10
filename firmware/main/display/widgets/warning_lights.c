#include "warning_lights.h"
#include "theme.h"
#include "widget_util.h"

LV_FONT_DECLARE(mdi_50);
LV_FONT_DECLARE(mdi_36);

// Material Design Icons UTF-8 encodings (codepoints in main/display/fonts).
#define ICON_OIL          "\xF3\xB0\x8F\x87"   // U+F03C7
#define ICON_ENGINE       "\xF3\xB0\x87\xBA"   // U+F01FA
#define ICON_ABS          "\xF3\xB0\xB1\x87"   // U+F0C47
#define ICON_BATTERY      "\xF3\xB0\x84\x8C"   // U+F010C  (car-battery)
#define ICON_IMMOBILISER  "\xF3\xB0\xAD\xAD"   // U+F0B6D  (car-key)
#define ICON_LOW_BEAM     "\xF3\xB0\xB1\x8A"   // U+F0C4A  (car-light-dimmed)
#define ICON_HIGH_BEAM    "\xF3\xB0\xB1\x8C"   // U+F0C4C  (car-light-high)

#define LAMP_GAP             16
#define LAMP_CELL            56      // generous cell so glyph variance is covered
#define BEAM_ROTATE_FRAMES   150     // ~5 s at 30 FPS
#define MAX_LAMPS            LAMP_COUNT

typedef struct {
    const char *icon;
    uint32_t    on_color;
} lamp_spec_t;

static const lamp_spec_t k_specs[LAMP_COUNT] = {
    [LAMP_OIL]         = {ICON_OIL,         VROD_RED_WARNING},
    [LAMP_ENGINE]      = {ICON_ENGINE,      VROD_AMBER_WARNING},
    [LAMP_ABS]         = {ICON_ABS,         VROD_AMBER_WARNING},
    [LAMP_BATTERY]     = {ICON_BATTERY,     VROD_RED_WARNING},
    [LAMP_IMMOBILISER] = {ICON_IMMOBILISER, VROD_RED_WARNING},
    [LAMP_LOW_BEAM]    = {ICON_LOW_BEAM,    VROD_GREEN_LOW_BEAM},
    [LAMP_HIGH_BEAM]   = {ICON_HIGH_BEAM,   VROD_BLUE_HIGH_BEAM},
    [LAMP_BEAM]        = {ICON_LOW_BEAM,    VROD_GREEN_LOW_BEAM},  // initial; rotates
};

typedef struct {
    int         count;
    lamp_id_t   ids[MAX_LAMPS];
    lv_obj_t   *lamps[MAX_LAMPS];
    const char *last_icon[MAX_LAMPS];
    uint32_t    last_color[MAX_LAMPS];
    uint32_t    beam_tick;
    bool        beam_show_high;   // virtual-beam slot currently showing high?
} warn_data_t;

static void lamp_apply_appearance(lv_obj_t *lbl, const char *icon, uint32_t color)
{
    lv_label_set_text(lbl, icon);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
}

lv_obj_t *warning_lights_create(lv_obj_t *parent,
                                const lamp_id_t *ids, int count,
                                warn_layout_t layout)
{
    if (count > MAX_LAMPS) count = MAX_LAMPS;
    if (layout == WARN_LAYOUT_CHEVRON && count != 3) {
        layout = WARN_LAYOUT_COLUMN;  // fall back if caller passes wrong count
    }

    int w = (layout == WARN_LAYOUT_CHEVRON) ? (2 * LAMP_CELL + LAMP_GAP) : LV_SIZE_CONTENT;
    int h = (layout == WARN_LAYOUT_CHEVRON) ? (2 * LAMP_CELL + LAMP_GAP) : LV_SIZE_CONTENT;
    lv_obj_t *cont = widget_container_create(parent, w, h);

    if (layout != WARN_LAYOUT_CHEVRON) {
        lv_obj_set_flex_flow(cont,
            layout == WARN_LAYOUT_ROW ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if (layout == WARN_LAYOUT_ROW) lv_obj_set_style_pad_column(cont, LAMP_GAP, 0);
        else                           lv_obj_set_style_pad_row(cont, LAMP_GAP, 0);
    }
    // For chevron, child positions are set per-child below.

    warn_data_t *wd = lv_malloc(sizeof(warn_data_t));
    wd->count = count;
    wd->beam_tick = 0;
    wd->beam_show_high = false;

    // The compact map row uses smaller lamps; the gauge's chevron/column
    // clusters keep the larger glyph.
    const lv_font_t *lamp_font = (layout == WARN_LAYOUT_ROW) ? &mdi_36 : &mdi_50;

    for (int i = 0; i < count; i++) {
        lamp_id_t id = ids[i];
        wd->ids[i] = id;
        lv_obj_t *lbl = lv_label_create(cont);
        lv_obj_set_style_text_font(lbl, lamp_font, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(VROD_LAMP_OFF), 0);
        lv_label_set_text(lbl, k_specs[id].icon);
        wd->last_icon[i]  = k_specs[id].icon;
        wd->last_color[i] = VROD_LAMP_OFF;

        if (layout == WARN_LAYOUT_CHEVRON) {
            // Cell-based positioning: 2 lamps top, 1 lamp bottom-centre.
            lv_obj_set_size(lbl, LAMP_CELL, LAMP_CELL);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            int x, y;
            if (i == 0)      { x = 0;                        y = 0; }
            else if (i == 1) { x = LAMP_CELL + LAMP_GAP;     y = 0; }
            else             { x = (LAMP_CELL + LAMP_GAP)/2; y = LAMP_CELL + LAMP_GAP; }
            lv_obj_set_pos(lbl, x, y);
        }

        wd->lamps[i] = lbl;
    }

    lv_obj_set_user_data(cont, wd);
    return cont;
}

static bool lamp_state(lamp_id_t id, const vehicle_data_t *d, bool beam_show_high)
{
    switch (id) {
        case LAMP_OIL:         return d->oil_pressure_warning;
        case LAMP_ENGINE:      return d->check_engine;
        case LAMP_ABS:         return d->abs_warning;
        case LAMP_BATTERY:     return d->battery_warning;
        case LAMP_IMMOBILISER: return d->immobiliser_warning;
        case LAMP_LOW_BEAM:    return d->low_beam;
        case LAMP_HIGH_BEAM:   return d->high_beam;
        default:  // LAMP_BEAM: follow whichever variant is shown
            return beam_show_high ? d->high_beam : d->low_beam;
        }
}

void warning_lights_update(lv_obj_t *cont, const vehicle_data_t *data)
{
    warn_data_t *wd = lv_obj_get_user_data(cont);
    if (!wd) return;

    // Rotate the virtual beam slot's icon every ~5 s. The colour and on/off
    // state below follow whichever variant is currently shown.
    wd->beam_tick++;
    if (wd->beam_tick >= BEAM_ROTATE_FRAMES) {
        wd->beam_tick = 0;
        wd->beam_show_high = !wd->beam_show_high;
    }

    for (int i = 0; i < wd->count; i++) {
        lamp_id_t id = wd->ids[i];
        const char *icon;
        uint32_t    on_color;
        if (id == LAMP_BEAM) {
            icon     = wd->beam_show_high ? ICON_HIGH_BEAM       : ICON_LOW_BEAM;
            on_color = wd->beam_show_high ? VROD_BLUE_HIGH_BEAM  : VROD_GREEN_LOW_BEAM;
        } else {
            icon     = k_specs[id].icon;
            on_color = k_specs[id].on_color;
        }
        uint32_t color = lamp_state(id, data, wd->beam_show_high) ? on_color : VROD_LAMP_OFF;

        // Most lamps stay off most of the time. Skip the LVGL calls when
        // the rendered appearance hasn't actually changed since last frame.
        if (wd->last_icon[i] == icon && wd->last_color[i] == color) continue;
        wd->last_icon[i]  = icon;
        wd->last_color[i] = color;
        lamp_apply_appearance(wd->lamps[i], icon, color);
    }
}
