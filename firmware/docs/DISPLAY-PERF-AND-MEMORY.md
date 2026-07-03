# Display performance & memory — constraints, rules, and a feature playbook

This board (Waveshare ESP32-P4, 800×800 round MIPI-DSI, RGB565, ESP-IDF v6 /
LVGL 9.4) renders the whole UI in **software** and runs **right at the edge of
internal RAM**. Several days of perf and crash debugging produced a set of
non-obvious rules. Read this before adding or changing anything that draws.

If you only read one thing, read **"The rules"** and **"Feature-development
checklist"** below.

---

## The rules (cheat sheet)

1. **Never run per-frame image transforms** (`lv_image_set_rotation`,
   `lv_image_set_scale`). The transform rasterizer is in PSRAM XIP and is ~20×
   slower than a blit. Bake the variants at boot and swap `lv_image_set_src`.
2. **Bake per-state sprites; swap, don't transform.** One `lv_image` whose
   `src` you swap between pre-rendered buffers. Redraw a buffer only when its
   value changes, never per frame.
3. **Do not build a moving/recolouring element out of many small `lv_obj`s.**
   Recolouring/invalidating ~20 scattered objects in one frame crashes the
   device's triple-partial DSI render path. Render such a thing as **one image**.
4. **Solid-fill rectangles are the only cheap vector primitive.** They use the
   one SRAM-resident blender. `lv_line`, `lv_arc`, `lv_scale`, anti-aliased/
   rounded shapes all rasterize from PSRAM XIP — slow, and only acceptable if
   baked once and never re-rasterized per frame.
5. **Cache every setter, per element.** `*_set_*()` must short-circuit when the
   value is unchanged, and only touch the sub-elements that actually changed
   (see `fuel_arc_set_level`, `speed_display`, the tach label/cursor caches).
6. **Keep the screen background a solid colour.** No gradient/photo background
   image — it forces every dirty rect to re-blit background pixels.
7. **Internal RAM is the binding constraint, not PSRAM.** Big buffers (baked
   sprites, framebuffers) go to PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`.
   Adding `lv_obj`s / task stacks / LVGL working memory eats internal RAM, of
   which there is **very little headroom** (see below).
8. **LVGL log level must stay at ERROR.** INFO/TRACE routes per-draw traces
   through `printf` to the 115200 UART; a burst blocks the render thread for
   hundreds of ms (periodic freeze to ~4 FPS, and the writes can stall when no
   host is draining the port).

---

## The constraints (why the rules exist)

**Software rendering, RGB565, no PPA.** `CONFIG_LV_COLOR_DEPTH=16`. The PPA
hardware accelerator is **off** (`# CONFIG_LV_USE_PPA is not set`) — it caused
horizontal banding on this BSP's tear-avoid triple-buffer setup. So every pixel
is composited by the CPU.

**Only one draw function is in fast memory.** Per `main/linker.lf`, only
`lv_draw_sw_blend_to_rgb565` (the ARGB8888→RGB565 blender) is placed in internal
SRAM. *All other* LVGL draw code (line/arc rasterizers, image transform/scale,
glyph rendering) executes from **PSRAM via XIP**, i.e. cache-miss-bound. The
binutils-2.45 / IDF non-contig-regions bug means we can't just mark more code
`noflash` without risking link failures (see `ble-bringup-bisect.md`). Net
effect: **a solid fill or a plain blit is fast; anything that rasterizes vectors
or transforms per pixel from PSRAM is ~5–20× slower.**

**30 FPS budget.** `CONFIG_LV_DEF_REFR_PERIOD=33` and `ui_update_task` ticks at
33 ms. The render budget per frame is ~33 ms. A healthy ride-screen frame is
~5 ms render / ~10 ms flush; the cap is the refresh period, not render cost.

**Triple-partial DSI.** `tear_avoid_mode = TEAR_AVOID_MODE_TRIPLE_PARTIAL`. The
panel is driven from three PSRAM framebuffers with partial updates. This path
does **not** like many small scattered dirty rects in one frame (see rule 3 and
the fuel-arc war story).

