#include "map_source.h"
#include "map_cells.h"
#include "map_render.h"
#include "map_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Two backing stores behind one interface (firmware/docs/map-worldwide-plan.md):
//
//   SINGLE - one map_tileset_t (flash-embedded demo blob, or one ZMTA streamed
//            off SD). The whole index is resident; fine for a city/region.
//   CELLS  - a GPS-paged grid: <dir>/world.hdr names the baked cells, and a small
//            working set of open per-cell archives follows the rider so only the
//            tiles near the current position cost RAM/FDs. This is the continent-
//            scale path.
//
// The render path (screen_map.c) only ever calls map_source_*, so it is agnostic
// to which store is underneath.

enum { SRC_SINGLE, SRC_CELLS };

// 3x3 working set: the cell under the rider plus its 8 neighbours, so crossing a
// cell border never blanks the leading edge (the neighbour is already open).
#define CELL_RADIUS 1
#define CELL_WINDOW ((2 * CELL_RADIUS + 1) * (2 * CELL_RADIUS + 1))

typedef struct {
    bool           used;  // slot holds a cell we have decided to keep resident
    map_cell_t     cell;
    map_tileset_t *ts;  // open streaming archive, or NULL if the cell file was empty
} cell_slot_t;

struct map_source {
    int kind;

    // SINGLE
    map_tileset_t *ts;
    bool           own;

    // CELLS
    map_world_t world;
    char        dir[128];
    map_cell_t  center;
    bool        have_center;
    cell_slot_t slots[CELL_WINDOW];
};

map_source_t *map_source_from_tileset(map_tileset_t *ts, bool own)
{
    if (!ts)
        return NULL;
    map_source_t *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->kind = SRC_SINGLE;
    s->ts   = ts;
    s->own  = own;
    return s;
}

map_source_t *map_source_open_cells(const char *dir)
{
    if (!dir)
        return NULL;
    map_source_t *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->kind = SRC_CELLS;
    snprintf(s->dir, sizeof(s->dir), "%s", dir);

    char hdr[160];
    snprintf(hdr, sizeof(hdr), "%s/world.hdr", dir);
    if (!map_world_load_file(hdr, &s->world)) {
        free(s);
        return NULL;
    }
    return s;
}

// --- cell helpers ----------------------------------------------------------

static int32_t iabs32(int32_t v)
{
    return v < 0 ? -v : v;
}

// The cell a fractional tile coordinate falls in (tile -> lon/lat -> cell).
static map_cell_t cell_at_tile(const map_source_t *s, double tx, double ty)
{
    double lon, lat;
    map_tilef_to_lonlat(tx, ty, s->world.zoom, &lon, &lat);
    return map_cell_of((int32_t)(lon * 1e7), (int32_t)(lat * 1e7), s->world.cell_size_256);
}

static cell_slot_t *find_slot(map_source_t *s, map_cell_t cell)
{
    for (int i = 0; i < CELL_WINDOW; i++)
        if (s->slots[i].used && map_cell_eq(s->slots[i].cell, cell))
            return &s->slots[i];
    return NULL;
}

static void close_slot(cell_slot_t *slot)
{
    if (slot->ts)
        map_tileset_free(slot->ts);
    slot->ts   = NULL;
    slot->used = false;
}

static void open_cell(map_source_t *s, map_cell_t cell)
{
    cell_slot_t *slot = NULL;
    for (int i = 0; i < CELL_WINDOW; i++)
        if (!s->slots[i].used) {
            slot = &s->slots[i];
            break;
        }
    if (!slot)
        return;  // window full (shouldn't happen: desired set <= CELL_WINDOW)

    char path[192];
    snprintf(path, sizeof(path), "%s/%c%d/%c%d.zmt", s->dir, cell.lat >= 0 ? 'N' : 'S',
             (int)iabs32(cell.lat), cell.lon >= 0 ? 'E' : 'W', (int)iabs32(cell.lon));

    slot->used = true;
    slot->cell = cell;
    slot->ts   = map_tileset_open_file(path);  // NULL keeps the slot as "tried, empty"
    if (slot->ts && slot->ts->ntiles == 0) {
        map_tileset_free(slot->ts);
        slot->ts = NULL;
    }
}

