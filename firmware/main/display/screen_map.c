#include "screen_map.h"
#include "map_render.h"
#include "esp_heap_caps.h"

#include "fuel_arc.h"
#include "rpm_bar.h"
#include "sprite_raster.h"
#include "theme.h"
#include "units.h"
#include "warning_lights.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(jbm_bold_72);
LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);
LV_FONT_DECLARE(jbm_bold_26);
LV_FONT_DECLARE(mdi_36);
LV_FONT_DECLARE(mdi_60);

#define ICON_ARROW_L "\xF3\xB0\x9C\xB1"  // U+F0731 arrow-left-bold
#define ICON_ARROW_R "\xF3\xB0\x9C\xB4"  // U+F0734 arrow-right-bold

// Compact cluster on the 800x800 round panel: the map fills the top (corners
// masked by the round bezel); below the chord sit a row of warning lamps, then a
// readout row - TEMP | GEAR | SPEED (big, centre) | RPM | FUEL - and a
// horizontal RPM bar hugging the bottom bezel.
#define SCR_W          800
#define MAP_H          452  // chord y; smaller map, taller info section below
#define MOVE_THRESH_PX 1.5  // skip a map redraw if the view shifted less
// Tile-bitmap cache: each map tile is rasterised once at TILE_PX (= the view's
// px-per-tile), then scrolling is just a blit of the cached tiles into the
// canvas - no per-frame re-rasterise. TILE_PX must match the ppt callers pass.
#define TILE_PX 340
#define CACHE_N 24  // >= visible tiles (~3x3) + margin for scroll-in
// Heading-up: the tiles composite into a square scratch bigger than the map
// region (>= its diagonal, ~473 px radius), then one rotated resample fills the
// canvas so the travel direction points up. Allocated lazily; north-up skips it.
#define SCRATCH_SZ 960

static lv_obj_t      *s_canvas;
static lv_obj_t      *s_warn;
static lv_obj_t      *s_temp_v;
static lv_obj_t      *s_gear_v;
static lv_obj_t      *s_speed_v;
static lv_obj_t      *s_speed_u;
static lv_obj_t      *s_rpm_v;
static lv_obj_t      *s_rpm_bar;
static lv_obj_t      *s_fuel_arc;
static lv_obj_t      *s_turn_l;
static lv_obj_t      *s_turn_r;
static lv_obj_t      *s_no_map;  // "off area" overlay, shown when position has no tiles
static map_tileset_t *s_ts;

// Double buffer: the map composites into the back buffer OFF the LVGL lock, then
// the commit swaps the canvas to it under the lock.
static uint16_t *s_buf[2];
static int       s_front;       // buffer the canvas currently shows
static int       s_back_ready;  // buffer holding a fresh render, or -1

static uint16_t *s_scratch;  // heading-up compositing buffer, lazily allocated

static bool   s_have_last;
static double s_last_tx, s_last_ty, s_last_ppt, s_last_heading;

// Tile-bitmap LRU. Each slot holds one rasterised tile at TILE_PX*TILE_PX;
// buffers are allocated lazily in PSRAM as tiles are first seen.
static uint16_t *s_cache_buf[CACHE_N];
static uint32_t  s_cache_tx[CACHE_N], s_cache_ty[CACHE_N];
static bool      s_cache_valid[CACHE_N];
static uint32_t  s_cache_lru[CACHE_N];
static uint32_t  s_lru_clock;