**Internal RAM budget — the binding constraint.** Internal RAM is ~256 KB main
pool (+ smaller pools). After static `.bss/.data` and, at runtime, BLE/
esp-hosted/SDIO + LVGL working memory + task stacks, **free internal RAM is only
tens of KB** — and we have seen it drop to ~6 KB, at which point FreeRTOS task
creation (`vApplicationGet{Idle,Timer}TaskMemory ... pxStackBufferTemp != NULL`)
or any unchecked `malloc` fails and the device crash-loops. PSRAM is the
opposite — ~15 MB+ free. Watch internal RAM, not PSRAM.

One more constraint discovered in Phase 3 (July 2026): on rev<v3
silicon the ~274 KB of SRAM above 0x4ff3afc0 joins the heap only after
both CPUs are scheduling — **before that, every task stack fights for
a ~151 KB pool** (RETENT_RAM+RTCRAM+TCM). esp_hosted's WiFi-sized SDIO
queues used to eat ~88 KB of it, leaving ~2 KB of boot margin; that is
why "one more component" (esp_adc) tipped the boot into an assert
loop. Fixed by `CONFIG_ESP_HOSTED_SDIO_TX/RX_Q_SIZE=6` (BLE HCI needs
nothing more). Regression canary: `CONFIG_VROD_ADC_REPRO`. Full story
in `ble-bringup-bisect.md`.

Internal-RAM levers (and their cost):
- `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` (not 2). Each SW draw thread reserves a
  **32 KB internal** stack (FreeType needs ≥32 KB). One unit frees ~32 KB; with
  the baked-sprite render at ~5 ms, single-threaded drawing still clears 30 FPS.
- `CONFIG_LV_USE_SYSMON` / `LV_USE_PERF_MONITOR` are **off**. They're a debug
  aid (read FreeRTOS idle stats) and cost RAM. If you need an FPS readout,
  enable `LV_USE_PERF_MONITOR_LOG_MODE` (logs to serial, no on-screen overlay —
  the overlay also repaints over the bottom widgets every frame). Turn it back
  off for shipping.
- Do **not** casually lower `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` to push
  allocations to PSRAM — it broke early-boot allocations that must be internal
  and made things worse. The draw-unit lever is the safe one.
- The image cache is **off** (`CONFIG_LV_CACHE_DEF_SIZE=0`). Leave it off — a
  cache lives in internal RAM, and our raw ARGB sprites are used in place.

---

## How the dynamic widgets are built (the safe patterns)

| Element | Pattern | Why |
|---|---|---|
| Tach cursor | 91 pre-rotated+coloured 84×84 ARGB sprites (3° buckets), swap `src` per frame, no runtime rotation | runtime `set_rotation` is PSRAM-bound (~110 ms) |
| Tach zoom labels | 6 labels × 16 pre-scaled ARGB sprites, swap `src` by zoom level, no runtime scale | runtime `set_scale` is PSRAM-bound |
| Tach track/glow/ticks | baked once into 800×800 ARGB at boot, static blit | avoids per-frame arc/scale rasterization |
| Fuel arc ticks | **one** ARGB strip image, redrawn into its buffer only on level change | 21 individual `lv_obj`s crashed the DSI render path |
| Speed digits | per-digit monospace slots; only the changed digit invalidates | a single 144-pt label re-rendered the whole number (alpha-blend) every tick |

