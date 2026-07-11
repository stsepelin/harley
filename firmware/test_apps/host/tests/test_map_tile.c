#include "unity.h"
#include "map_tile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Regression tests for the vector-map tile parser: it decodes untrusted bytes
// from a flash-embedded archive (or SD), so malformed input must fail cleanly.
// map_tile.c allocates, so it is not in the 100% branch gate (like icon_cache);
// these run in CI as regressions.

static size_t put_u16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    return 2;
}
static size_t put_u32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
    return 4;
}

// Build a ZMT0 tile: one polyline (2 pts) + one polygon (3 pts). Returns length.
static size_t build_tile(uint8_t *b, uint32_t tx, uint32_t ty)
{
    size_t i = 0;
    memcpy(b + i, "ZMT0", 4);
    i += 4;
    i += put_u16(b + i, 16);  // zoom
    i += put_u32(b + i, tx);
    i += put_u32(b + i, ty);
    i += put_u16(b + i, 2);  // nfeat
    // feature 0: polyline, style 3, 2 points
    b[i++] = 0;
    b[i++] = 3;
    i += put_u16(b + i, 2);
    i += put_u16(b + i, 10);
    i += put_u16(b + i, 20);
    i += put_u16(b + i, 30);
    i += put_u16(b + i, 40);
    // feature 1: polygon, style 10, 3 points
    b[i++] = 1;
    b[i++] = 10;
    i += put_u16(b + i, 3);
    i += put_u16(b + i, 100);
    i += put_u16(b + i, 100);
    i += put_u16(b + i, 200);
    i += put_u16(b + i, 100);
    i += put_u16(b + i, 150);
    i += put_u16(b + i, 200);
    return i;
}

static void test_parse_valid_tile(void)
{
    uint8_t    b[256];
    size_t     n = build_tile(b, 37279, 19232);
    map_tile_t t;
    TEST_ASSERT_TRUE(map_tile_parse(b, n, &t));
    TEST_ASSERT_EQUAL_UINT32(37279, t.tx);
    TEST_ASSERT_EQUAL_UINT32(19232, t.ty);
    TEST_ASSERT_EQUAL_UINT16(2, t.nfeat);
    TEST_ASSERT_EQUAL_UINT8(0, t.feats[0].type);
    TEST_ASSERT_EQUAL_UINT8(3, t.feats[0].style);
    TEST_ASSERT_EQUAL_UINT16(2, t.feats[0].npts);
    TEST_ASSERT_EQUAL_UINT16(10, t.feats[0].xy[0]);
    TEST_ASSERT_EQUAL_UINT16(40, t.feats[0].xy[3]);
    TEST_ASSERT_EQUAL_UINT8(1, t.feats[1].type);
    TEST_ASSERT_EQUAL_UINT8(10, t.feats[1].style);
    TEST_ASSERT_EQUAL_UINT16(3, t.feats[1].npts);
    map_tile_free(&t);
}

static void test_parse_bad_magic(void)
{
    uint8_t    b[64] = {'X', 'M', 'T', '0'};
    map_tile_t t;
    TEST_ASSERT_FALSE(map_tile_parse(b, 32, &t));
}

static void test_parse_too_short_header(void)
{
    uint8_t    b[8] = {'Z', 'M', 'T', '0'};
    map_tile_t t;
    TEST_ASSERT_FALSE(map_tile_parse(b, 8, &t));  // < 16-byte header
}

static void test_parse_truncated_feature_points(void)
{
    // Header claims 1 feature with 5 points, but the buffer ends early.
    uint8_t b[32];
    size_t  i = 0;
    memcpy(b + i, "ZMT0", 4);
    i += 4;
    i += put_u16(b + i, 16);
    i += put_u32(b + i, 1);
    i += put_u32(b + i, 2);
    i += put_u16(b + i, 1);  // nfeat = 1
    b[i++] = 0;
    b[i++] = 0;
    i += put_u16(b + i, 5);  // npts = 5 -> needs 20 more bytes, buffer ends here
    map_tile_t t;
    TEST_ASSERT_FALSE(map_tile_parse(b, i, &t));
}

static void test_parse_zero_features(void)
{
    uint8_t b[16];
    size_t  i = 0;
    memcpy(b + i, "ZMT0", 4);
    i += 4;
    i += put_u16(b + i, 16);
    i += put_u32(b + i, 1);
    i += put_u32(b + i, 2);
    i += put_u16(b + i, 0);  // nfeat = 0
    map_tile_t t;
    TEST_ASSERT_TRUE(map_tile_parse(b, i, &t));
    TEST_ASSERT_EQUAL_UINT16(0, t.nfeat);
    map_tile_free(&t);
}

