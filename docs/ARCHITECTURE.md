# V-Rod cluster — architecture

How the firmware fits together, and *why* each non-obvious choice is the
way it is. CLAUDE.md is the day-to-day cheat sheet; this file is the
context behind it.

## Threading model

Two cores on the ESP32-P4. Pinning is deliberate.

| Task                  | Core | Prio | Purpose                                              |
|-----------------------|:----:|:----:|------------------------------------------------------|
| `sim_task`            |  0   |  8   | Synthetic driving cycle → `vehicle_data`             |
| `event_watcher_task`  |  0   |  5   | Polls indev + `vehicle_data` at 100 Hz for events    |
| `audio_task`          |  0   |  3   | Pulls click events from queue, writes to I2S codec   |
| `ui_update_task`      |  1   |  4   | 30 FPS widget setter sweep                           |
| LVGL render pthread   |  1   |  4   | Composes dirty regions, flushes to DSI               |

**Why event detection isn't in `ui_update_task`.** The UI task takes
`bsp_display_lock(-1)` before calling widget setters. LVGL's render
thread holds the same lock during compose. On a heavy frame (cursor +
shift-light + turn arrows all redrawing) the UI task waits on that lock,
which means turn-signal-edge detection and long-press timing stop being
sampled — inputs feel sluggish. `event_watcher_task` polls
`lv_indev_get_state()` and `vehicle_data` directly at 100 Hz on the
sim's core, where there's nothing to compete with, and only grabs the
LVGL lock briefly to call `ui_manager_show_settings` when a long-press
fires.

**Why audio is on core 0, not core 1.** Earlier the audio task lived on
core 1 with LVGL. `esp_codec_dev_write` blocks for ~30 ms while feeding
DMA; whenever LVGL was rendering a heavy frame at the same time, audio
queueing latency jumped and turn-signal clicks felt off-sync with the
arrows. Moving the audio task to core 0 (where the sim sleeps 45 of
every 50 ms) gives the codec writes a quiet neighbour and the clicks
stay locked to the visual.

## Shared state

- **`vehicle_data`** is the single source of truth between the producer
  (sim today, J1850 driver tomorrow) and the UI. Mutex-guarded. Sim
  writes the whole struct under the lock; consumers `vehicle_data_get`
  a snapshot.
- **`settings_store`** (NVS) loads at boot and exposes a stable pointer
  via `settings_store_current()`. Mutations happen inside settings-screen
  event callbacks, which run under `bsp_display_lock` — that serialises
  them against `ui_update_task`'s reads. No extra mutex needed.

## Render pipeline (the tach is the interesting case)

Everything that's static is pre-baked into ARGB8888 sprites in PSRAM
once at boot. Per-frame work is reduced to memcpy-with-alpha blits.

| Sprite              | Size    | What it is                                          |
|---------------------|---------|-----------------------------------------------------|
| Tail glow ring      | 800×800 | Gaussian halo, orange in the normal sweep + red in  |
|                     |         | the redline sweep                                   |
| Cursor (normal)     | 32×72   | Pill-shaped bar with perpendicular Gaussian glow    |
| Cursor (redline)    | 32×72   | Tighter sigma + red palette, swapped past redline   |
| Tach track ring     | 800×800 | 5-pixel gray rail at the bezel (was an `lv_arc`)    |
| Tach scale ticks    | 800×800 | 21 ticks at correct angles + redline coloring (was  |
|                     |         | an `lv_scale` with section styles)                  |

Total PSRAM at runtime: ~14 MB / 31 MB (45%).

**Why bake the track + ticks.** The original `lv_arc` track and
`lv_scale` widget composed pixels per dirty area. Every cursor-movement
frame had to re-walk the arc path and re-draw 21 tick line segments in
the dirty rect. Replacing both with ARGB sprites makes the per-frame
cost a memory blit; cursor frames dropped noticeably on the device.

