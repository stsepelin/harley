# V-Rod cluster — working notes

Digital instrument cluster for a 2009 Harley-Davidson V-Rod. Waveshare
ESP32-P4-WIFI6-Touch-LCD-3.4C, 800×800 round MIPI-DSI panel.
ESP-IDF v6.0.1 / LVGL 9.4 / GCC 15 (RISC-V).

Background:

- Cross-system docs at the monorepo root:
  - `../docs/PROJECT-BRIEF.md` — what we're building
  - `../docs/00-MASTER-PROJECT-PLAN.md` — phases / roadmap
  - `../docs/03-PHASE3-J1850-PLAN.md` — current phase
- Firmware-internal docs (here in `firmware/docs/`):
  - `docs/01-PHASE2-DISPLAY-PLAN.md` — Phase 2 plan (complete)
  - `docs/ARCHITECTURE.md` — threading, render pipeline, decision history
  - `docs/DISPLAY-PERF-AND-MEMORY.md` — **read before touching anything that
    draws.** Render/RAM constraints, the bake-don't-transform rules, the
    internal-RAM budget, the debug playbook (sim+ASan first), and a
    feature-development checklist.
  - `docs/ble-bringup-bisect.md` — BLE link-trap resolution notes
  - `docs/j1850-toggling-isr-candidate.md` — design note (not
    implemented) for a future RX ISR that infers the pulse level by
    toggling instead of reading the pin, so a glitch filter could
    coexist. Read before touching the sniffer edge capture.
  - `docs/bike-power-injection.md` — protected 12V→5V power chain
    (fuse / reverse-polarity / load-dump TVS / mini560 / output
    reverse-block) into the header 5V; parts list + bench test.
    Prerequisite for powering the cluster on the bike.

This file is for *how to work in the firmware code*, not what we're
working toward or why each decision is the way it is. Path references
below are relative to this `firmware/` directory.

## Build & run

```sh
# Firmware
. $IDF_PATH/export.sh
idf.py build flash monitor

# Host unit tests (clang/gcc + lcov; macOS: brew install lcov)
cd test_apps/host
cmake -B build -S . && cmake --build build && ctest --test-dir build
./coverage.sh                # full lcov report, opens HTML

# Desktop simulator (macOS: brew install sdl2)
cd simulator
cmake -B build -S . && cmake --build build && ./build/vrod_sim
```

CI: `host-tests.yml` (Unity + 100 % line/branch gate on the policy scope),
`firmware-build.yml` (`espressif/idf:v6.0.1` container), and `lint.yml`
(clang-format + hygiene hooks via pre-commit).

## Pre-commit hooks

```sh
pipx install pre-commit          # one-shot, host-wide
pre-commit install               # in the repo root, once per clone
```

Runs clang-format on staged C / H under `firmware/`, the standard
whitespace / EOF / yaml / merge-conflict checks, and the host unit
tests when something in the tested scope changes. Config lives at
`.pre-commit-config.yaml` at the repo root.

## Code style

- **No comments that restate the code.** A comment earns its keep by
  explaining *why* — non-obvious constraint, workaround, calibration that
  came out of an experiment. If removing it wouldn't confuse a reader,
  delete it.
- **No emoji** anywhere — code, commits, UI strings.
- **Caches everywhere.** Every widget's `*_set_*()` short-circuits when
  the input matches the previous frame:

  ```c
  if (sd->has_value && sd->last_x == x) return;
  sd->last_x = x;
  sd->has_value = true;
  ```

  Tested in `test_widget_caches.c`. Adding a setter without the cache
  silently regresses UI FPS.
- **V-Rod palette in `main/display/theme.h`** — never hex-literal a colour
  in a widget. Add a name to the palette if you need a new shade.