static void test_free_null_is_safe(void)
{
    map_tile_free(NULL);  // must not crash
}

static void test_lonlat_projection(void)
{
    // Tallinn Old Town at z16. Compare against the direct slippy formula.
    double tx, ty;
    map_lonlat_to_tilef(24.745, 59.437, 16, &tx, &ty);
    double n    = 65536.0;
    double ex   = (24.745 + 180.0) / 360.0 * n;
    double latr = 59.437 * M_PI / 180.0;
    double ey   = (1.0 - asinh(tan(latr)) / M_PI) / 2.0 * n;
    TEST_ASSERT_TRUE(fabs(tx - ex) < 0.01);
    TEST_ASSERT_TRUE(fabs(ty - ey) < 0.01);
    // Sanity: Tallinn is around tile x37272 / y19234 at z16.
    TEST_ASSERT_TRUE(tx > 37270 && tx < 37275);
    TEST_ASSERT_TRUE(ty > 19232 && ty < 19237);
}

static void test_lonlat_roundtrip(void)
{
    // Forward then inverse must return the original lon/lat (paged-cell routing
    // relies on this to map a tile back to its cell).
    double tx, ty, lon, lat;
    map_lonlat_to_tilef(24.745, 59.437, 16, &tx, &ty);
    map_tilef_to_lonlat(tx, ty, 16, &lon, &lat);
    TEST_ASSERT_TRUE(fabs(lon - 24.745) < 1e-6);
    TEST_ASSERT_TRUE(fabs(lat - 59.437) < 1e-6);
    // A negative (S/W) position round-trips too.
    map_lonlat_to_tilef(-0.5, -33.9, 16, &tx, &ty);
    map_tilef_to_lonlat(tx, ty, 16, &lon, &lat);
    TEST_ASSERT_TRUE(fabs(lon + 0.5) < 1e-6);
    TEST_ASSERT_TRUE(fabs(lat + 33.9) < 1e-6);
}

// --- ZMTA archive (map_tileset_load_mem) -----------------------------------

// Pack two tiles into a ZMTA archive. Returns length.
static size_t build_archive(uint8_t *b)
{
    uint8_t t0[256], t1[256];
    size_t  l0 = build_tile(t0, 100, 200);
    size_t  l1 = build_tile(t1, 101, 201);

    size_t hdr   = 12;
    size_t index = 2 * 16;
    size_t off0  = (hdr + index + 3) & ~(size_t)3;
    size_t off1  = (off0 + l0 + 3) & ~(size_t)3;

    memset(b, 0, off1 + l1);
    size_t i = 0;
    memcpy(b + i, "ZMTA", 4);
    i += 4;
    i += put_u16(b + i, 16);  // zoom
    i += put_u16(b + i, 0);   // reserved
    i += put_u32(b + i, 2);   // count
    // index
    put_u32(b + i, 100);
    put_u32(b + i + 4, 200);
    put_u32(b + i + 8, (uint32_t)off0);
    put_u32(b + i + 12, (uint32_t)l0);
    i += 16;
    put_u32(b + i, 101);
    put_u32(b + i + 4, 201);
    put_u32(b + i + 8, (uint32_t)off1);
    put_u32(b + i + 12, (uint32_t)l1);
    memcpy(b + off0, t0, l0);
    memcpy(b + off1, t1, l1);
    return off1 + l1;
}

static void test_load_mem_valid_archive(void)
{
    uint8_t        b[1024];
    size_t         n  = build_archive(b);
    map_tileset_t *ts = map_tileset_load_mem(b, n);
    TEST_ASSERT_NOT_NULL(ts);
    TEST_ASSERT_EQUAL_INT(16, ts->zoom);
    TEST_ASSERT_EQUAL_INT(2, ts->ntiles);
    TEST_ASSERT_EQUAL_UINT32(100, ts->tiles[0].tx);
    TEST_ASSERT_EQUAL_UINT32(201, ts->tiles[1].ty);
    TEST_ASSERT_EQUAL_UINT16(2, ts->tiles[0].nfeat);
    map_tileset_free(ts);
}

static void test_load_mem_bad_magic(void)
{
    uint8_t b[16] = {'Z', 'M', 'T', 'X'};
    TEST_ASSERT_NULL(map_tileset_load_mem(b, 16));
}

static void test_load_mem_too_short(void)
{
    uint8_t b[8] = {'Z', 'M', 'T', 'A'};
    TEST_ASSERT_NULL(map_tileset_load_mem(b, 8));  // < 12-byte header
}

