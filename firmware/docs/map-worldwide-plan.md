# Worldwide map: GPS-paged cell tiles

> **Status: Stages 1-4 done (seam, cell math, format + tooling, cell manager);
> index halved as a down-payment.** The paged cell grid works end-to-end in the
> sim on a real Tallinn bake and the device builds with it. Next: Stage 5 - the
> Europe bake to the 58 GB card + the on-bike paging check (task #58). Goal: put a
> whole
> continent (Europe first, world later) on the SD card and keep only the tiles
> near the rider's GPS resident, paged in and out as you move. Builds directly on
> the streaming SD loader (task #60) and the SD-backed real map (task #57 / PR #34).
>
> **Done so far (map-bring-up, July 2026):** the `map_source` seam (Stage 1); and
> the resident index was cut from ~28 B to **16 B per tile** (a compact `map_sidx_t`
> for the streaming path — `map_tile.c`), so the full-Estonia index (243,767 tiles)
> dropped from ~6.8 MB to ~3.9 MB PSRAM. That's the same lever the cell-paging
> plan needs, and it fixed the on-device symptom below (dark tiles + choppy
> rotation were both PSRAM starvation once the map ran on a real fix).

## Motivation

Today a real map build (`CONFIG_VROD_MAP_SD`) reads **one** `.zmta` archive whose
**entire tile index** is held in PSRAM (`map_tileset_open_file`). Tile *geometry*
is already streamed on demand off the card with an LRU cache — that part scales.
The **index** does not: it is `~28 B` per tile resident (`sizeof(map_tile_t)`),
so a country costs single-digit MB but a continent costs hundreds of MB to ~1 GB,
which will not fit the P4's 32 MB PSRAM. A single archive also can't exceed
**4 GB** — `pack.py`/the ZMTA index use a `u32` file offset.

The rider only ever sees a few km around their position. So we page the index by
GPS the same way geometry is already paged: keep the tiles near you resident,
drop the rest.

## What exists vs. what's missing

| Piece | State |
|---|---|
| Per-tile geometry read off SD on demand (`map_tileset_read_tile`, fseek+parse) | done |
| Recently-seen tile LRU cache (`screen_map.c`) | done |
| Whole tile index resident in RAM (`map_tileset_open_file`) | **the gap** |
| Single archive ≤ 4 GB (`u32` offsets in ZMTA) | **caps continent size** |
| Build clips one bbox → one archive (`region.py`) | needs a continent-scale variant |

## Sizing (measured, not guessed)

Two real anchors baked from `europe-latest.osm.pbf` (roads + water, z16):

| Region | Tiles | Archive | Resident index (~28 B/tile) |
|---|---|---|---|
| Tallinn r25 km (scratch bake) | 10,489 | 2.28 MB | 0.29 MB |
| Estonia (current card `map.zmta`) | 243,767 | 28 MB | 6.8 MB |

Estonia (45k km²) → 244k tiles. Europe land is ~10M km² and denser on average, so
**Europe ≈ 10–20 GB / ~50–120M tiles** — the archive **fits a 58 GB card**, but
its index (~1 GB whole) must be paged. **World** at uniform z16 roads+water likely
exceeds 58 GB and wants a bigger card or a lower zoom outside home regions; Europe
is the v1 target and the design is world-ready.

## Architecture decision: a grid of per-cell ZMTA files

Two ways to page the index:

1. **One big archive** with 64-bit offsets + a coarse spatial directory, and a
   windowed index reader. Cleanest at runtime (one open file), but needs a new
   archive format *and* an external-sort pack over 10–20 GB (can't sort in RAM).
2. **A grid of small per-cell ZMTA files** (e.g. 1°×1°), the loader keeping only
   the cells within a GPS radius open. Each cell is well under 4 GB, so it
   **reuses the existing ZMTA format and `map_tileset_*` code almost verbatim**;
   the build is a single streaming pass bucketing into cells — no giant sort.

**Chosen: option 2 (cell grid).** Lowest format risk, reuses validated loader
code, and the build scales without holding the continent in RAM. Option 1 is the
future move only if we go world-scale on one file or want to drop the multi-file
bookkeeping; noted as the alternative, not v1.

## On-card layout

```
/sdcard/map/
  world.hdr            # manifest: zoom, cell size, present-cell set, bbox
  N59/E024.zmt         # one ZMTA archive per cell; dir = lat cell, file = lon cell
  N59/E025.zmt         # (subdir keeps any one directory small for FAT)
  S34/W001.zmt         # letter = hemisphere of the cell INDEX (not the degree)
  ...
```

- **Path from a cell index** (the runtime rebuilds it, no directory scan):
  `<dir>/<'N'|'S'><|lat|>/<'E'|'W'><|lon|>.zmt`, where `(lat,lon)` is the integer
  cell index (SW corner = `idx * cell_size` in 1/256°, per `map_cells.c`). At the
  1° default the index equals the degree, so `N59/E024` reads naturally; at 0.5°
  it is a half-degree index (`N118/E049`). The sign letter carries S/W.
- `world.hdr` — little-endian (see `map/map_world.h` for the authoritative byte
  layout): magic `ZMTW`, version, zoom (u16), cell-size in 1/256° (u16), cell-index
  bbox, then a **present-cell set** (sorted `(lat16,lon16)` pairs, a few KB for
  Europe) so the loader knows coverage without statting the card. This is the only
  always-resident metadata (~KB). Written by `world_hdr.py`, read by
  `map_world_parse`.
- Each `.zmt` is a normal ZMTA (same bytes `pack.py` writes today), covering one
  cell's tiles. `u32` offsets are fine — a 1° cell is at most tens of MB.
- Cell size is a build parameter. **1° default** (≈ up to ~180 tiles/side at z16;
  a dense-city 1° cell ≈ 30–60k tiles ≈ ~1–1.7 MB index). Drop to **0.5° near
  metros** if a worst-case cell index gets too big; the loader reads the size
  from `world.hdr`, so it is not hard-coded.

## Firmware design

Introduce a thin **`map_source`** seam so the render path doesn't care whether it
is reading a single archive or a paged cell grid:

```
map_source_t                       // opaque
map_source_open_single(path)       // wraps one map_tileset_t (demo / small card)
map_source_open_cells(dir)         // reads world.hdr, manages a cell working set
bool map_source_read_tile(src, tx, ty, out)
bool map_source_covers(src, tx, ty)
int  map_source_zoom(src)
void map_source_set_center(src, tx, ty, heading)   // hint for paging/prefetch
```

`screen_map_create` and `map_sd.c`'s `anim_task` switch from `map_tileset_t *` to
`map_source_t *`; the LRU tile cache and rasteriser are untouched.

**Cell manager (the new logic):**

- Keep a small **working set of open cells** — the cell under the rider plus its 8
  neighbours (3×3), each an already-validated `map_tileset_t` (index resident,
  `FILE*` open). Resident cost = up to 9 cell indexes (a few MB) + 9 `FILE*`.
- `map_source_read_tile(tx,ty)` maps the tile to its cell (`tx,ty → lon,lat → cell`),
  and routes to that cell's `map_tileset_read_tile`. A tile whose cell isn't open
  (edge of the working set) simply returns "absent" that frame — same contract as
  a missing tile today.
- **Re-page on movement:** when `map_source_set_center` shows the rider crossed
  into a new cell, evict cells no longer in the 3×3 (LRU) and open the new ones.
- **Predictive prefetch:** use `heading` to open the *next* cell in the direction
  of travel before the rider reaches it, so the fopen + index read (the one bigger
  SD read this design adds) is off the critical path. Do all opens on a **loader
  task**, never under `bsp_display_lock` or in the render frame.
- **FAT `max_files`:** raise from 4 (`map_sd.c` mount cfg) to ~12 to allow the
  working-set `FILE*`s plus the ride log. Verify RAM cost of the FAT file cache.

**Coverage / "off area":** `map_source_covers` checks the present-cell set in
`world.hdr` (rider is over a baked cell) — replaces `map_tileset_covers`'s single
bbox, so "no coverage" still lights correctly at the edge of the baked world.

## Build pipeline

New `tools/maptiles/world.py`, reusing `styles.py` + the ZMT0/ZMTA writers:

1. **Filter once.** `osmium tags-filter europe-latest.osm.pbf w/highway w/waterway
   n/natural=water a/natural=water a/water a/building -o europe_rw.pbf` — roads +
   water + building footprints, shrinks 32 GB → a few GB. **Buildings ~3x the tile
   bytes** (a dense-city 0.1° cell went 0.1→0.3-0.9 MB), so a whole-Europe bake with
   buildings could exceed a 58 GB card — `world.py --no-buildings` drops them for a
   continent-scale run while home regions keep them.
2. **Single-pass multi-extract into cells.** Generate an `osmium extract` config
   listing every 1° cell bbox and run it **once** (`osmium extract -c cells.json
   europe_rw.pbf`) — one read of the input produces all per-cell pbfs. (Per-cell
   `osmium extract` in a loop would re-read the whole input per cell — days;
   rejected.)
3. **Bake each cell** with the existing `bake.py` logic (bounded RAM: one cell at
   a time) → a `<lat>/<lon>.zmt`, then delete the cell pbf.
4. **Write `world.hdr`** from the set of cells that produced tiles.

**Disk discipline (the Mac has ~9.8 GB free):** write `europe_rw.pbf`, the cell
pbfs, and the output tree **to the 58 GB SD card / an external disk**, and delete
each cell pbf right after baking it, so peak scratch ≈ the filtered pbf, not the
GeoJSON explosion. Never stage the whole continent as GeoJSON on the Mac.

## Config

- New Kconfig choice: map source = `SINGLE` (today, `CONFIG_VROD_MAP_SD_PATH`) or
  `CELLS` (`CONFIG_VROD_MAP_SD_DIR`, default `/sdcard/map`). Demo build
  (`CONFIG_VROD_MAP_DEMO`) stays single-archive from flash.
- `world.hdr` carries zoom + cell size, so firmware isn't rebuilt to change them.

## Testing

- **Pure, host-tested (100 % gate):** the cell math (`tile↔cell`, working-set
  membership, LRU eviction order, re-page decision, prefetch-direction pick) goes
  in a free-function module (`map_cells.c`) with no LVGL/SD deps — same discipline
  as `gear_table`/`smooth`. Fixture: a fake present-cell set + a synthetic GPS
  track that crosses cell borders; assert the open/evict/prefetch sequence.
- **Format round-trip:** `world.hdr` writer (Python) ↔ reader (C) parity test,
  mirroring the existing ZMT0/ZMTA fixture tests (`test_map_tile.c`).
- **Sim:** `VROD_MAP_CELLS=<dir>` runs the real cell manager against a baked cell
  tree on the desktop, so paging is exercised without flashing — like
  `VROD_MAP_ZMTA` today.
- **On device:** a Tallinn→Tartu run confirming cells page without a hitch at
  road speed (folds into the on-bike verification, task #58).

## Rollout (each stage independently verifiable)

1. **`map_source` seam** — DONE (July 2026). Single archive wrapped behind the
   `map_source_*` API (`map/map_source.c`); `screen_map`/`map_sd`/`map_demo` and
   the sim all go through it; no behaviour change, host + device verified. The
   16-B streaming index (`map_sidx_t`) also landed here.
2. **`map_cells.c` pure logic** — cell math + working-set/LRU/prefetch, host-tested
   to 100 %. No SD yet.
3. **`world.py` build + `world.hdr` format** — DONE (July 2026, format + tooling).
   `world.hdr` layout defined and implemented both ways: the C reader
   (`map/map_world.c`, `map_world_parse`/`covers`/`load_file`) and the Python
   writer (`tools/maptiles/world_hdr.py`), with a byte-level round-trip regression
   (`test_map_world`, out of the branch gate like `map_tile`) and a writer/reader
   parity check. `tools/maptiles/world.py` bakes an OSM extract into the cell grid
   (`<lat>/<lon>.zmt` + `world.hdr`) with one filter pass + batched single-pass
   multi-extract. Still to run: an actual Baltics bake to a card dir + sim preview
   (folds into Stage 4 bring-up, which needs the cell manager to read the tree).
4. **Cell manager wiring** — DONE (July 2026). `map_source_open_cells` reads
   `world.hdr` and keeps a 3x3 working set of open per-cell archives that follows
   the rider: `map_source_set_center` maps the view centre to a cell, evicts cells
   outside the window, and opens at most one missing cell per frame (bounded SD
   work), biased to the centre then the heading-ahead cell. `map_source_render_tile`
   / `covers` route a tile to its cell via the new `map_tilef_to_lonlat` inverse
   projection; `map_world_covers` still answers "off area" from the baked set.
   Kconfig `VROD_MAP_SD_CELLS` + `VROD_MAP_SD_DIR` pick the paged path; `max_files`
   raised 6 -> 14 for the 9 cell FILE*s + ride log. Verified in the sim
   (`VROD_MAP_CELLS=<dir>`) against a real 8-cell Tallinn bake - pages across cell
   borders and renders correctly - and the device firmware builds with the cell
   config. **Deferred:** a dedicated loader task; the one-open-per-frame lazy fill
   over the 3x3 window keeps opens off the render critical path in practice (worst
   case one `fopen`+index-read per frame, and the neighbour cells are long since
   open by the time a border is reached), so the task is a later optimisation, not
   a correctness need. On-bike paging check folds into Stage 5 / task #58.
5. **Europe bake** — run the full pipeline to the 58 GB card; record real
   size/tile-count/index-RAM; on-bike Tallinn→Tartu paging check (task #58).
6. **(Later) world / mixed-zoom** — lower zoom outside home regions, or migrate to
   the single 64-bit archive if one-file management is preferred.

## Risks / open questions

- **SD random-read latency at speed.** Per-tile reads already work at 15 fps for
  Estonia; the new cost is the per-cell index read on a border crossing. Mitigated
  by heading prefetch on the loader task; measure worst-case fopen+index-read and
  confirm it never stalls a frame.
- **Worst-case cell index size** (dense metro 1° cell). Bound it by dropping those
  cells to 0.5°; `world.hdr` cell size makes this data-driven.
- **FAT `max_files` / file-cache RAM** with ~12 open files — measure.
- **Card wear / mount sharing** with the ride log (slot 0, already shared) — no new
  slot, but more open files; validate against the ride-log mount.
- **Effort:** medium. Stages 1–4 are the bulk (a refactor + one pure module + one
  build script + the manager); the format reuse keeps it from being a rewrite.