**Why labels are still `lv_label` widgets.** The labels need
transform-scale animation as the cursor sweeps past them. Per-label
scale is cached so labels whose scale didn't change skip their style
set (during a hard RPM transient that's 4 of 6 labels saved per frame).

## Upshift warning — the long story

The warning has been through several iterations. Recording them here so
nobody re-tries an approach we already discarded.

1. **5 Hz blinking full-screen ring** (`lv_obj` with circular border).
   Each visibility toggle invalidated the entire 800×800 framebuffer.
   At 10 invalidates/sec the triple-buffer copy stalled the audio +
   tach pipeline; visible stutter.
2. **Solid full-screen ring**. Only invalidated twice per WOT pull, but
   the ring's bounding rect IS the screen, so any other widget that
   overlapped it (cursor at the bezel, etc.) caused per-frame
   ring-pixel recompose.
3. **`lv_arc` with the same sweep as the tach**. Same cost story as the
   ring; visual was nicer but performance unchanged.
4. **Line-arc colour fade + glow-opacity fade**. Each LVGL refresh
   during the 150 ms fade invalidated the line arc's 800×800 bounding
   rect — 10× full-tach redraws crammed into the worst possible 150 ms,
   right when the cursor was moving fastest. Laggy.
5. **2 Hz blink of the line + glow swap**. Less dense than the fade
   but same per-toggle invalidation cost. Still laggy.
6. **Blink the gear digit**. Current implementation. `gear_indicator`
   is ~80×110 px — invalidating it costs ~70× less per blink than the
   tach area, and the timer fires 4×/sec instead of 60×/sec.

## Boot sequence

```
bsp_display_start_with_config   // panel + LVGL up, backlight at LEDC duty 0
settings_store_init             // NVS open, defaults if first boot
vehicle_data_init               // data sources must be live before
phone_data_init                 //   boot_screen_show's safety-timer
sim_engine_start                //   fast-path into the ride screen
bsp_display_lock                // hold the LVGL lock for the paint sequence
  loop 3×:                      //   triple-partial has 3 PSRAM framebuffers
    invalidate(scr) + lv_refr_now + unlock + 20ms + lock
                                //   cycles all three buffers to black so
                                //   none of them shows PSRAM init garbage
                                //   (~0xFFFF / white) when the panel lights
  boot_screen_show              //   GIF widget paints on top of black
  lv_refr_now
bsp_display_unlock + 20ms       // last DMA flush reaches the panel
bsp_display_brightness_set      // LIGHTS THE PANEL at saved brightness — this
                                //   is the moment the user sees the screen
sound_init / sound_set_*        // codec, queue, click sample baked
ble_peripheral_init             // NimBLE host, esp_hosted VHCI brings up
                                //   the BLE controller on the C6
                                // GIF eventually hands off to ride screen
                                //   on LV_EVENT_READY (or 15 s safety)
```

The `bsp_display_brightness_set()` is the only call that actually turns
the backlight on. `bsp_display_backlight_on()` is just
`bsp_display_brightness_set(100)`; any duty change lights the panel.
Painting black-then-GIF before that call is what prevents the boot
white flash.

## Out-of-band timing notes

- **Sim tick**: 20 Hz (50 ms). Writes a complete `vehicle_data` snapshot
  per tick.
- **UI tick**: 30 Hz (33 ms). Reads `vehicle_data`, runs the widget
  setter sweep under `bsp_display_lock`.
- **Event watcher tick**: 100 Hz (10 ms). Independent of LVGL load.
- **LVGL refresh**: `CONFIG_LV_DEF_REFR_PERIOD=15` (~66 Hz).
- **Boot GIF**: native 600×600, software blit. Native 800×800 was
  attempted but smoothness suffered when PPA was disabled (see
  CLAUDE.md gotchas).

## Tried and rejected

- **PPA** for the cooked-GIF blit. Caused horizontal banding on this
  BSP's DSI tear-avoid triple-buffer setup. Re-enable only with a
  known-good alignment story.
- **Lottie / ThorVG** for the boot animation. Vector rasterisation at
  800×800 isn't real-time on the P4. Don't retry unless either the
  resolution drops to ≤200×200 or ThorVG's rasteriser gets much faster.
- **LVGL `LV_EVENT_PRESSED` / `_LONG_PRESSED`** for touch on the ride
  screen. Hover events fired but press never reached the screen on this
  BSP. We bypass LVGL's gesture system and poll `lv_indev_get_state()`
  directly from `event_watcher_task`.
- **Line color animation / glow opacity animation** for the upshift
  warning. See the "long story" above.