static void test_load_mem_index_overruns(void)
{
    uint8_t b[64];
    size_t  i = 0;
    memcpy(b + i, "ZMTA", 4);
    i += 4;
    i += put_u16(b + i, 16);
    i += put_u16(b + i, 0);
    i += put_u32(b + i, 1000);  // claims 1000 tiles; index won't fit in 64 bytes
    TEST_ASSERT_NULL(map_tileset_load_mem(b, 32));
}

static void test_load_mem_skips_out_of_range_tile(void)
{
    // One index entry points past the end of the buffer -> skipped, ntiles 0.
    uint8_t b[64];
    size_t  i = 0;
    memcpy(b + i, "ZMTA", 4);
    i += 4;
    i += put_u16(b + i, 16);
    i += put_u16(b + i, 0);
    i += put_u32(b + i, 1);
    put_u32(b + i, 5);
    put_u32(b + i + 4, 6);
    put_u32(b + i + 8, 9999);
    put_u32(b + i + 12, 40);  // offset way past end
    map_tileset_t *ts = map_tileset_load_mem(b, 28 + 16);
    TEST_ASSERT_NOT_NULL(ts);
    TEST_ASSERT_EQUAL_INT(0, ts->ntiles);
    map_tileset_free(ts);
}

// --- directory loader (host/sim) -------------------------------------------

static void write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    fwrite(data, 1, len, f);
    fclose(f);
}

static void test_load_dir_reads_tiles(void)
{
    char dir[] = "/tmp/zmt_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(dir));

    char path[512];
    snprintf(path, sizeof(path), "%s/manifest.json", dir);
    const char *man = "{\"zoom\": 16, \"tiles\": 1}";
    write_file(path, (const uint8_t *)man, strlen(man));

    snprintf(path, sizeof(path), "%s/16", dir);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s/16/100", dir);
    mkdir(path, 0777);

    uint8_t tile[256];
    size_t  tl = build_tile(tile, 100, 200);
    snprintf(path, sizeof(path), "%s/16/100/200.bin", dir);
    write_file(path, tile, tl);

    map_tileset_t *ts = map_tileset_load_dir(dir);
    TEST_ASSERT_NOT_NULL(ts);
    TEST_ASSERT_EQUAL_INT(16, ts->zoom);
    TEST_ASSERT_EQUAL_INT(1, ts->ntiles);
    TEST_ASSERT_EQUAL_UINT32(100, ts->tiles[0].tx);
    map_tileset_free(ts);
}

static void test_load_dir_missing_manifest(void)
{
    char dir[] = "/tmp/zmt_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(dir));
    TEST_ASSERT_NULL(map_tileset_load_dir(dir));  // no manifest.json
}

// --- file / owned-buffer loaders (SD path) ---------------------------------

static void test_load_mem_owned_takes_ownership(void)
{
    uint8_t  stack[1024];
    size_t   n   = build_archive(stack);
    uint8_t *buf = malloc(n);  // freed by map_tileset_free, not by us
    memcpy(buf, stack, n);
    map_tileset_t *ts = map_tileset_load_mem_owned(buf, n);
    TEST_ASSERT_NOT_NULL(ts);
    TEST_ASSERT_EQUAL_PTR(buf, ts->owned);
    TEST_ASSERT_EQUAL_INT(2, ts->ntiles);
    map_tileset_free(ts);  // ASan would flag a leak or double-free here
}

static void test_load_mem_owned_frees_on_bad_archive(void)
{
    uint8_t *buf = malloc(8);
    memcpy(buf, "ZMTX", 4);
    TEST_ASSERT_NULL(map_tileset_load_mem_owned(buf, 8));  // bad magic -> frees buf
}

static void test_load_file_reads_archive(void)
{
    uint8_t b[1024];
    size_t  n      = build_archive(b);
    char    path[] = "/tmp/zmta_XXXXXX";
    int     fd     = mkstemp(path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);
    write_file(path, b, n);

    map_tileset_t *ts = map_tileset_load_file(path);
    TEST_ASSERT_NOT_NULL(ts);
    TEST_ASSERT_EQUAL_INT(16, ts->zoom);
    TEST_ASSERT_EQUAL_INT(2, ts->ntiles);
    TEST_ASSERT_EQUAL_UINT32(100, ts->tiles[0].tx);
    TEST_ASSERT_NOT_NULL(ts->owned);
    map_tileset_free(ts);
    unlink(path);
}

static void test_load_file_missing(void)
{
    TEST_ASSERT_NULL(map_tileset_load_file("/tmp/does_not_exist_zmta_9v3"));
}

// --- streaming loader (index in RAM, tiles read on demand) ------------------

