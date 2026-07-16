#include "map_tile.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Parse a ZMT0 tile. When `copy` is true the bytes are duplicated into an owned
// buffer (files can be freed after); when false the feature xy pointers alias
// `data` directly (for flash-mapped embedded archives - zero RAM copy; `data`
// must outlive the tile). feats[] is always heap-owned.
static bool parse_into(const uint8_t *data, size_t len, map_tile_t *out, bool copy)
{
    // magic(4) + z(2) + x(4) + y(4) + count(2) = 16-byte header.
    if (len < 16 || memcmp(data, "ZMT0", 4) != 0)
        return false;

    uint16_t       nfeat = rd16(data + 14);
    const uint8_t *base  = data;
    uint8_t       *raw   = NULL;
    if (copy) {
        raw = malloc(len);
        if (!raw)
            return false;
        memcpy(raw, data, len);
        base = raw;
    }

    map_feature_t *feats = nfeat ? calloc(nfeat, sizeof(map_feature_t)) : NULL;
    if (nfeat && !feats) {
        free(raw);
        return false;
    }

    size_t off = 16;
    for (uint16_t i = 0; i < nfeat; i++) {
        if (off + 4 > len)
            goto bad;
        uint8_t  type  = base[off];
        uint8_t  style = base[off + 1];
        uint16_t npts  = rd16(base + off + 2);
        off += 4;
        if (off + (size_t)npts * 4 > len)
            goto bad;
        feats[i].type  = type;
        feats[i].style = style;
        feats[i].npts  = npts;
        // off is 4-aligned (16 header + 4-byte feat headers + npts*4 runs), and
        // base is malloc/flash-aligned, so the uint16 view is safe on LE targets.
        feats[i].xy = (const uint16_t *)(base + off);
        off += (size_t)npts * 4;
    }

    out->tx    = rd32(base + 6);
    out->ty    = rd32(base + 10);
    out->nfeat = nfeat;
    out->feats = feats;
    out->raw   = raw;
    return true;

bad:
    free(feats);
    free(raw);
    return false;
}

bool map_tile_parse(const uint8_t *data, size_t len, map_tile_t *out)
{
    return parse_into(data, len, out, true);
}

// `owned` (when non-NULL) is a heap buffer equal to `data` that the tileset
// takes over and frees on destroy - lets an SD/PSRAM archive be parsed in place.
static map_tileset_t *load_mem_impl(const uint8_t *data, size_t len, uint8_t *owned)
{
    // ZMTA: magic(4) + zoom(2) + rsvd(2) + count(4), then 16-byte index entries.
    if (len < 12 || memcmp(data, "ZMTA", 4) != 0) {
        free(owned);
        return NULL;
    }
    uint16_t zoom  = rd16(data + 4);
    uint32_t count = rd32(data + 8);
    if (12 + (size_t)count * 16 > len) {
        free(owned);
        return NULL;
    }

    map_tileset_t *ts = calloc(1, sizeof(*ts));
    ts->zoom          = zoom;
    ts->tiles         = calloc(count, sizeof(map_tile_t));
    ts->owned         = owned;

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e   = data + 12 + (size_t)i * 16;
        uint32_t       off = rd32(e + 8), tlen = rd32(e + 12);
        if ((size_t)off + tlen > len)
            continue;
        // Parse in place: xy aliases the archive (flash or the owned buffer).
        if (parse_into(data + off, tlen, &ts->tiles[ts->ntiles], false))
            ts->ntiles++;
    }
    return ts;
}

// Cache the tile-coordinate bounding box so off-area checks are O(1) per frame.
static void compute_bbox(map_tileset_t *ts)
{
    if (ts->ntiles <= 0)
        return;
    // Streaming sets keep positions in the compact sidx; whole-loaded sets in
    // the parsed tiles[].
    const map_sidx_t *sidx = ts->sidx;
    ts->min_tx = ts->max_tx = sidx ? sidx[0].tx : ts->tiles[0].tx;
    ts->min_ty = ts->max_ty = sidx ? sidx[0].ty : ts->tiles[0].ty;
    for (int i = 1; i < ts->ntiles; i++) {
        uint32_t tx = sidx ? sidx[i].tx : ts->tiles[i].tx;
        uint32_t ty = sidx ? sidx[i].ty : ts->tiles[i].ty;
        if (tx < ts->min_tx)
            ts->min_tx = tx;
        if (tx > ts->max_tx)
            ts->max_tx = tx;
        if (ty < ts->min_ty)
            ts->min_ty = ty;
        if (ty > ts->max_ty)
            ts->max_ty = ty;
    }
}

bool map_tileset_covers(const map_tileset_t *ts, uint32_t tx, uint32_t ty)
{
    if (!ts || ts->ntiles <= 0)
        return false;
    return tx >= ts->min_tx && tx <= ts->max_tx && ty >= ts->min_ty && ty <= ts->max_ty;
}

