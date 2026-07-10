#include "fuel_arc.h"
#include "fuel_scale.h"
#include "lvgl.h"
#include "theme.h"
#include "sprite_raster.h"
#include "widget_util.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>

LV_FONT_DECLARE(mdi_36);
LV_FONT_DECLARE(jbm_bold_26);

#define ICON_FUEL       "\xF3\xB0\x9F\x8A"  // U+F07CA gas pump (mdi_36 has it; mdi_50 does NOT)
#define FUEL_LEVELS     FUEL_SCALE_LEVELS
#define FUEL_RED_LEVELS 2       // level <= this: lit ticks turn red (low)
#define FUEL_ICON_RED_LEVELS 1  // level <= this: pump icon also red (critically low)

// BMW-style fuel scale, rendered as ONE baked ARGB image (everything drawn
// into a single buffer) rather than individual lv_obj rectangles. The
// per-object version recoloured + invalidated up to 21 small scattered
// objects on a level change, which crashed the device's triple-partial DSI
// render path (the desktop simulator + ASan never reproduced it; the
// cursor/label *images* render fine, so a single image is the safe path).
// The strip is redrawn only when the level changes, so per-frame cost is one
// blit -- and nothing overlaps it, so usually zero.
//
// Visual: the FILLED portion is a solid continuous arc band growing from E
// (like the BMW range bar); the EMPTY remainder shows the discrete sector
// ticks. Red band when low.

#define TICK_COUNT   FUEL_SCALE_TICKS
#define MAJOR_EVERY  FUEL_SCALE_MAJOR_EVERY

// All the arc dimensions live in one geometry struct so the same drawing code
// can bake a full-gauge arc and a compact (~1.5x smaller) map-strip arc. The
// FULL preset reproduces the original hard-coded gauge exactly; COMPACT scales
// the radius / ticks / labels down and shrinks the image box to match.
typedef struct {
    int              cont_w, cont_h;
    int              arc_cy, arc_r;  // arc centre (cx = cont_w/2) + radius, container-local
    int              tick_w, tick_w_major, tick_h, tick_h_major;
    float            arc_span_deg;                // sweep, centred on straight-down (90 deg)
    int              arc_end_dx;                  // ~ arc_r*sin(span/2); aligns E/F to the arc ends
    int              img_x, img_y, img_w, img_h;  // tick-strip image box (bounds all ticks)
    float            band_corner_r, band_tick_gap_deg;
    const lv_font_t *icon_font;
    int              icon_ofs_x, icon_ofs_y;
    const lv_font_t *label_font;
    int              label_ofs_y;
} fuel_geom_t;

// Full gauge. The arc is CONCENTRIC with the round 800x800 display so it
// parallels the bottom bezel: the display centre (screen 400,400) maps to
// container (240, -264) with the widget bottom-aligned at y=-8. The image box
// is sized to cover the full sweep INCLUDING the radial ticks' rounded caps +
// rim flare at the E/F ends (an undersized box visibly clipped the F major).
static const fuel_geom_t FUEL_GEOM_FULL = {
    .cont_w            = 480,
    .cont_h            = 128,  // extra width past the arc ends leaves room for the icon
    .arc_cy            = -264,
    .arc_r             = 360,
    .tick_w            = 5,
    .tick_w_major      = 6,
    .tick_h            = 14,
    .tick_h_major      = 22,
    .arc_span_deg      = 58.0f,
    .arc_end_dx        = 175,
    .img_x             = 52,
    .img_y             = 34,
    .img_w             = 376,
    .img_h             = 84,
    .band_corner_r     = 6.0f,
    .band_tick_gap_deg = 1.5f,
    .icon_font         = &mdi_36,
    .icon_ofs_x        = 10,
    .icon_ofs_y        = -26,
    .label_font        = &jbm_bold_26,
    .label_ofs_y       = 6,
};

// Compact strip for the map cluster. It keeps the SAME radius as the full gauge
// (360) so its curve is concentric with the round 800x800 bezel and parallels
// the screen edge exactly -- a smaller radius would dip into a tight U that
// fights the display curve. "Compact" instead comes from a narrower sweep
// (44 vs 58 deg) + thinner ticks + smaller labels, so the arc is a short,
// shallow, bezel-hugging band. Bottom-aligned at -12: container top at screen
// 698, arc centre at (400, 400) = the display centre.
static const fuel_geom_t FUEL_GEOM_COMPACT = {
    .cont_w            = 384,
    .cont_h            = 100,  // taller than the arc needs, to lift E/F clear of the ticks
    .arc_cy            = -288,
    .arc_r             = 360,
    .tick_w            = 4,
    .tick_w_major      = 5,
    .tick_h            = 10,
    .tick_h_major      = 15,
    .arc_span_deg      = 44.0f,
    .arc_end_dx        = 136,
    .img_x             = 48,
    .img_y             = 32,
    .img_w             = 292,
    .img_h             = 54,
    .band_corner_r     = 4.0f,
    .band_tick_gap_deg = 1.5f,
    .icon_font         = &mdi_36,
    .icon_ofs_x        = 4,
    .icon_ofs_y        = -10,
    .label_font        = &jbm_bold_26,
    .label_ofs_y       = 2,
};

