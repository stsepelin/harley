# V-Rod cluster — working notes

Digital instrument cluster for a 2009 Harley-Davidson V-Rod. Waveshare
ESP32-P4-WIFI6-Touch-LCD-3.4C, 800×800 round MIPI-DSI panel.
ESP-IDF v6.0.1 / LVGL 9.4 / GCC 15 (RISC-V).

For project brief, build plan, and roadmap see:

- `docs/PROJECT-BRIEF.md`
- `docs/00-MASTER-PROJECT-PLAN.md`
- `docs/01-PHASE2-DISPLAY-PLAN.md`

This file is for *how to work in the code*, not what we're working toward.

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

CI: `host-tests.yml` (Unity + 100 % line/branch gate on the policy scope)
and `firmware-build.yml` (`espressif/idf:v6.0.1` container).

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
  for icons. Don't reach for Montserrat unless you're on the boot screen.
- **Pure logic separated from LVGL.** New math / formatting / state-machine
  code goes in its own free-function module under `main/`, so it can be
  tested on host. That's how `gear_table`, `sim_math`, `format`, `smooth`
  ended up as their own files.

## Architecture you should know

- **`vehicle_data`** is the single shared latest-value store, mutex-guarded.
  Sim writes, `ui_update_task` reads at ~30 FPS. No direct widget-to-sim
  wiring.
- **Core pinning**: sim task on **core 0** at prio 8; LVGL pthreads on
  **core 1** via `CONFIG_PTHREAD_DEFAULT_CORE_1=y`. Don't let UI work
  bleed onto core 0.
- **Tach uses pre-baked ARGB8888 sprites** (Gaussian glow ring, cursor
  pill) in PSRAM rather than stacked semi-transparent arcs. Cleaner
  gradient, much smaller per-frame cost.
- **`smooth_step(cur, target)`** moves 25 % per call with a ±1 snap when
  within a few ticks. Same pattern works for any UI value that should
  ease toward a target.
- **Info-slot rotation** in `screen_ride_update` cycles
  clock → odo → trip1 → trip2 every ~5 s. Only the *visible* widget gets
  updated; `HIDDEN` flags toggle only on mode change.
- **Boot animation** is an embedded GIF (`main/assets/boot.gif`), 800×800
  RGB565, blit via PPA. Hand-off on `LV_EVENT_READY` with a safety timer.
  Lottie/ThorVG was tried and abandoned — vector rasterisation at 800×800
  isn't real-time on the P4.

## Patches & gotchas

- **`AnimatedGIF.h` MAX_WIDTH 480 → 1024.** LVGL's bundled GIF decoder
  hard-caps width at 480, our boot GIF is 800. Patched at CMake configure
  time in `main/CMakeLists.txt` — idempotent, re-applies after a fresh
  `managed_components/` fetch. Don't manually edit the patched file.
- **PPA buffer alignment** must be a multiple of the L1 cache line (64).
  `CONFIG_LV_DRAW_BUF_ALIGN=64` + `CONFIG_LV_ATTRIBUTE_MEM_ALIGN_SIZE=64`.
  LVGL's `esp.cmake` uses `PRIV_REQUIRES` for `esp_driver_ppa` which
  doesn't expose include paths — `main/CMakeLists.txt` links it
  explicitly with `target_link_libraries(${LVGL_LIB} PRIVATE
  idf::esp_driver_ppa idf::esp_mm)`.
- **GT911 touch** sometimes fails on cold boot; the BSP retries with a
  100 ms delay. First-boot serial logs aren't always clean.
- **Don't try Lottie again** for the boot screen unless ThorVG learns to
  rasterise much faster, or you're rendering at ≤200×200. We've measured.

## Testing

Full policy in `test_apps/host/README.md`. Short version:

- **In scope (100 % line + branch required):** pure-logic modules —
  `gear_table.c`, `sim_math.c`, `format.c`, `smooth.c`, `vehicle_data.c`.
- **Behaviour-checked, not coverage-measured:** every label-based widget
  has one cache-regression test via the LVGL stub.
- **Out of scope:** fonts (generated), boot/screen wiring (BSP glue),
  `tach_arc.c` (LVGL stub would double in size for marginal value).

When you add code:

- **New pure-logic module** → add a test, add the .c to `vrod_pure` in
  `test_apps/host/CMakeLists.txt`, add it to the `--extract` filter in
  `.github/workflows/host-tests.yml` and `coverage.sh`, add a row to the
  policy table in `test_apps/host/README.md`.
- **New widget setter** → add the cache short-circuit, add a regression
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
  before flashing. It runs the real widget code; if it looks right there
  it looks right on device.
