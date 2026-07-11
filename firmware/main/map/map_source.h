#pragma once
#include "map_tile.h"
#include <stdbool.h>
#include <stdint.h>

// A map source is what the map screen draws from. Today it wraps a single
// tileset (a flash-embedded demo blob, or one ZMTA opened for streaming off SD);
// the worldwide plan (firmware/docs/map-worldwide-plan.md) adds a paged-cell
// source behind this same interface so a continent lives on the card with only
// the tiles near the rider resident. The render path (screen_map.c) talks only
// to this seam, never to map_tileset_t directly, so swapping the backing store
// never touches the rasteriser.

typedef struct map_source map_source_t;

// Wrap an already-loaded tileset. `own` = free the tileset in map_source_free
// (true for a load_file/open_file result the source should own; false to borrow
// a flash-embedded or caller-managed tileset). NULL if `ts` is NULL or on OOM.
map_source_t *map_source_from_tileset(map_tileset_t *ts, bool own);

// Open a GPS-paged cell grid: read <dir>/world.hdr and manage a small working
// set of open per-cell archives that follows the rider (map_source_set_center
// pages them in/out). This is what puts a whole continent on the card - only the
// cells near the current position are resident. NULL if world.hdr is missing or
// bad. See firmware/docs/map-worldwide-plan.md and tools/maptiles/world.py.
map_source_t *map_source_open_cells(const char *dir);

// Zoom level of the tiles (all one zoom today).
int map_source_zoom(const map_source_t *src);

// True if (tx,ty) has map data - i.e. the rider's position is inside the baked
// area. False -> the "off area" overlay. See map_tileset_covers.
bool map_source_covers(map_source_t *src, uint32_t tx, uint32_t ty);

// Fractional tile coord at the centre of the available data, so the map shows
// something before the first GPS fix.
void map_source_center(const map_source_t *src, double *tx, double *ty);

// Rasterise tile (tx,ty) into `dst` (px*px RGB565), background-filled on a
// gap/miss. Encapsulates in-memory (parsed in RAM) vs streaming (read off SD on
// demand) so the tile cache in screen_map.c is backing-store agnostic.
void map_source_render_tile(map_source_t *src, uint16_t *dst, int px, uint32_t tx, uint32_t ty);

// Hint the current view centre (fractional tile) + heading so a paged source can
// prefetch/evict cells ahead of the rider. No-op for a single tileset.
void map_source_set_center(map_source_t *src, double tx, double ty, double heading_deg);

void map_source_free(map_source_t *src);