typedef struct {
    const fuel_geom_t *g;
    lv_obj_t          *tick_img;
    uint8_t           *buf;  // ARGB8888 strip, redrawn on level change
    lv_image_dsc_t     dsc;
    lv_obj_t          *icon;
    uint8_t            last_level;
    bool               has_value;
} fuel_data_t;

static inline float geom_out_r(const fuel_geom_t *g)
{
    return (float)(g->arc_r + g->tick_h_major / 2);  // rim-aligned tick outer edge
}

// Rim-aligned true radial tick at sweep angle ang_deg: the outer end sits on
// the rim circle and the bar points at the arc centre, matching the band's
// radial gap cuts. sprite_raster.h supplies the AA capsule, the house taper
// and the over-composite (so the white section majors draw on top of the
// fill band), keeping the fuel marks identical to the tach's.
static void draw_radial_tick(const fuel_geom_t *g, uint8_t *buf, float ang_deg, float halfw,
                             float len, uint32_t hex)
{
    float ar = ang_deg * (float)M_PI / 180.0f;
    float dx = cosf(ar), dy = sinf(ar);
    float outr = geom_out_r(g);
    float x1   = (float)(g->cont_w / 2) + outr * dx - (float)g->img_x;
    float y1   = (float)g->arc_cy + outr * dy - (float)g->img_y;
    sprite_stamp_capsule(buf, g->img_w, g->img_h, x1 - len * dx, y1 - len * dy, x1, y1, halfw, hex);
}

// White section graduations (E, 1/3, 2/3, F) — drawn last, over the fill
// band, so the 3-section structure stays visible at every level.
static void draw_majors(fuel_data_t *fd)
{
    const fuel_geom_t *g  = fd->g;
    const float        a0 = 90.0f + g->arc_span_deg / 2.0f;
    for (int i = 0; i < TICK_COUNT; i += MAJOR_EVERY) {
        float ang = a0 - (float)i / (float)(TICK_COUNT - 1) * g->arc_span_deg;
        draw_radial_tick(g, fd->buf, ang, g->tick_w_major / 2.0f, g->tick_h_major, VROD_TEXT);
    }
}

static void draw_ticks(fuel_data_t *fd, uint8_t level)
{
    const fuel_geom_t *g = fd->g;
    memset(fd->buf, 0, (size_t)g->img_w * g->img_h * 4);
    int last_cov = fuel_scale_last_covered(level);

    // Empty remainder: gray minor ticks (rounded radial capsules). Minors
    // under the band are skipped; the white section majors draw LAST, on top
    // of the band, so the 3-section structure is always visible.
    const float a0 = 90.0f + g->arc_span_deg / 2.0f;  // E end (left)
    for (int i = 0; i < TICK_COUNT; i++) {
        if ((i % MAJOR_EVERY) == 0)
            continue;
        if (level > 0 && i <= last_cov)
            continue;
        float ang = a0 - (float)i / (float)(TICK_COUNT - 1) * g->arc_span_deg;
        draw_radial_tick(g, fd->buf, ang, g->tick_w / 2.0f, g->tick_h, VROD_RAIL);
    }
    if (level == 0) {
        draw_majors(fd);
        return;
    }

    // Filled portion: solid rounded arc segments from the E end to the fill
    // edge, split with a gap each side of every white section major (the
    // grid/segment math lives in fuel_scale.c, host-tested), anti-aliased
    // via a rounded-box SDF in (arc-length, radial) space -- the same
    // construction as the tach redline segments.
    bool     low = level <= FUEL_RED_LEVELS;
    uint32_t lit = low ? VROD_RED_BRIGHT : VROD_ORANGE;
    uint8_t  cb = lit & 0xFF, cg = (lit >> 8) & 0xFF, cr = (lit >> 16) & 0xFF;

    const float deg2rad = (float)M_PI / 180.0f;
    float       segs[FUEL_SCALE_MAX_SEGS][2];
    int         nseg = fuel_scale_segs(level, g->band_tick_gap_deg / g->arc_span_deg, segs);

    const float band_half_w = g->tick_h / 2.0f;
    const float band_c      = geom_out_r(g) - band_half_w;  // rim-aligned, like the ticks
    for (int y = 0; y < g->img_h; y++) {
        for (int x = 0; x < g->img_w; x++) {
            float px = (float)(x + g->img_x) - (float)(g->cont_w / 2);
            float py = (float)(y + g->img_y) - (float)g->arc_cy;
            float r  = sqrtf(px * px + py * py);
            if (fabsf(r - band_c) > band_half_w + 1.5f)
                continue;

            float ang = atan2f(py, px) / deg2rad;
            float cov = 0.0f;
            for (int s = 0; s < nseg; s++) {
                float sa_hi    = a0 - segs[s][0] * g->arc_span_deg;
                float sa_lo    = a0 - segs[s][1] * g->arc_span_deg;
                float a_c      = 0.5f * (sa_hi + sa_lo);
                float half_arc = 0.5f * (sa_hi - sa_lo) * deg2rad * r;
                float p_arc    = (ang - a_c) * deg2rad * r;
                float c =
                    sprite_arc_seg_cov(p_arc, r - band_c, half_arc, band_half_w, g->band_corner_r);
                if (c > cov)
                    cov = c;
            }
            if (cov <= 0.0f)
                continue;

            int     idx = (y * g->img_w + x) * 4;
            uint8_t a   = (uint8_t)(cov * 255.0f + 0.5f);
            if (a > fd->buf[idx + 3]) {  // band wins over any tick edge
                fd->buf[idx + 0] = cb;
                fd->buf[idx + 1] = cg;
                fd->buf[idx + 2] = cr;
                fd->buf[idx + 3] = a;
            }
        }
    }

    draw_majors(fd);
}

