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
    uint8_t       *raw;         // owning copy of the tile bytes; xy points into it
    uint32_t       foff, flen;  // byte offset + length in the archive (streaming mode)
} map_tile_t;

// Compact streaming index entry: a tile's grid position + its byte range in the
// archive, nothing else. 16 B vs the full map_tile_t (~28 B on the P4), so a
// country-sized index (hundreds of k tiles) costs roughly half the RAM. Used
// only by the streaming loader (map_tileset_open_file); whole-loaded sets keep
// parsed map_tile_t entries in `tiles` instead.
typedef struct {
    uint32_t tx, ty;
    uint32_t foff, flen;
} map_sidx_t;

typedef struct {
    int         zoom;
    int         ntiles;
    map_tile_t *tiles;  // parsed tiles (whole-loaded sets); NULL when streaming
    map_sidx_t *sidx;   // compact index (streaming sets); NULL when whole-loaded
    uint8_t    *owned;  // backing archive buffer to free with the set, or NULL
    void       *fp;     // open FILE* when streaming from disk, else NULL
    uint8_t    *rbuf;   // reusable streaming read buffer (grown, never per-tile malloc'd)
    size_t      rcap;   // capacity of rbuf
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

// Open a ZMTA archive for streaming: read only the index into RAM and keep the
// file open. Tiles carry no geometry until map_tileset_read_tile reads them on
// demand - so a country-sized archive costs ~index bytes of RAM, not the whole
// file. NULL on open / bad-archive.
map_tileset_t *map_tileset_open_file(const char *path);

// Streaming: seek to tile (tx,ty), read + parse it into `out` (owned - free with
// map_tile_free). false if the tile is absent or the read fails. The index is
// sorted at open time, so the lookup is a binary search.
bool map_tileset_read_tile(map_tileset_t *ts, uint32_t tx, uint32_t ty, map_tile_t *out);

// Cumulative count of streaming read errors (fseek/fread/parse/alloc failures) -
// for the on-device map diagnostics. A tile that is simply absent does not count.
uint32_t map_tileset_read_fails(void);

void map_tileset_free(map_tileset_t *ts);

// True if tile (tx,ty) lies within the baked area's bounding box - i.e. the map
// has data to render for that position. False for an empty set. A gap inside the
// box still counts as covered (it renders as background, not "off area").
bool map_tileset_covers(const map_tileset_t *ts, uint32_t tx, uint32_t ty);

// Slippy-map projection: lon/lat degrees -> fractional tile coordinate at zoom.
void map_lonlat_to_tilef(double lon, double lat, int zoom, double *tx, double *ty);

// Inverse: fractional tile coordinate at zoom -> lon/lat degrees. Used by the
// paged-cell source to map a rendered tile back to the cell that holds it.
void map_tilef_to_lonlat(double tx, double ty, int zoom, double *lon, double *lat);