Baked sprites live in PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`).
Baking runs at boot on the LVGL worker task during the boot→ride transition and
takes a few seconds total; the bake loops **yield periodically** (`vTaskDelay(1)`
in `cursor_image_init` / `bake_label_zoom_levels`) so the CPU-0 idle task feeds
its watchdog (a >5 s stall there panics).

---

## The debugging playbook (do this FIRST, before flashing)

The crash hunt wasted a lot of flash cycles before switching to these. Use them
in order:

1. **Simulator + AddressSanitizer.** The sim (`firmware/simulator/`) runs the
   real widget code. Build it with sanitizers and run it — catches buffer
   overflows / bad pointers / use-after-free instantly with file:line, no
   hardware:
   ```sh
   cd firmware/simulator
   cmake -B build-asan -S . -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g -O1" \
                            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
   cmake --build build-asan -j8 && ./build-asan/vrod_sim   # let it run the cycle
   ```
   To exercise the **device colour path**, temporarily set `LV_COLOR_DEPTH 16`
   in `simulator/lv_conf.h` (device is RGB565; sim defaults to 32). If the sim
   is clean at both depths, the bug is **device-specific** (hardware render /
   memory), not a logic bug — stop looking for a logic bug.

2. **Is it my change or the baseline?** `git stash push -u`, build + flash the
   last commit, watch. Stable baseline = the regression is in the working tree;
   `git stash pop` and bisect.

3. **Bisect on device by disabling segments**, not by guessing. Early-return a
   widget's `*_set_*`, or comment a single `*_update` call in
   `screen_ride_update`, flash, watch the reboot/panic count. This is how the
   fuel arc was isolated.

4. **Get the panic reason via `idf.py monitor`** (it decodes asserts and panics
   with the matching ELF). It needs a TTY, so capture through `script`:
   ```sh
   script -q /tmp/mon.log timeout 40 idf.py -p <PORT> monitor
   ```
   Caveats learned the hard way: **UART coredump capture byte-drops** (invalid
   base64) and **coredump-to-flash read is disrupted by the crash-loop**
   (esptool can't read while the device resets). Don't sink time into coredump
   on this board — prefer the monitor + segment-bisection.

5. **Watch internal RAM** if you suspect OOM. Temporarily log
   `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` and
   `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` from `ui_update_task`
   and at boot milestones. A steadily-falling number = leak; a low floor (<~15 KB)
   = you're over budget.

6. **Find the expensive redraws** with the simulator's invalidation tooling
   (below) — dirty-rect area and count transfer directly to the device's
   partial-refresh cost, even though absolute FPS does not.

### Simulator tooling (sim-only, in `simulator/main.c`)

- `VROD_PERF=<frames> ./build/vrod_sim` — per-frame render-time percentiles +
  per-frame dirty-rect area + rects/frame over a driving cycle. The dirty-area
  numbers are the transferable signal for device render cost.
- `VROD_INVLOG=1 ./build/vrod_sim` — logs every invalidated area (`[inv] WxH @
  (x,y)`); rank by area to find the widget driving cost.
- `VROD_SHOT=/path.png VROD_SHOT_FRAMES=N ./build/vrod_sim` — render N frames,
  dump a PNG, exit. Use this to eyeball layout/colour changes without hardware.

---

## War stories (root causes, for context)

- **"~7 FPS in sections."** Runtime image transforms (cursor rotation + label
  zoom) hit the PSRAM transform rasterizer → 100–155 ms render during sweeps.
  Fixed by baking sprites and swapping `src`.
- **"Periodic freeze to ~4 FPS."** LVGL was logging at INFO via `printf` to the
  115200 UART; bursts blocked the render thread. Fixed: `LV_LOG_LEVEL_ERROR`.
- **The `600×600 @ (100,100)` invalidation was a red herring** — it fired ~2/sec
  but was a cheap mostly-black fill; it kept firing while FPS was a solid 30.
  Don't chase invalidations by *size* alone; correlate with render time.
- **The crash loop** was two independent internal-RAM-class bugs: OOM asserts
  (fixed by 1 draw unit freeing 32 KB) and an instruction-access-fault from
  recolouring 21 `lv_obj` fuel ticks through the DSI partial-render path (fixed
  by rendering the ticks as one image). The simulator + ASan ran clean, which is
  what proved it was device-specific and ended the guessing.

---

## Feature-development checklist

Before you write a new widget or change a drawing one:

- [ ] Is anything updated **per frame**? If so it must be a **blit or solid
      fill** — no transforms, no vector rasterization, no many-object recolour.
- [ ] Does a value have a small set of states? **Bake the variants** at boot
      (to PSRAM) and swap `src`. Redraw a buffer only on change.
- [ ] Does the widget have **many sub-elements** that change together? Render it
      as **one image**, not N `lv_obj`s.
- [ ] Did you add a **cache** to every `*_set_*()` so unchanged frames (and
      unchanged sub-elements) do no work?
- [ ] Did you add **internal-RAM cost** (new `lv_obj`s, a task, an internal
      buffer)? Budget is tiny — put big buffers in PSRAM and re-check free
      internal RAM on device.
- [ ] **Verify in the simulator first** (`VROD_SHOT` for looks, `VROD_PERF` for
      cost, ASan for memory). Only then flash.
- [ ] After flashing, watch for **reboots/panics for at least 60–90 s** through
      a full driving cycle — the device's failures are often delayed and
      intermittent (a fuel change 10 s in, a sweep, a gear shift).
- [ ] Keep `LV_USE_PERF_MONITOR` / `SYSMON` **off** in committed builds; if you
      enabled coredump or heap logging to debug, **strip it** before committing.