static lv_obj_t *fuel_arc_create_geom(lv_obj_t *parent, const fuel_geom_t *g)
{
    lv_obj_t *cont = widget_container_create(parent, g->cont_w, g->cont_h);

    fuel_data_t *fd = lv_malloc(sizeof(fuel_data_t));
    // lv_malloc does not zero. The tach sprites use static (zeroed) image
    // descriptors; fd->dsc lives in this heap struct, so its header.flags would
    // otherwise be garbage -- a stray COMPRESSED/ALLOCATED bit makes LVGL's
    // decoder reject the raw ARGB strip and draw nothing (invisible on device,
    // while the sim heap happens to come up zeroed).
    memset(fd, 0, sizeof(*fd));
    fd->g          = g;
    fd->last_level = 0;
    fd->has_value  = false;
    fd->buf =
        heap_caps_malloc((size_t)g->img_w * g->img_h * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    lv_obj_t *img = lv_image_create(cont);
    if (fd->buf) {
        draw_ticks(fd, 0);
        sprite_dsc_init_argb(&fd->dsc, fd->buf, g->img_w, g->img_h);
        lv_image_set_src(img, &fd->dsc);
    }
    lv_obj_set_pos(img, g->img_x, g->img_y);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    fd->tick_img = img;

    // Labels + icon sit ABOVE the arc: pump icon centred, E / F at the ends.
    lv_obj_t *icon = lv_label_create(cont);
    lv_obj_set_style_text_font(icon, g->icon_font, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(VROD_ICON), 0);
    lv_label_set_text(icon, ICON_FUEL);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, g->icon_ofs_x, g->icon_ofs_y);
    fd->icon = icon;

    lv_obj_t *e_lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(e_lbl, g->label_font, 0);
    lv_obj_set_style_text_color(e_lbl, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(e_lbl, "E");
    lv_obj_align(e_lbl, LV_ALIGN_TOP_MID, -g->arc_end_dx, g->label_ofs_y);

    lv_obj_t *f_lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(f_lbl, g->label_font, 0);
    lv_obj_set_style_text_color(f_lbl, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(f_lbl, "F");
    lv_obj_align(f_lbl, LV_ALIGN_TOP_MID, g->arc_end_dx, g->label_ofs_y);

    lv_obj_set_user_data(cont, fd);
    return cont;
}

lv_obj_t *fuel_arc_create(lv_obj_t *parent)
{
    return fuel_arc_create_geom(parent, &FUEL_GEOM_FULL);
}

lv_obj_t *fuel_arc_create_compact(lv_obj_t *parent)
{
    return fuel_arc_create_geom(parent, &FUEL_GEOM_COMPACT);
}

void fuel_arc_set_level(lv_obj_t *cont, uint8_t level)
{
    fuel_data_t *fd = lv_obj_get_user_data(cont);
    if (!fd)
        return;
    if (level > FUEL_LEVELS)
        level = FUEL_LEVELS;
    if (fd->has_value && fd->last_level == level)
        return;
    fd->last_level = level;
    fd->has_value  = true;

    if (fd->buf) {
        draw_ticks(fd, level);
        lv_obj_invalidate(fd->tick_img);  // one invalidation, not 21
    }
    uint32_t icon_color = (level <= FUEL_ICON_RED_LEVELS) ? VROD_RED_BRIGHT : VROD_ICON;
    lv_obj_set_style_text_color(fd->icon, lv_color_hex(icon_color), 0);
}