static void test_open_file_streams_tiles(void)
{
    uint8_t b[1024];
    size_t  n      = build_archive(b);  // tiles (100,200) and (101,201)
    char    path[] = "/tmp/zmta_str_XXXXXX";
    int     fd     = mkstemp(path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);
    write_file(path, b, n);

    map_tileset_t *ts = map_tileset_open_file(path);
    TEST_ASSERT_NOT_NULL(ts);
    TEST_ASSERT_EQUAL_INT(16, ts->zoom);
    TEST_ASSERT_EQUAL_INT(2, ts->ntiles);
    TEST_ASSERT_NOT_NULL(ts->fp);                        // file kept open
    TEST_ASSERT_TRUE(map_tileset_covers(ts, 100, 200));  // bbox computed at open

    map_tile_t t;
    TEST_ASSERT_TRUE(map_tileset_read_tile(ts, 100, 200, &t));  // read on demand
    TEST_ASSERT_EQUAL_UINT32(100, t.tx);
    TEST_ASSERT_EQUAL_UINT16(2, t.nfeat);
    map_tile_free(&t);

    TEST_ASSERT_FALSE(map_tileset_read_tile(ts, 999, 999, &t));  // absent tile
    map_tileset_free(ts);                                        // closes the file
    unlink(path);
}

static void test_read_tile_rejects_null_and_nonstreaming(void)
{
    map_tile_t t;
    TEST_ASSERT_FALSE(map_tileset_read_tile(NULL, 1, 1, &t));
    // A whole-loaded (non-streaming) set has fp==NULL, so read_tile is a no-op.
    uint8_t        b[1024];
    size_t         n  = build_archive(b);
    map_tileset_t *ts = map_tileset_load_mem(b, n);
    TEST_ASSERT_FALSE(map_tileset_read_tile(ts, 100, 200, &t));
    map_tileset_free(ts);
}

static void test_open_file_missing(void)
{
    TEST_ASSERT_NULL(map_tileset_open_file("/tmp/does_not_exist_zmta_str_x"));
}

// --- coverage / off-area (bounding box over the baked tiles) ----------------

static void test_covers_within_and_outside_bbox(void)
{
    uint8_t        b[1024];
    size_t         n  = build_archive(b);  // tiles (100,200) and (101,201)
    map_tileset_t *ts = map_tileset_load_mem(b, n);
    TEST_ASSERT_NOT_NULL(ts);
    // Inside the box - both real tiles and a gap between them count as covered.
    TEST_ASSERT_TRUE(map_tileset_covers(ts, 100, 200));
    TEST_ASSERT_TRUE(map_tileset_covers(ts, 101, 201));
    TEST_ASSERT_TRUE(map_tileset_covers(ts, 100, 201));  // gap, still "in area"
    // Outside the box on each side.
    TEST_ASSERT_FALSE(map_tileset_covers(ts, 99, 200));
    TEST_ASSERT_FALSE(map_tileset_covers(ts, 102, 201));
    TEST_ASSERT_FALSE(map_tileset_covers(ts, 100, 199));
    TEST_ASSERT_FALSE(map_tileset_covers(ts, 101, 202));
    map_tileset_free(ts);
}

static void test_covers_null_set_is_false(void)
{
    TEST_ASSERT_FALSE(map_tileset_covers(NULL, 100, 200));
}

void RunTests(void)
{
    RUN_TEST(test_parse_valid_tile);
    RUN_TEST(test_parse_bad_magic);
    RUN_TEST(test_parse_too_short_header);
    RUN_TEST(test_parse_truncated_feature_points);
    RUN_TEST(test_parse_zero_features);
    RUN_TEST(test_free_null_is_safe);
    RUN_TEST(test_lonlat_projection);
    RUN_TEST(test_lonlat_roundtrip);
    RUN_TEST(test_load_mem_valid_archive);
    RUN_TEST(test_load_mem_bad_magic);
    RUN_TEST(test_load_mem_too_short);
    RUN_TEST(test_load_mem_index_overruns);
    RUN_TEST(test_load_mem_skips_out_of_range_tile);
    RUN_TEST(test_load_dir_reads_tiles);
    RUN_TEST(test_load_dir_missing_manifest);
    RUN_TEST(test_load_mem_owned_takes_ownership);
    RUN_TEST(test_load_mem_owned_frees_on_bad_archive);
    RUN_TEST(test_load_file_reads_archive);
    RUN_TEST(test_load_file_missing);
    RUN_TEST(test_covers_within_and_outside_bbox);
    RUN_TEST(test_covers_null_set_is_false);
    RUN_TEST(test_open_file_streams_tiles);
    RUN_TEST(test_read_tile_rejects_null_and_nonstreaming);
    RUN_TEST(test_open_file_missing);
}