// Order tiles by (tx, ty) so streaming lookups can binary-search. pack.py sorts
// y filenames as strings, not numerically, so the on-disk index is not usably
// ordered; sort in RAM at load instead (independent of the packer).
static int tile_cmp(const void *a, const void *b)
{
    const map_tile_t *x = a, *y = b;
    if (x->tx != y->tx)
        return x->tx < y->tx ? -1 : 1;
    if (x->ty != y->ty)
        return x->ty < y->ty ? -1 : 1;
    return 0;
}

static int sidx_cmp(const void *a, const void *b)
{
    const map_sidx_t *x = a, *y = b;
    if (x->tx != y->tx)
        return x->tx < y->tx ? -1 : 1;
    if (x->ty != y->ty)
        return x->ty < y->ty ? -1 : 1;
    return 0;
}

// Binary search the streaming index (sidx) for (tx,ty). Streaming-only: the
// caller is map_tileset_read_tile, which the whole-loaded path never reaches.
static int find_index(const map_tileset_t *ts, uint32_t tx, uint32_t ty)
{
    int lo = 0, hi = ts->ntiles - 1;
    while (lo <= hi) {
        int      mid = (lo + hi) / 2;
        uint32_t mtx = ts->sidx[mid].tx, mty = ts->sidx[mid].ty;
        if (mtx == tx && mty == ty)
            return mid;
        if (mtx < tx || (mtx == tx && mty < ty))
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

map_tileset_t *map_tileset_load_mem(const uint8_t *data, size_t len)
{
    map_tileset_t *ts = load_mem_impl(data, len, NULL);
    if (ts)
        compute_bbox(ts);
    return ts;
}

map_tileset_t *map_tileset_load_mem_owned(uint8_t *data, size_t len)
{
    map_tileset_t *ts = load_mem_impl(data, len, data);
    if (ts)
        compute_bbox(ts);
    return ts;
}

void map_tile_free(map_tile_t *t)
{
    if (!t)
        return;
    free(t->feats);
    free(t->raw);
    t->feats = NULL;
    t->raw   = NULL;
    t->nfeat = 0;
}

void map_lonlat_to_tilef(double lon, double lat, int zoom, double *tx, double *ty)
{
    double n    = (double)(1u << zoom);
    *tx         = (lon + 180.0) / 360.0 * n;
    double latr = lat * M_PI / 180.0;
    *ty         = (1.0 - asinh(tan(latr)) / M_PI) / 2.0 * n;
}

void map_tilef_to_lonlat(double tx, double ty, int zoom, double *lon, double *lat)
{
    double n = (double)(1u << zoom);
    *lon     = tx / n * 360.0 - 180.0;
    *lat     = atan(sinh(M_PI * (1.0 - 2.0 * ty / n))) * 180.0 / M_PI;
}

// --- host/sim directory loader --------------------------------------------

static int read_zoom(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    char   buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n]     = '\0';
    char *z    = strstr(buf, "\"zoom\"");
    int   zoom = -1;
    if (z)
        sscanf(z, "\"zoom\"%*[^0-9]%d", &zoom);
    return zoom;
}

static uint8_t *read_file(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(len > 0 ? (size_t)len : 1);
    if (buf && len > 0)
        len = (long)fread(buf, 1, (size_t)len, f);
    fclose(f);
    *len_out = (size_t)len;
    return buf;
}

map_tileset_t *map_tileset_load_dir(const char *dir)
{
    int zoom = read_zoom(dir);
    if (zoom < 0)
        return NULL;

    map_tileset_t *ts = calloc(1, sizeof(*ts));
    ts->zoom          = zoom;
    int cap           = 64;
    ts->tiles         = malloc(sizeof(map_tile_t) * cap);

    char zdir[1024];
    snprintf(zdir, sizeof(zdir), "%s/%d", dir, zoom);
    DIR *zd = opendir(zdir);
    if (!zd) {
        free(ts->tiles);
        free(ts);
        return NULL;
    }
    struct dirent *xe;
    while ((xe = readdir(zd))) {
        if (xe->d_name[0] == '.')
            continue;
        char xdir[1200];
        snprintf(xdir, sizeof(xdir), "%s/%s", zdir, xe->d_name);
        DIR *xd = opendir(xdir);
        if (!xd)
            continue;
        struct dirent *ye;
        while ((ye = readdir(xd))) {
            if (ye->d_name[0] == '.' || !strstr(ye->d_name, ".bin"))
                continue;
            char path[2600];
            snprintf(path, sizeof(path), "%s/%s", xdir, ye->d_name);
            size_t   len;
            uint8_t *bytes = read_file(path, &len);
            if (!bytes)
                continue;
            map_tile_t t;
            if (map_tile_parse(bytes, len, &t)) {
                if (ts->ntiles == cap) {
                    cap *= 2;
                    ts->tiles = realloc(ts->tiles, sizeof(map_tile_t) * cap);
                }
                ts->tiles[ts->ntiles++] = t;
            }
            free(bytes);
        }
        closedir(xd);
    }
    closedir(zd);
    compute_bbox(ts);
    return ts;
}

void map_tileset_free(map_tileset_t *ts)
{
    if (!ts)
        return;
    // Whole-loaded sets own parsed tiles[]; streaming sets own only the compact
    // sidx (geometry is transient, read + freed per map_tileset_read_tile).
    if (ts->tiles) {
        for (int i = 0; i < ts->ntiles; i++)
            map_tile_free(&ts->tiles[i]);
        free(ts->tiles);
    }
    free(ts->sidx);
    free(ts->owned);
    free(ts->rbuf);
    if (ts->fp)
        fclose((FILE *)ts->fp);
    free(ts);
}

map_tileset_t *map_tileset_open_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "ZMTA", 4) != 0) {
        fclose(f);
        return NULL;
    }
    uint16_t zoom  = rd16(hdr + 4);
    uint32_t count = rd32(hdr + 8);

    map_tileset_t *ts = calloc(1, sizeof(*ts));
    if (!ts) {
        fclose(f);
        return NULL;
    }
    ts->zoom = zoom;
    ts->fp   = f;
    // Compact 16-B-per-tile index (not the 28-B parsed map_tile_t): a
    // country-sized set costs ~half the RAM, and geometry is read off the card
    // on demand anyway.
    ts->sidx = count ? calloc(count, sizeof(map_sidx_t)) : NULL;
    if (count && !ts->sidx) {
        fclose(f);
        free(ts);
        return NULL;
    }

    // Read the whole on-disk index in one go (count * 16 bytes) - far fewer
    // syscalls than per-entry reads for a big set. Only tx/ty/offset/len.
    size_t   idx_sz = (size_t)count * 16;
    uint8_t *idx    = idx_sz ? malloc(idx_sz) : NULL;
    if (idx && fread(idx, 1, idx_sz, f) == idx_sz) {
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *e          = idx + (size_t)i * 16;
            ts->sidx[ts->ntiles].tx   = rd32(e);
            ts->sidx[ts->ntiles].ty   = rd32(e + 4);
            ts->sidx[ts->ntiles].foff = rd32(e + 8);
            ts->sidx[ts->ntiles].flen = rd32(e + 12);
            ts->ntiles++;
        }
    }
    free(idx);
    qsort(ts->sidx, ts->ntiles, sizeof(map_sidx_t), sidx_cmp);
    compute_bbox(ts);
    return ts;
}

