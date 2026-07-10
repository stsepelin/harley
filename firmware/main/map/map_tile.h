#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Compact vector map tile (ZMT0). Baked offline by tools/maptiles/bake.py;
// see that file's header for the byte layout. Coordinates are tile-local,
// quantised to a 0..MAP_TILE_EXTENT grid. This parser is pure (no LVGL, no
// filesystem) so it host-tests directly and drops onto the P4 unchanged; the
// directory loader below is the host/sim convenience that reads from disk.

#define MAP_TILE_EXTENT 4096

typedef struct {
    uint8_t         type;   // 0 = polyline (stroke), 1 = polygon (fill)
    uint8_t         style;  // index into map_style.h
    uint16_t        npts;
    const uint16_t *xy;  // npts interleaved (x,y) pairs; points into `raw`
} map_feature_t;

typedef struct {
    uint32_t       tx, ty;
    uint16_t       nfeat;
    map_feature_t *feats;
    uint8_t       *raw;  // owning copy of the tile bytes; xy points into it
} map_tile_t;

typedef struct {
    int         zoom;
    int         ntiles;
    map_tile_t *tiles;
    uint8_t    *owned;  // backing archive buffer to free with the set, or NULL
    // Tile-coordinate bounding box of the baked area (inclusive). Used to tell
    // whether the rider's position has map data (else "off area", e.g. another
    // country). Valid only when ntiles > 0.
    uint32_t min_tx, min_ty, max_tx, max_ty;
} map_tileset_t;

// Parse one tile's bytes into `out` (copies the data). false on bad magic /
// truncation. Free with map_tile_free.
bool map_tile_parse(const uint8_t *data, size_t len, map_tile_t *out);
void map_tile_free(map_tile_t *t);

// Load every <dir>/<zoom>/<x>/<y>.bin (zoom from manifest.json). Host/sim only.
map_tileset_t *map_tileset_load_dir(const char *dir);

// Load a packed ZMTA archive from memory (flash-mapped embedded blob). Tiles are
// parsed in place - `data` must outlive the tileset. See tools/maptiles/pack.py.
map_tileset_t *map_tileset_load_mem(const uint8_t *data, size_t len);

// Same, but the tileset takes ownership of `data` (a heap buffer) and frees it
// in map_tileset_free. Use for a ZMTA read off SD/PSRAM rather than flash.
map_tileset_t *map_tileset_load_mem_owned(uint8_t *data, size_t len);

// Read a whole ZMTA archive from a filesystem path (e.g. /sdcard/map.zmta) into
// a heap buffer and load it (owned). NULL on open / bad-archive. Host, sim, and
// the on-device SD path all reach the file the same way via VFS.
map_tileset_t *map_tileset_load_file(const char *path);

void map_tileset_free(map_tileset_t *ts);

// True if tile (tx,ty) lies within the baked area's bounding box - i.e. the map
// has data to render for that position. False for an empty set. A gap inside the
// box still counts as covered (it renders as background, not "off area").
bool map_tileset_covers(const map_tileset_t *ts, uint32_t tx, uint32_t ty);

// Slippy-map projection: lon/lat degrees -> fractional tile coordinate at zoom.
void map_lonlat_to_tilef(double lon, double lat, int zoom, double *tx, double *ty);