// Return the cached bitmap for tile (tx,ty), rasterising it on a miss (evicting
// the least-recently-used slot). Only misses do CPU work; hits are free.
static const uint16_t *cache_get(uint32_t tx, uint32_t ty)
{
    for (int i = 0; i < CACHE_N; i++) {
        if (s_cache_valid[i] && s_cache_tx[i] == tx && s_cache_ty[i] == ty) {
            s_cache_lru[i] = ++s_lru_clock;
            return s_cache_buf[i];
        }
    }
    int victim = 0;
    for (int i = 0; i < CACHE_N; i++) {
        if (!s_cache_valid[i]) {
            victim = i;
            break;
        }
        if (s_cache_lru[i] < s_cache_lru[victim])
            victim = i;
    }
    if (!s_cache_buf[victim])
        s_cache_buf[victim] =
            heap_caps_malloc((size_t)TILE_PX * TILE_PX * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (s_ts->fp) {
        // Streaming: read + parse this one tile off SD, rasterise, free. Only
        // the cache miss touches the card; hits are pure blits.
        map_tile_t tile;
        if (map_tileset_read_tile(s_ts, tx, ty, &tile)) {
            map_render_tile_data(s_cache_buf[victim], TILE_PX, &tile);
            map_tile_free(&tile);
        } else {
            map_render_tile_data(s_cache_buf[victim], TILE_PX, NULL);  // gap / off-area
        }
    } else {
        map_render_tile(s_cache_buf[victim], TILE_PX, s_ts, tx, ty);
    }
    s_cache_tx[victim]    = tx;
    s_cache_ty[victim]    = ty;
    s_cache_valid[victim] = true;
    s_cache_lru[victim]   = ++s_lru_clock;
    return s_cache_buf[victim];
}

// Blit a TILE_PX*TILE_PX source into dst at (ox,oy), clipped to the canvas.
static void blit_tile(uint16_t *dst, int dw, int dh, const uint16_t *src, int ox, int oy)
{
    for (int sy = 0; sy < TILE_PX; sy++) {
        int dy = oy + sy;
        if (dy < 0 || dy >= dh)
            continue;
        int sx = 0, dx = ox, w = TILE_PX;
        if (dx < 0) {
            sx = -dx;
            w += dx;
            dx = 0;
        }
        if (dx + w > dw)
            w = dw - dx;
        if (w > 0)
            memcpy(&dst[dy * dw + dx], &src[sy * TILE_PX + sx], (size_t)w * sizeof(uint16_t));
    }
}

// A stacked readout: dim caption on top, value below. Returns the value label.
static lv_obj_t *readout(lv_obj_t *p, const char *cap, const lv_font_t *font, uint32_t color, int x,
                         int y_cap, int y_val)
{
    lv_obj_t *c = lv_label_create(p);
    lv_obj_set_style_text_font(c, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(c, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(c, cap);
    lv_obj_align(c, LV_ALIGN_TOP_MID, x, y_cap);

    lv_obj_t *v = lv_label_create(p);
    lv_obj_set_style_text_font(v, font, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(color), 0);
    lv_label_set_text(v, "--");
    lv_obj_align(v, LV_ALIGN_TOP_MID, x, y_val);
    return v;
}

// A frame around a value - just the TOP of the gauge's gear-selector shape
// (flat top + splayed sides, open bottom), baked like gear_indicator: a white
// outline that is thick + opaque in the middle and tapers thinner + fades to
// transparent at the ends, rotated to sit tangent to the fuel arc's E/F ends.
// Buffer is generous (220x200) so the wide shape still fits once rotated ~44 deg
// at the E/F ends. The shape is the gauge selector widened ~1.35x horizontally
// (longer flat top + more splay), centred on the pivot at (110,100).
#define CHIP_W    220
#define CHIP_H    200
#define CHIP_CX   110.0f  // shape centre in buffer-local coords (= CHIP_W/2)
#define CHIP_CY   100.0f
#define CHIP_SPAN 105.0f  // centre-to-end taper/fade span
static const lv_point_precise_t k_chip_top[] = {
    {13, 126}, {45, 86},               // left diagonal side
    {52, 81},  {60, 78},   {71, 76},   // smooth left shoulder
    {110, 75},                         // flat top
    {149, 76}, {160, 78},  {168, 81},  // smooth right shoulder
    {175, 86}, {207, 126},             // right diagonal side
};

static void bake_chip(uint8_t *buf, float deg)
{
    memset(buf, 0, (size_t)CHIP_W * CHIP_H * 4);
    float a = deg * (float)M_PI / 180.0f, ca = cosf(a), sa = sinf(a);
    int   n = sizeof(k_chip_top) / sizeof(k_chip_top[0]);
    for (int i = 0; i < n - 1; i++) {
        float x0 = (float)k_chip_top[i].x, y0 = (float)k_chip_top[i].y;
        float x1 = (float)k_chip_top[i + 1].x, y1 = (float)k_chip_top[i + 1].y;
        int   steps = (int)hypotf(x1 - x0, y1 - y0) + 1;
        for (int s = 0; s <= steps; s++) {
            float t  = (float)s / (float)steps;
            float px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
            // Width taper + alpha fade from the shape centre, like the gauge.
            float d  = fabsf(px - CHIP_CX);
            float tw = fminf(d / CHIP_SPAN, 1.0f);
            float r  = 2.8f - 1.9f * tw;  // ~5.6 px centre -> ~1.8 px ends
            float ta = fminf(fmaxf((d - 46.0f) / (CHIP_SPAN - 46.0f), 0.0f), 1.0f);
            float al = 255.0f * (1.0f - ta);
            // Rotate around the shape centre.
            float rx = px - CHIP_CX, ry = py - CHIP_CY;
            sprite_stamp_disk_max(buf, CHIP_W, CHIP_H, CHIP_CX + rx * ca - ry * sa,
                                  CHIP_CY + rx * sa + ry * ca, r, al, 0xFFFFFF);
        }
    }
}

static lv_obj_t *chip(lv_obj_t *p, const lv_font_t *font, uint32_t color, int x, int y,
                      uint8_t *buf, lv_image_dsc_t *dsc, float deg, const char *cap)
{
    lv_obj_t *f = lv_obj_create(p);
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, CHIP_W, CHIP_H);
    lv_obj_align(f, LV_ALIGN_TOP_MID, x, y);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(f, false, 0);

    if (buf) {
        bake_chip(buf, deg);
        sprite_dsc_init_argb(dsc, buf, CHIP_W, CHIP_H);
        lv_obj_t *img = lv_image_create(f);
        lv_image_set_src(img, dsc);
        lv_obj_set_pos(img, 0, 0);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    }

    const int   rot = (int)lroundf(deg * 10.0f);  // LVGL rotation is 0.1 deg units
    const float a   = deg * (float)M_PI / 180.0f;
    // Frame axes (rotated): "up" points toward the flat top, "down" to the open
    // bottom. Value rides just above centre, caption sits below it, both tilted
    // with the frame.
    const float ux = sinf(a), uy = -cosf(a);
    const int   cx = 0, cyb = (int)CHIP_CY - CHIP_H / 2;  // frame-centre align offset

    if (cap) {
        lv_obj_t *c = lv_label_create(f);
        lv_obj_set_style_text_font(c, &jbm_bold_26, 0);
        lv_obj_set_style_text_color(c, lv_color_hex(VROD_TEXT_DIM), 0);
        lv_label_set_text(c, cap);
        lv_obj_set_style_transform_pivot_x(c, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(c, LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(c, rot, 0);
        lv_obj_set_style_transform_scale_x(c, 160, 0);  // ~x1.6 smaller (256 = 1x)
        lv_obj_set_style_transform_scale_y(c, 160, 0);
        // Above the frame's flat top (outside the shape), tilted with the frame.
        lv_obj_align(c, LV_ALIGN_CENTER, cx + (int)lroundf(44.0f * ux),
                     cyb + (int)lroundf(44.0f * uy));
    }

    lv_obj_t *v = lv_label_create(f);
    lv_obj_set_style_text_font(v, font, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(color), 0);
    lv_label_set_text(v, "--");
    lv_obj_set_style_transform_pivot_x(v, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(v, LV_PCT(50), 0);
    lv_obj_set_style_transform_rotation(v, rot, 0);
    // Centre on the frame's centroid (= pivot); the tilt is applied about the
    // label centre. The caption now lives above the shape, so no offset needed.
    lv_obj_align(v, LV_ALIGN_CENTER, cx, cyb);
    return v;
}

// Place a chip on the "edge" arc: a circle concentric with the round bezel
// (centre 400,400) at the SAME gap from it as the fuel arc (~33 px), so gear
// and temp stick to the screen edge alongside the arc. Position is an angle
// from straight-right; 90 deg is bottom-centre, and a LARGER angle slides the
// frame up-and-out along the left edge (a smaller one down-and-in toward
// centre). The tilt is the arc tangent there, so the frame follows the curve.
#define CHIP_EDGE_R 343.0f
static lv_obj_t *edge_chip(lv_obj_t *p, const lv_font_t *font, uint32_t color, float ang,
                           uint8_t *buf, lv_image_dsc_t *dsc, const char *cap)
{
    float a = ang * (float)M_PI / 180.0f;
    int   x = (int)lroundf(400.0f + CHIP_EDGE_R * cosf(a)) - 400;
    int   y = (int)lroundf(400.0f + CHIP_EDGE_R * sinf(a) - CHIP_CY);
    return chip(p, font, color, x, y, buf, dsc, ang - 90.0f, cap);
}

lv_obj_t *screen_map_create(map_tileset_t *ts, int w, int h)
{
    (void)w;
    (void)h;
    s_ts         = ts;
    s_front      = 0;
    s_back_ready = -1;
    s_have_last  = false;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Two map buffers (top region). PSRAM: 800x505 RGB565 = 808 KB each. Sim
    // shim maps heap_caps to malloc. Start on background for a clean first frame.
    for (int i = 0; i < 2; i++) {
        s_buf[i] = heap_caps_malloc((size_t)SCR_W * MAP_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        for (int p = 0; p < SCR_W * MAP_H; p++)
            s_buf[i][p] = 0x1082;  // MAP_BG565
    }
    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, s_buf[s_front], SCR_W, MAP_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_canvas, LV_ALIGN_TOP_MID, 0, 0);

    // Rider position marker, at the map's vertical centre.
    lv_obj_t *marker = lv_obj_create(scr);
    lv_obj_remove_style_all(marker);
    lv_obj_set_size(marker, 48, 48);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(marker, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(marker, lv_color_black(), 0);
    lv_obj_set_style_border_width(marker, 5, 0);
    lv_obj_align(marker, LV_ALIGN_TOP_MID, 0, MAP_H / 2 - 24);

    // Off-area overlay: shown when the rider's position falls outside the baked
    // tiles (e.g. another country), where the map would otherwise be blank.
    s_no_map = lv_label_create(scr);
    lv_obj_set_style_text_font(s_no_map, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(s_no_map, lv_color_hex(VROD_TEXT), 0);
    lv_obj_set_style_text_align(s_no_map, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(s_no_map, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_no_map, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_no_map, 12, 0);
    lv_obj_set_style_radius(s_no_map, 8, 0);
    lv_label_set_text(s_no_map, "NO MAP\nFOR THIS AREA");
    lv_obj_align(s_no_map, LV_ALIGN_TOP_MID, 0, MAP_H / 2 - 90);
    lv_obj_add_flag(s_no_map, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *div = lv_obj_create(scr);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, 720, 2);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x2A2E36), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, MAP_H + 2);

    // Warning-lamp row just under the map (same lamps as the gauge's chevrons).
    static const lamp_id_t LAMPS[] = {LAMP_OIL,     LAMP_ENGINE,      LAMP_ABS,
                                      LAMP_BATTERY, LAMP_IMMOBILISER, LAMP_BEAM};
    // Full-width segmented shift-light RPM bar across the very top of the info
    // section (the extra room comes from the smaller map).
    s_rpm_bar = rpm_bar_create(scr);
    lv_obj_align(s_rpm_bar, LV_ALIGN_TOP_MID, 0, MAP_H + 14);

    s_warn = warning_lights_create(scr, LAMPS, 6, WARN_LAYOUT_ROW);
    lv_obj_align(s_warn, LV_ALIGN_TOP_MID, 0, MAP_H + 56);

    // Turn-signal arrows flanking the lamp row; lit green when active.
    s_turn_l = lv_label_create(scr);
    lv_obj_set_style_text_font(s_turn_l, &mdi_60, 0);
    lv_obj_set_style_text_color(s_turn_l, lv_color_hex(VROD_ARROW_OFF), 0);
    lv_label_set_text(s_turn_l, ICON_ARROW_L);
    lv_obj_align(s_turn_l, LV_ALIGN_TOP_MID, -332, MAP_H + 52);
    s_turn_r = lv_label_create(scr);
    lv_obj_set_style_text_font(s_turn_r, &mdi_60, 0);
    lv_obj_set_style_text_color(s_turn_r, lv_color_hex(VROD_ARROW_OFF), 0);
    lv_label_set_text(s_turn_r, ICON_ARROW_R);
    lv_obj_align(s_turn_r, LV_ALIGN_TOP_MID, 332, MAP_H + 52);

    // RPM digital readout in the top-left corner, below the bar.
    s_rpm_v = readout(scr, "RPM", &jbm_bold_33, VROD_TEXT, -285, MAP_H + 54, MAP_H + 76);

    // GEAR (left) + TEMP (right) in gear-selector frames stuck to the E/F edges,
    // rotated tangent to the arc. Baked ARGB (thick/opaque centre -> thin/faded
    // ends) so they match the gauge's frame exactly.
    static uint8_t       *gbuf, *tbuf;
    static lv_image_dsc_t gdsc, tdsc;
    gbuf     = heap_caps_malloc((size_t)CHIP_W * CHIP_H * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tbuf     = heap_caps_malloc((size_t)CHIP_W * CHIP_H * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // GEAR (left) + TEMP (right) on the edge arc, just up-and-out from the fuel
    // E/F ends. Angles mirror about 90 deg (bottom-centre).
    s_gear_v = edge_chip(scr, &jbm_bold_45, VROD_ORANGE, 135.0f, gbuf, &gdsc, "GEAR");
    s_temp_v = edge_chip(scr, &jbm_bold_33, VROD_TEXT, 45.0f, tbuf, &tdsc, "TEMP");

    s_speed_v = lv_label_create(scr);
    lv_obj_set_style_text_font(s_speed_v, &jbm_bold_72, 0);
    lv_obj_set_style_text_color(s_speed_v, lv_color_hex(VROD_TEXT), 0);
    lv_label_set_text(s_speed_v, "0");
    lv_obj_align(s_speed_v, LV_ALIGN_TOP_MID, 0, MAP_H + 173);
    s_speed_u = lv_label_create(scr);
    lv_obj_set_style_text_font(s_speed_u, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(s_speed_u, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(s_speed_u, "MPH");
    lv_obj_align(s_speed_u, LV_ALIGN_TOP_MID, 0, MAP_H + 245);

    // Compact fuel arc at the bottom - the same segmented E..F arc as the full
    // gauge, ~1.5x smaller so the map strip has room to breathe.
    s_fuel_arc = fuel_arc_create_compact(scr);
    lv_obj_align(s_fuel_arc, LV_ALIGN_BOTTOM_MID, 0, -12);

    return scr;
}

// Composite the visible tiles into `dst` (dw x dh) by blitting their cached
// bitmaps - a memcpy each, no re-rasterise. The centre tile coord maps to the
// buffer centre. Every covered slot gets a bitmap (real or all-bg), so the blits
// paint the whole buffer - no separate background fill needed.
static void composite_tiles(uint16_t *dst, int dw, int dh, double cx, double cy, double ppt)
{
    double half_w = (dw / 2.0) / ppt;
    double half_h = (dh / 2.0) / ppt;
    int    mintx  = (int)floor(cx - half_w);
    int    maxtx  = (int)floor(cx + half_w);
    int    minty  = (int)floor(cy - half_h);
    int    maxty  = (int)floor(cy + half_h);
    for (int ty = minty; ty <= maxty; ty++) {
        for (int tx = mintx; tx <= maxtx; tx++) {
            const uint16_t *tbuf = cache_get((uint32_t)tx, (uint32_t)ty);
            int             ox   = (int)lrint(dw / 2.0 + ((double)tx - cx) * ppt);
            int             oy   = (int)lrint(dh / 2.0 + ((double)ty - cy) * ppt);
            blit_tile(dst, dw, dh, tbuf, ox, oy);
        }
    }
}

// Rotated resample of the square scratch into the canvas so `heading_deg` (0 =
// north, clockwise) points up: an ahead-in-travel point lands above the marker.
// Nearest-neighbour; out-of-scratch pixels fall back to background.
static void rotate_blit(uint16_t *dst, const uint16_t *src, double heading_deg)
{
    double a  = heading_deg * M_PI / 180.0;
    double ca = cos(a), sa = sin(a);
    double dcx = SCR_W / 2.0, dcy = MAP_H / 2.0;
    double sc = SCRATCH_SZ / 2.0;
    for (int y = 0; y < MAP_H; y++) {
        double ry = y - dcy;
        for (int x = 0; x < SCR_W; x++) {
            double rx          = x - dcx;
            int    sx          = (int)lrint(sc + rx * ca - ry * sa);
            int    sy          = (int)lrint(sc + rx * sa + ry * ca);
            dst[y * SCR_W + x] = ((unsigned)sx < SCRATCH_SZ && (unsigned)sy < SCRATCH_SZ)
                                     ? src[sy * SCRATCH_SZ + sx]
                                     : 0x1082;  // MAP_BG565
        }
    }
}

// heading_deg < 0 keeps the map north-up (cheap direct composite); >= 0 rotates
// so the heading points up (composite to scratch, then one rotated resample).
bool screen_map_render(double center_tx, double center_ty, double ppt, double heading_deg)
{
    if (!s_buf[0])
        return false;
    // Skip if neither the view nor the heading moved since the last render.
    bool heading_turned = heading_deg >= 0 && (!s_have_last || s_last_heading < 0 ||
                                               fabs(heading_deg - s_last_heading) > 0.5);
    if (s_have_last && ppt == s_last_ppt && !heading_turned) {
        double dpx = hypot((center_tx - s_last_tx) * ppt, (center_ty - s_last_ty) * ppt);
        if (dpx < MOVE_THRESH_PX)
            return false;
    }
    int       back = 1 - s_front;
    uint16_t *dst  = s_buf[back];

    if (heading_deg < 0) {
        composite_tiles(dst, SCR_W, MAP_H, center_tx, center_ty, ppt);
    } else {
        if (!s_scratch)
            s_scratch = heap_caps_malloc((size_t)SCRATCH_SZ * SCRATCH_SZ * sizeof(uint16_t),
                                         MALLOC_CAP_SPIRAM);
        if (!s_scratch)
            return false;
        composite_tiles(s_scratch, SCRATCH_SZ, SCRATCH_SZ, center_tx, center_ty, ppt);
        rotate_blit(dst, s_scratch, heading_deg);
    }

    s_back_ready   = back;
    s_have_last    = true;
    s_last_tx      = center_tx;
    s_last_ty      = center_ty;
    s_last_ppt     = ppt;
    s_last_heading = heading_deg;
    return true;
}

void screen_map_set_no_coverage(bool off_area)
{
    static int last = -1;  // cache: avoid re-invalidating the label every frame
    if (last == (int)off_area)
        return;
    last = (int)off_area;
    if (off_area)
        lv_obj_remove_flag(s_no_map, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_no_map, LV_OBJ_FLAG_HIDDEN);
}

void screen_map_commit(const vehicle_data_t *data, const settings_t *settings)
{
    if (s_back_ready >= 0) {
        lv_canvas_set_buffer(s_canvas, s_buf[s_back_ready], SCR_W, MAP_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_invalidate(s_canvas);
        s_front      = s_back_ready;
        s_back_ready = -1;
    }

    lv_label_set_text_fmt(s_temp_v, "%d\xC2\xB0%s",
                          units_temp_display(data->engine_temp_c, settings->temp_units),
                          units_temp_label(settings->temp_units));
    if (data->gear == GEAR_NEUTRAL)
        lv_label_set_text(s_gear_v, "N");
    else if (data->gear >= GEAR_1 && data->gear <= GEAR_6)
        lv_label_set_text_fmt(s_gear_v, "%d", (int)data->gear);
    else
        lv_label_set_text(s_gear_v, "-");
    lv_label_set_text_fmt(s_speed_v, "%d", units_speed_display(data->speed_mph, settings->units));
    lv_label_set_text(s_speed_u, units_speed_label(settings->units));
    lv_label_set_text_fmt(s_rpm_v, "%d", data->rpm);
    rpm_bar_set_rpm(s_rpm_bar, data->rpm);
    fuel_arc_set_level(s_fuel_arc, data->fuel_level);

    // Turn arrows: green when active, dim otherwise. Cached (house rule).
    static int last_tl = -1, last_tr = -1;
    if (last_tl != data->turn_left) {
        last_tl = data->turn_left;
        lv_obj_set_style_text_color(
            s_turn_l, lv_color_hex(data->turn_left ? VROD_GREEN_SIGNAL : VROD_ARROW_OFF), 0);
    }
    if (last_tr != data->turn_right) {
        last_tr = data->turn_right;
        lv_obj_set_style_text_color(
            s_turn_r, lv_color_hex(data->turn_right ? VROD_GREEN_SIGNAL : VROD_ARROW_OFF), 0);
    }

    warning_lights_update(s_warn, data);
}
