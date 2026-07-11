#include "map_render.h"
#include "map_style.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
    uint16_t *buf;
    int       w, h;
} canvas_t;

static inline void put(canvas_t *c, int x, int y, uint16_t color)
{
    if ((unsigned)x < (unsigned)c->w && (unsigned)y < (unsigned)c->h)
        c->buf[y * c->w + x] = color;
}

// Stamp a filled disc of radius `half`. Round stamps (vs a square) give rounded
// caps and smoother joints, so thick strokes look far less blocky.
static void fill_disc(canvas_t *c, int x, int y, int half, uint16_t color)
{
    int r2 = half * half + half;  // slightly generous so the disc keeps full width
    for (int dy = -half; dy <= half; dy++)
        for (int dx = -half; dx <= half; dx++)
            if (dx * dx + dy * dy <= r2)
                put(c, x + dx, y + dy, color);
}

// Bresenham segment, stamping a disc at each step for a smooth rounded stroke.
static void draw_seg(canvas_t *c, int x0, int y0, int x1, int y1, uint16_t color, int half)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (half > 0)
            fill_disc(c, x0, y0, half, color);
        else
            put(c, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Even-odd scanline fill of a screen-space polygon (up to MAX_POLY points).
// Scratch buffers are static, not stack: rendering is single-threaded (the map
// task / LVGL), and 12 KB of stack arrays overflows a FreeRTOS task stack.
#define MAX_POLY 1024
static void fill_poly(canvas_t *c, const float *xs, const float *ys, int n, uint16_t color)
{
    static float xint[MAX_POLY];
    if (n < 3)
        return;
    float ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < ymin)
            ymin = ys[i];
        if (ys[i] > ymax)
            ymax = ys[i];
    }
    int y0 = (int)floorf(ymin), y1 = (int)ceilf(ymax);
    if (y0 < 0)
        y0 = 0;
    if (y1 > c->h)
        y1 = c->h;

    for (int y = y0; y < y1; y++) {
        float yc = y + 0.5f;
        int   m  = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float yi = ys[i], yj = ys[j];
            if ((yi <= yc && yj > yc) || (yj <= yc && yi > yc)) {
                float t = (yc - yi) / (yj - yi);
                if (m < MAX_POLY)
                    xint[m++] = xs[i] + t * (xs[j] - xs[i]);
            }
        }
        for (int a = 0; a < m - 1; a++)
            for (int b = a + 1; b < m; b++)
                if (xint[b] < xint[a]) {
                    float tmp = xint[a];
                    xint[a]   = xint[b];
                    xint[b]   = tmp;
                }
        for (int k = 0; k + 1 < m; k += 2) {
            int xa = (int)ceilf(xint[k] - 0.5f), xb = (int)floorf(xint[k + 1] - 0.5f);
            if (xa < 0)
                xa = 0;
            if (xb >= c->w)
                xb = c->w - 1;
            for (int x = xa; x <= xb; x++)
                put(c, x, y, color);
        }
    }
}

// One feature -> screen coords, drawn. sox/soy = tile screen origin, sc = px/extent.
static void draw_feature(canvas_t *c, const map_feature_t *f, double sox, double soy, double sc,
                         int width_px)
{
    if (f->type == 1) {
        static float xs[MAX_POLY], ys[MAX_POLY];
        int          n = f->npts < MAX_POLY ? f->npts : MAX_POLY;
        for (int i = 0; i < n; i++) {
            xs[i] = (float)(sox + f->xy[i * 2] * sc);
            ys[i] = (float)(soy + f->xy[i * 2 + 1] * sc);
        }
        fill_poly(c, xs, ys, n, map_style(f->style).color);
        return;
    }
    uint16_t color = map_style(f->style).color;
    int      half  = width_px / 2;
    for (int i = 0; i + 1 < f->npts; i++) {
        int x0 = (int)lrint(sox + f->xy[i * 2] * sc);
        int y0 = (int)lrint(soy + f->xy[i * 2 + 1] * sc);
        int x1 = (int)lrint(sox + f->xy[(i + 1) * 2] * sc);
        int y1 = (int)lrint(soy + f->xy[(i + 1) * 2 + 1] * sc);
        draw_seg(c, x0, y0, x1, y1, color, half);
    }
}

void map_render_rgb565(uint16_t *buf, int w, int h, const map_tileset_t *ts, double center_tx,
                       double center_ty, double ppt)
{
    canvas_t c = {buf, w, h};
    for (int i = 0; i < w * h; i++)
        buf[i] = MAP_BG565;

    double sc = ppt / MAP_TILE_EXTENT;  // px per extent unit

    // Layer order: water (base) -> buildings -> roads minor->major (on top), so
    // arterials sit above buildings and water sits under everything. Pass -2 =
    // water fills, -1 = building fills, 0..3 = line style ids.
    for (int pass = -2; pass <= MAP_STYLE_MAJOR; pass++) {
        for (int t = 0; t < ts->ntiles; t++) {
            const map_tile_t *tile = &ts->tiles[t];
            double            sox  = w / 2.0 + ((double)tile->tx - center_tx) * ppt;
            double            soy  = h / 2.0 + ((double)tile->ty - center_ty) * ppt;
            if (sox > w || soy > h || sox + ppt < 0 || soy + ppt < 0)
                continue;  // tile fully off-screen
            for (int fi = 0; fi < tile->nfeat; fi++) {
                const map_feature_t *f = &tile->feats[fi];
                if (pass == -2) {
                    if (f->type != 1 || f->style != MAP_STYLE_WATER)
                        continue;
                } else if (pass == -1) {
                    if (f->type != 1 || f->style != MAP_STYLE_BUILDING)
                        continue;
                } else {
                    if (f->type != 0 || f->style != (uint8_t)pass)
                        continue;
                }
                int width_px = (int)lrint(map_style(f->style).width * ppt / 256.0);
                if (width_px < 1)
                    width_px = 1;
                draw_feature(&c, f, sox, soy, sc, width_px);
            }
        }
    }
}

void map_render_tile_data(uint16_t *dst, int px, const map_tile_t *tile)
{
    canvas_t c = {dst, px, px};
    for (int i = 0; i < px * px; i++)
        dst[i] = MAP_BG565;

    if (!tile)
        return;  // off the baked area / missing tile: background only

    double sc = (double)px / MAP_TILE_EXTENT;  // px per extent unit (tile-local)
    // Water (base) -> buildings -> roads, same layering as map_render_rgb565.
    for (int pass = -2; pass <= MAP_STYLE_MAJOR; pass++) {
        for (int fi = 0; fi < tile->nfeat; fi++) {
            const map_feature_t *f = &tile->feats[fi];
            if (pass == -2) {
                if (f->type != 1 || f->style != MAP_STYLE_WATER)
                    continue;
            } else if (pass == -1) {
                if (f->type != 1 || f->style != MAP_STYLE_BUILDING)
                    continue;
            } else {
                if (f->type != 0 || f->style != (uint8_t)pass)
                    continue;
            }
            int width_px = (int)lrint(map_style(f->style).width * px / 256.0);
            if (width_px < 1)
                width_px = 1;
            draw_feature(&c, f, 0.0, 0.0, sc, width_px);
        }
    }
}

void map_render_tile(uint16_t *dst, int px, const map_tileset_t *ts, uint32_t tx, uint32_t ty)
{
    const map_tile_t *tile = NULL;
    for (int t = 0; t < ts->ntiles; t++) {
        if (ts->tiles[t].tx == tx && ts->tiles[t].ty == ty) {
            tile = &ts->tiles[t];
            break;
        }
    }
    map_render_tile_data(dst, px, tile);
}