// Evict any open cell no longer in the working window, then open at most one
// still-missing cell of the desired set (bounded per-call SD work). Priority:
// the centre cell, then the cell ahead along the heading, then the rest.
static void repage(map_source_t *s, double heading_deg)
{
    for (int i = 0; i < CELL_WINDOW; i++)
        if (s->slots[i].used && !map_cell_in_window(s->slots[i].cell, s->center, CELL_RADIUS))
            close_slot(&s->slots[i]);

    map_cell_t want[CELL_WINDOW];
    int        n = map_cell_window(s->center, CELL_RADIUS, want, CELL_WINDOW);

    map_cell_t ahead = map_cell_ahead(s->center, heading_deg);
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < n; i++) {
            map_cell_t c = want[i];
            bool       priority =
                (pass == 0 && map_cell_eq(c, s->center)) || (pass == 1 && map_cell_eq(c, ahead));
            if (pass == 2)
                priority = true;
            if (!priority)
                continue;
            if (!map_world_covers(&s->world, c))
                continue;  // cell was never baked
            if (find_slot(s, c))
                continue;  // already resident
            open_cell(s, c);
            return;  // one open per call
        }
    }
}

// --- interface -------------------------------------------------------------

int map_source_zoom(const map_source_t *src)
{
    if (!src)
        return 0;
    return src->kind == SRC_CELLS ? src->world.zoom : (src->ts ? src->ts->zoom : 0);
}

bool map_source_covers(map_source_t *src, uint32_t tx, uint32_t ty)
{
    if (!src)
        return false;
    if (src->kind == SRC_CELLS)
        return map_world_covers(&src->world, cell_at_tile(src, tx + 0.5, ty + 0.5));
    return src->ts && map_tileset_covers(src->ts, tx, ty);
}

void map_source_center(const map_source_t *src, double *tx, double *ty)
{
    *tx = *ty = 0.0;
    if (!src)
        return;
    if (src->kind == SRC_CELLS) {
        if (src->world.ncells <= 0)
            return;
        // Centre of the first baked cell - guarantees the initial view has data.
        map_cell_t c   = src->world.cells[0];
        double     deg = src->world.cell_size_256 / 256.0;
        double     lon = (c.lon + 0.5) * deg;
        double     lat = (c.lat + 0.5) * deg;
        map_lonlat_to_tilef(lon, lat, src->world.zoom, tx, ty);
        return;
    }
    if (!src->ts || src->ts->ntiles <= 0)
        return;
    const map_tileset_t *ts = src->ts;
    *tx                     = (ts->min_tx + ts->max_tx + 1) / 2.0;
    *ty                     = (ts->min_ty + ts->max_ty + 1) / 2.0;
}

void map_source_render_tile(map_source_t *src, uint16_t *dst, int px, uint32_t tx, uint32_t ty)
{
    if (!src) {
        map_render_tile_data(dst, px, NULL);
        return;
    }

    if (src->kind == SRC_CELLS) {
        cell_slot_t *slot = find_slot(src, cell_at_tile(src, tx + 0.5, ty + 0.5));
        map_tile_t   tile;
        if (slot && slot->ts && map_tileset_read_tile(slot->ts, tx, ty, &tile)) {
            map_render_tile_data(dst, px, &tile);
            map_tile_free(&tile);
        } else {
            map_render_tile_data(dst, px, NULL);  // cell not resident / gap / off-area
        }
        return;
    }

    if (src->ts->fp) {
        map_tile_t tile;
        if (map_tileset_read_tile(src->ts, tx, ty, &tile)) {
            map_render_tile_data(dst, px, &tile);
            map_tile_free(&tile);
        } else {
            map_render_tile_data(dst, px, NULL);
        }
    } else {
        map_render_tile(dst, px, src->ts, tx, ty);
    }
}

void map_source_set_center(map_source_t *src, double tx, double ty, double heading_deg)
{
    if (!src || src->kind != SRC_CELLS || src->world.ncells <= 0)
        return;
    map_cell_t c = cell_at_tile(src, tx, ty);
    if (!src->have_center || !map_cell_eq(c, src->center)) {
        src->center      = c;
        src->have_center = true;
    }
    repage(src, heading_deg);  // one evict-sweep + at most one open per frame
}

void map_source_free(map_source_t *src)
{
    if (!src)
        return;
    if (src->kind == SRC_CELLS) {
        for (int i = 0; i < CELL_WINDOW; i++)
            close_slot(&src->slots[i]);
        map_world_free(&src->world);
    } else if (src->own) {
        map_tileset_free(src->ts);
    }
    free(src);
}