- **JetBrains Mono Bold** for numeric readouts (tabular digits), MDI font
  for icons. Boot screen text-fallback also uses JBM Bold — Montserrat is
  intentionally off (only the 14-pt size stays, as LVGL's required default).
- **Pure logic separated from LVGL.** New math / formatting / state-machine
  code goes in its own free-function module under `main/`, so it can be
  tested on host. That's how `gear_table`, `sim_math`, `format`, `units`,
  `smooth`, `settings` ended up as their own files.

## How things fit together

One-liners — see `docs/ARCHITECTURE.md` for the why.

- `vehicle_data` is the mutex-guarded latest-value store between
  producer (sim, J1850 later) and UI.
- Sim + event watcher + audio on **core 0**; LVGL + `ui_update_task`
  on **core 1**. Don't bleed UI/render work onto core 0 or input/audio
  work onto core 1.
- Tach uses pre-baked ARGB8888 sprites in PSRAM (glow, cursor, track
  ring, scale ticks). Per-frame cost is a blit, not a redraw.
- Upshift warning blinks the **gear digit** (not the tach), driven by
  `gear_indicator_set_warning`. The tach was tried and abandoned — see
  the architecture doc for the four-iteration story.
- `smooth_step(cur, target)` is the canonical "ease toward a target"
  helper (25 % per call + ±1 snap).
- Settings: pure `settings.c` (host-tested) + `settings_store.c` (NVS).
  Apply runs inside the LVGL lock, which serialises reads/writes.

## Patches & gotchas (operational)

- **`AnimatedGIF.h` MAX_WIDTH 480 → 800.** LVGL's bundled GIF decoder
  hard-caps width at 480; our boot GIF is 800. Patched at CMake configure
  time in `main/CMakeLists.txt` — idempotent, re-applies after a fresh
  `managed_components/` fetch. Don't manually edit the patched file. The
  path in the CMake patch must match the current LVGL layout
  (`src/libs/gif/AnimatedGIF.h` — *not* the older `gif/AnimatedGIF/src/`).
- **PPA is off** (`CONFIG_LV_USE_PPA` unset, `CONFIG_LV_DRAW_BUF_ALIGN=4`).
  Caused horizontal banding on this BSP. Re-enable only with a
  known-good alignment story.
- **GT911 touch** sometimes fails on cold boot; the BSP retries with a
  100 ms delay. First-boot serial logs aren't always clean.
- **LVGL press events don't reach the ride screen** on this BSP — hover
  does but `LV_EVENT_PRESSED` / `_LONG_PRESSED` never fire. We poll
  `lv_indev_get_state()` from `event_watcher_task` instead.
- **Backlight stays at duty 0 through init**, then `_brightness_set()`
  fires once after the boot screen is painted. `_backlight_on()` is
  literally `_brightness_set(100)` — every brightness call lights the
  panel. Lighting it before LVGL has painted shows whatever PSRAM came
  up with (≈ white in RGB565), as a flash. See `main/main.c`'s
  `app_main()` boot sequence and `docs/ble-bringup-bisect.md`. Below
  ~25 % duty the LCD goes fully black, so `SETTINGS_BRIGHTNESS_MIN = 30`
  in `settings.h` clamps loads.
- **Don't try Lottie again** for the boot screen unless ThorVG's
  rasteriser gets much faster.
- **`jbm_bold_26.c` / `jbm_bold_33.c` have a manually-stripped `const`** on
  the `lv_font_t` struct definition (not on the big bitmap/dsc tables —
  those stay in flash). `emoji_font.c` writes `.fallback` on those two
  structs at boot to attach the monochrome Noto Emoji FreeType font;
  the P4 maps `.rodata` to flash (XIP), so the struct has to live in
  `.data`. If you regenerate either font via `lv_font_conv` (see the
  `Opts:` line in the file header), re-apply the strip on the
  `lv_font_t jbm_bold_XX = {` line so the assignment doesn't fault at
  boot. The other JBM sizes (45, 72, 144) and the MDI sizes keep their
  `const` — they're numeric/icon fonts with no emoji to fall back to.
- **Emoji are monochrome, not color**, even though FreeType supports
  COLR/sbix in principle. LVGL's `lv_freetype_glyph.c::freetype_glyph_create_cb`
  uses `FT_LOAD_COMPUTE_METRICS` without `FT_LOAD_COLOR`, which on
  Twemoji / Noto-COLRv1 returns metrics.width = 0 / height = 0 (their
  glyf outlines are empty placeholders, the real visual lives in COLR
  paint tables). The label then allocates a 0×0 slot and draws
  nothing. Mono fonts have real outlines, so metrics work and the
  glyph renders as an alpha mask tinted by `text_color`. Patching
  LVGL to honour `FT_LOAD_COLOR` during metrics would unlock color
  emoji — own piece of work, deserves an upstream bug.

## Testing

Full policy in `test_apps/host/README.md`. Short version:

- **In scope (100 % line + branch required):** the pure-logic modules —
  `gear_table.c`, `sim_math.c`, `format.c`, `gesture.c`, `units.c`,
  `smooth.c`, `fuel_scale.c`, `phone_data.c`, `phone_protocol.c`,
  `settings.c`, `vehicle_data.c`, `ble_visibility.c`, `j1850_vpw.c`,
  `j1850_parse.c`, `j1850_driver.c`, `j1850_edge.c`, `j1850_tx_logic.c`,
  `ride_log_format.c` — **plus all the widgets** (including `tach_arc.c`),
  which run against the LVGL stub and sit inside the same gate.
- **Out of scope:** fonts (generated), boot/screen wiring (BSP glue),
  `sim_engine.c`'s task body, and the `j1850_sniffer.c` / `j1850_tx.c` /
  `ride_log.c` producers and capture/driver glue.

When you add code:

- **New pure-logic module** → add a test, add the `.c` to `vrod_pure`
  in `test_apps/host/CMakeLists.txt`, add it to the `--extract` filter
  in `.github/workflows/host-tests.yml` and `coverage.sh`, add a row
  to the policy table in `test_apps/host/README.md`.
- **New widget setter** → add the cache short-circuit + a regression
  test in `test_widget_caches.c`.
- **New widget** → if label-based, add to `vrod_widgets` + cache test.
  If arc/scale/image-heavy, mark out-of-scope.

## When in doubt

- **Adding a managed component** — check `memory/feedback_ecosystem_compat.md`
  first; we burned time on a Lottie/ThorVG dead end by not researching
  upstream support before recommending a downgrade.
- **Editing `managed_components/`** — usually wrong. Prefer a CMake
  configure-time patch in `main/CMakeLists.txt` so it survives a fresh
  fetch.
- **Anything visual on the round display** — verify in the simulator
  before flashing. It runs the real widget code; if it looks right
  there it looks right on device.
