#pragma once
// Minimal LVGL header stand-in for host-side cache-regression tests.
// Covers exactly the surface used by the label-based widgets (speed,
// gear, fuel, temp, turn signals, clock, odo, trip, warning lights).
// Bigger widgets (tach_arc with arc/scale/image) are out of scope and
// are not linked into the host build.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- Types -----------------------------------------------------------------

typedef struct lv_obj_t {
    void *user_data;
} lv_obj_t;

typedef struct lv_timer_t {
    void *user_data;
} lv_timer_t;

typedef void (*lv_timer_cb_t)(lv_timer_t *t);

typedef struct { uint32_t v; } lv_color_t;
typedef struct lv_font_t { int _opaque; } lv_font_t;
typedef int   lv_align_t;
typedef int   lv_flex_flow_t;
typedef int   lv_flex_align_t;
typedef int   lv_opa_t;
typedef int   lv_obj_flag_t;
typedef int   lv_text_align_t;
typedef int   lv_part_t;

// --- Constants -------------------------------------------------------------

#define LV_FONT_DECLARE(name)  extern const lv_font_t name;

#define LV_OPA_TRANSP          0
#define LV_OPA_COVER           255

#define LV_PCT(x)              (x)
#define LV_SIZE_CONTENT        (-1)

#define LV_ALIGN_CENTER        0
#define LV_ALIGN_LEFT_MID      1
#define LV_ALIGN_RIGHT_MID     2
#define LV_ALIGN_TOP_MID       3
#define LV_ALIGN_BOTTOM_MID    4

#define LV_FLEX_FLOW_ROW       1
#define LV_FLEX_FLOW_COLUMN    2

#define LV_FLEX_ALIGN_CENTER   0

#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_HIDDEN     2
#define LV_OBJ_FLAG_CLICKABLE  4

#define LV_TEXT_ALIGN_CENTER   0

#define LV_PART_MAIN           0
#define LV_PART_INDICATOR      1
#define LV_PART_ITEMS          2
#define LV_PART_KNOB           3

// --- API -------------------------------------------------------------------

lv_obj_t *lv_obj_create(lv_obj_t *parent);
lv_obj_t *lv_label_create(lv_obj_t *parent);

void  lv_label_set_text(lv_obj_t *obj, const char *text);

void  lv_obj_set_size(lv_obj_t *obj, int w, int h);
void  lv_obj_set_pos(lv_obj_t *obj, int x, int y);
void  lv_obj_align(lv_obj_t *obj, lv_align_t align, int x_ofs, int y_ofs);
void  lv_obj_center(lv_obj_t *obj);

void  lv_obj_set_user_data(lv_obj_t *obj, void *data);
void *lv_obj_get_user_data(lv_obj_t *obj);

void  lv_obj_add_flag(lv_obj_t *obj, lv_obj_flag_t flag);
void  lv_obj_remove_flag(lv_obj_t *obj, lv_obj_flag_t flag);
void  lv_obj_remove_style_all(lv_obj_t *obj);

void  lv_obj_set_flex_flow(lv_obj_t *obj, lv_flex_flow_t flow);
void  lv_obj_set_flex_align(lv_obj_t *obj, lv_flex_align_t main,
                            lv_flex_align_t cross, lv_flex_align_t track);

void  lv_obj_set_style_text_color(lv_obj_t *obj, lv_color_t c,    lv_part_t part);
void  lv_obj_set_style_text_font (lv_obj_t *obj, const lv_font_t *f, lv_part_t part);
void  lv_obj_set_style_text_align(lv_obj_t *obj, lv_text_align_t a, lv_part_t part);
void  lv_obj_set_style_bg_color    (lv_obj_t *obj, lv_color_t c,    lv_part_t part);
void  lv_obj_set_style_border_color(lv_obj_t *obj, lv_color_t c,    lv_part_t part);
void  lv_obj_set_style_bg_opa    (lv_obj_t *obj, lv_opa_t opa,    lv_part_t part);
void  lv_obj_set_style_border_width(lv_obj_t *obj, int w,         lv_part_t part);
void  lv_obj_set_style_pad_all   (lv_obj_t *obj, int v,           lv_part_t part);
void  lv_obj_set_style_pad_column(lv_obj_t *obj, int v,           lv_part_t part);
void  lv_obj_set_style_pad_row   (lv_obj_t *obj, int v,           lv_part_t part);
void  lv_obj_set_style_radius    (lv_obj_t *obj, int v,           lv_part_t part);

lv_color_t lv_color_hex(uint32_t hex);
lv_color_t lv_color_white(void);
lv_color_t lv_color_black(void);

void *lv_malloc(size_t size);
void  lv_free(void *ptr);

lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period_ms, void *user_data);
void        lv_timer_delete(lv_timer_t *t);
void        lv_timer_set_user_data(lv_timer_t *t, void *user_data);
void       *lv_timer_get_user_data(lv_timer_t *t);