static uint32_t s_read_fails;  // streaming fseek/fread/parse errors, for on-device diagnostics

uint32_t map_tileset_read_fails(void)
{
    return s_read_fails;
}

bool map_tileset_read_tile(map_tileset_t *ts, uint32_t tx, uint32_t ty, map_tile_t *out)
{
    if (!ts || !ts->fp)
        return false;
    int i = find_index(ts, tx, ty);
    if (i < 0)
        return false;  // tile genuinely absent (gap in the bake) - not a read error
    uint32_t off = ts->sidx[i].foff, len = ts->sidx[i].flen;

    // Reuse one growable buffer instead of malloc+free per tile: a ride streams
    // thousands of tiles, and that variable-size churn fragments PSRAM until an
    // allocation fails and the whole map blanks (only a reboot recovered). The
    // buffer only ever grows to the largest tile; the tile views it directly
    // (copy=false), which is valid until the next read - and cache_get renders
    // then frees each tile before the next read.
    if (len > ts->rcap) {
        uint8_t *nb = realloc(ts->rbuf, len);
        if (!nb) {
            s_read_fails++;
            return false;
        }
        ts->rbuf = nb;
        ts->rcap = len;
    }

    FILE *f = (FILE *)ts->fp;
    clearerr(
        f);  // a prior transient SD error must not poison every later read (fseek clears only EOF)
    if (fseek(f, (long)off, SEEK_SET) != 0 || fread(ts->rbuf, 1, len, f) != len ||
        !parse_into(ts->rbuf, len, out, false)) {
        s_read_fails++;
        return false;
    }
    return true;
}

map_tileset_t *map_tileset_load_file(const char *path)
{
    size_t   len;
    uint8_t *bytes = read_file(path, &len);
    if (!bytes)
        return NULL;
    map_tileset_t *ts = map_tileset_load_mem_owned(bytes, len);  // frees bytes on failure
    return ts;
}
