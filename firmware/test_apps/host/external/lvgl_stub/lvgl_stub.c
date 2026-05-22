#include "lvgl.h"
#include "lvgl_stub.h"
#include <stdlib.h>

// --- Counters --------------------------------------------------------------
int g_lv_label_set_text_calls            = 0;
int g_lv_obj_set_style_text_color_calls  = 0;
int g_lv_obj_set_style_bg_color_calls    = 0;

void lv_stub_reset(void)
{
    g_lv_label_set_text_calls           = 0;
    g_lv_obj_set_style_text_color_calls = 0;
    g_lv_obj_set_style_bg_color_calls   = 0;
}

// --- Object creation -------------------------------------------------------
// Every `*_create` just allocates a fresh `lv_obj_t`. We don't track the
// parent/child tree — the cache tests never traverse it.

static lv_obj_t *new_obj(void)
{
    lv_obj_t *o = calloc(1, sizeof(lv_obj_t));
    return o;
}

lv_obj_t *lv_obj_create(lv_obj_t *parent)   { (void)parent; return new_obj(); }
lv_obj_t *lv_label_create(lv_obj_t *parent) { (void)parent; return new_obj(); }

// --- The functions we actually count --------------------------------------

void lv_label_set_text(lv_obj_t *obj, const char *text)
{
    (void)obj; (void)text;
    g_lv_label_set_text_calls++;
}

void lv_obj_set_style_text_color(lv_obj_t *obj, lv_color_t c, lv_part_t part)
{
    (void)obj; (void)c; (void)part;
    g_lv_obj_set_style_text_color_calls++;
}

void lv_obj_set_style_bg_color(lv_obj_t *obj, lv_color_t c, lv_part_t part)
{
    (void)obj; (void)c; (void)part;
    g_lv_obj_set_style_bg_color_calls++;
}

// --- user_data passthrough (the cache lives here) -------------------------

void  lv_obj_set_user_data(lv_obj_t *obj, void *data) { obj->user_data = data; }
void *lv_obj_get_user_data(lv_obj_t *obj)             { return obj->user_data; }

// --- Everything else: silent no-ops ---------------------------------------

void lv_obj_set_size(lv_obj_t *o, int w, int h)                       { (void)o; (void)w; (void)h; }
void lv_obj_set_pos (lv_obj_t *o, int x, int y)                       { (void)o; (void)x; (void)y; }
void lv_obj_align   (lv_obj_t *o, lv_align_t a, int x, int y)         { (void)o; (void)a; (void)x; (void)y; }
void lv_obj_center  (lv_obj_t *o)                                     { (void)o; }
void lv_obj_add_flag   (lv_obj_t *o, lv_obj_flag_t f)                 { (void)o; (void)f; }
void lv_obj_remove_flag(lv_obj_t *o, lv_obj_flag_t f)                 { (void)o; (void)f; }
void lv_obj_remove_style_all(lv_obj_t *o)                             { (void)o; }
void lv_obj_set_flex_flow(lv_obj_t *o, lv_flex_flow_t f)              { (void)o; (void)f; }
void lv_obj_set_flex_align(lv_obj_t *o, lv_flex_align_t m,
                           lv_flex_align_t c, lv_flex_align_t t)      { (void)o; (void)m; (void)c; (void)t; }

void lv_obj_set_style_text_font (lv_obj_t *o, const lv_font_t *f, lv_part_t p) { (void)o; (void)f; (void)p; }
void lv_obj_set_style_text_align(lv_obj_t *o, lv_text_align_t a, lv_part_t p)  { (void)o; (void)a; (void)p; }
void lv_obj_set_style_bg_opa    (lv_obj_t *o, lv_opa_t v, lv_part_t p)         { (void)o; (void)v; (void)p; }
void lv_obj_set_style_border_width(lv_obj_t *o, int w, lv_part_t p)            { (void)o; (void)w; (void)p; }
void lv_obj_set_style_border_color(lv_obj_t *o, lv_color_t c, lv_part_t p)     { (void)o; (void)c; (void)p; }
void lv_obj_set_style_pad_all   (lv_obj_t *o, int v, lv_part_t p)              { (void)o; (void)v; (void)p; }
void lv_obj_set_style_pad_column(lv_obj_t *o, int v, lv_part_t p)              { (void)o; (void)v; (void)p; }
void lv_obj_set_style_pad_row   (lv_obj_t *o, int v, lv_part_t p)              { (void)o; (void)v; (void)p; }
void lv_obj_set_style_radius    (lv_obj_t *o, int v, lv_part_t p)              { (void)o; (void)v; (void)p; }

lv_color_t lv_color_hex(uint32_t hex)   { return (lv_color_t){.v = hex}; }
lv_color_t lv_color_white(void)         { return (lv_color_t){.v = 0xFFFFFFu}; }
lv_color_t lv_color_black(void)         { return (lv_color_t){.v = 0x000000u}; }

void *lv_malloc(size_t size)            { return malloc(size); }
void  lv_free(void *ptr)                { free(ptr); }

// --- Fonts referenced by widgets ------------------------------------------
// LV_FONT_DECLARE expands to `extern const lv_font_t name;` so we have to
// provide the actual symbols somewhere. Each is an opaque zero-init.
const lv_font_t jbm_bold_26          = {0};
const lv_font_t jbm_bold_33          = {0};
const lv_font_t jbm_bold_45          = {0};
const lv_font_t jbm_bold_72          = {0};
const lv_font_t jbm_bold_144         = {0};
const lv_font_t mdi_36               = {0};
const lv_font_t mdi_50               = {0};
const lv_font_t mdi_60               = {0};
const lv_font_t mdi_96               = {0};
const lv_font_t lv_font_montserrat_22 = {0};
const lv_font_t lv_font_montserrat_48 = {0};

// --- Timer stubs -----------------------------------------------------------
// The cache-regression tests only exercise the create / delete / get-user-data
// surface (gear_indicator's warning blink). We don't actually fire the
// timer — the per-tick behaviour is verified on device.

lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period_ms, void *user_data)
{
    (void)cb; (void)period_ms;
    lv_timer_t *t = calloc(1, sizeof(lv_timer_t));
    if (t) t->user_data = user_data;
    return t;
}

void  lv_timer_delete(lv_timer_t *t)                          { free(t); }
void  lv_timer_set_user_data(lv_timer_t *t, void *user_data)  { if (t) t->user_data = user_data; }
void *lv_timer_get_user_data(lv_timer_t *t)                   { return t ? t->user_data : NULL; }
