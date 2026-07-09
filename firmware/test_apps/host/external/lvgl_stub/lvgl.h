#pragma once
// Minimal LVGL header stand-in for host-side widget tests. Covers exactly
// the surface the display widgets use — labels, images, canvas (with a
// deterministic block-glyph rasterizer in lvgl_stub.c), timers, events —
// so every widget, tach_arc included, runs its real code on host.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- Types -----------------------------------------------------------------

// Events carry a target + the user_data registered with the callback —
// enough for the banner buttons' click plumbing. Tests fire them with
// lv_event_stub_click_all() (lvgl_stub.h).
struct lv_obj_t;
typedef struct lv_event_t {
    struct lv_obj_t *target;
    void            *user_data;
} lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t *e);
typedef int lv_event_code_t;

typedef struct lv_obj_t {
    void            *user_data;
    struct lv_obj_t *parent;
    lv_event_cb_t    event_cb;  // single slot: widgets register one cb per obj
    void            *event_user_data;
    uint8_t         *canvas_buf;  // lv_canvas surface (ARGB8888)
    int32_t          canvas_w;
    int32_t          canvas_h;
} lv_obj_t;

struct lv_timer_t;
typedef void (*lv_timer_cb_t)(struct lv_timer_t *t);

typedef struct lv_timer_t {
    void         *user_data;
    lv_timer_cb_t cb;
} lv_timer_t;

typedef struct { uint32_t v; } lv_color_t;
typedef struct {
    int32_t x, y;
} lv_point_precise_t;
typedef struct lv_font_t { int _opaque; } lv_font_t;

// Raw-image descriptor surface used by the baked-sprite widgets (gear
// outline, fuel strip). Mirrors the real lv_image_dsc_t fields those
// widgets and sprite_raster.h touch.
typedef struct {
    struct {
        uint32_t magic;
        uint32_t cf;
        uint32_t flags;
        uint32_t w;
        uint32_t h;
        uint32_t stride;
    } header;
    size_t         data_size;
    const uint8_t *data;
} lv_image_dsc_t;
typedef int   lv_align_t;
typedef int   lv_flex_flow_t;
typedef int   lv_flex_align_t;
typedef int   lv_opa_t;
typedef int   lv_obj_flag_t;
typedef int   lv_text_align_t;
typedef int   lv_part_t;
typedef int   lv_label_long_mode_t;
typedef struct {
    int32_t x1, y1, x2, y2;
} lv_area_t;

// Draw layer: carries the canvas surface between init/draw/finish.
typedef struct {
    uint8_t *buf;
    int32_t  w;
    int32_t  h;
} lv_layer_t;

typedef struct {
    const char      *text;
    bool             text_local;
    const lv_font_t *font;
    lv_color_t       color;
    lv_text_align_t  align;
} lv_draw_label_dsc_t;

// --- Constants -------------------------------------------------------------

#define LV_FONT_DECLARE(name)  extern const lv_font_t name;

#define LV_OPA_TRANSP          0
#define LV_OPA_COVER           255

#define LV_IMAGE_HEADER_MAGIC    0x19
#define LV_COLOR_FORMAT_ARGB8888 0x10
#define LV_COLOR_FORMAT_RGB565   0x12

#define LV_PCT(x)              (x)
#define LV_SIZE_CONTENT        (-1)

#define LV_ALIGN_CENTER        0
#define LV_ALIGN_LEFT_MID      1
#define LV_ALIGN_RIGHT_MID     2
#define LV_ALIGN_TOP_MID       3
#define LV_ALIGN_BOTTOM_MID    4
#define LV_ALIGN_TOP_LEFT      5
#define LV_ALIGN_TOP_RIGHT     6
#define LV_ALIGN_BOTTOM_LEFT   7
#define LV_ALIGN_BOTTOM_RIGHT  8

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

#define LV_EVENT_CLICKED 7

#define LV_LABEL_LONG_WRAP 0
#define LV_LABEL_LONG_DOT  1

// --- API -------------------------------------------------------------------

lv_obj_t *lv_obj_create(lv_obj_t *parent);
lv_obj_t *lv_label_create(lv_obj_t *parent);
lv_obj_t *lv_image_create(lv_obj_t *parent);
lv_obj_t *lv_button_create(lv_obj_t *parent);
lv_obj_t *lv_canvas_create(lv_obj_t *parent);
void      lv_obj_delete(lv_obj_t *obj);

void lv_canvas_set_buffer(lv_obj_t *canvas, void *buf, int32_t w, int32_t h, int cf);
void lv_canvas_init_layer(lv_obj_t *canvas, lv_layer_t *layer);
void lv_canvas_finish_layer(lv_obj_t *canvas, lv_layer_t *layer);
void lv_draw_label_dsc_init(lv_draw_label_dsc_t *dsc);
void lv_draw_label(lv_layer_t *layer, const lv_draw_label_dsc_t *dsc, const lv_area_t *area);

void lv_obj_set_style_image_recolor(lv_obj_t *obj, lv_color_t c, lv_part_t part);
void lv_obj_set_style_image_recolor_opa(lv_obj_t *obj, lv_opa_t opa, lv_part_t part);

lv_color_t lv_color_mix(lv_color_t c1, lv_color_t c2, uint8_t mix);

void lv_image_set_src(lv_obj_t *obj, const void *src);
void lv_obj_invalidate(lv_obj_t *obj);

void lv_obj_add_event_cb(lv_obj_t *obj, lv_event_cb_t cb, lv_event_code_t filter, void *user_data);
lv_obj_t *lv_event_get_target(lv_event_t *e);
void     *lv_event_get_user_data(lv_event_t *e);

void  lv_label_set_text(lv_obj_t *obj, const char *text);
void  lv_label_set_long_mode(lv_obj_t *obj, lv_label_long_mode_t mode);

void  lv_obj_set_size(lv_obj_t *obj, int w, int h);
void      lv_obj_set_width(lv_obj_t *obj, int w);
void      lv_obj_set_height(lv_obj_t *obj, int h);
int32_t   lv_obj_get_height(lv_obj_t *obj);
void      lv_obj_update_layout(lv_obj_t *obj);
lv_obj_t *lv_obj_get_parent(lv_obj_t *obj);
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

// Provided by external/lvgl_tick_stub (linked via vrod_pure); tests drive
// the clock with lv_tick_stub_set().
uint32_t lv_tick_get(void);
uint32_t lv_tick_elaps(uint32_t prev_tick);

lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period_ms, void *user_data);
void        lv_timer_delete(lv_timer_t *t);
void        lv_timer_set_user_data(lv_timer_t *t, void *user_data);
void       *lv_timer_get_user_data(lv_timer_t *t);
