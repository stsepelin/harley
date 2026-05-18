# Desktop simulator

Runs the real production widget code in an SDL2 window. The synthetic
driving cycle (`main/simulator/sim_engine.c`) drives `vehicle_data` from
a real pthread, the LVGL UI thread renders, and what you see is exactly
what the device would render — minus the GIF boot screen.

Iterate on layout, colours, animation timing, etc. without flashing.

## One-time setup

macOS:

```sh
brew install sdl2
```

Linux:

```sh
sudo apt-get install libsdl2-dev
```

## Build + run

```sh
cd simulator
cmake -B build -S .
cmake --build build --parallel
./build/vrod_sim
```

Quit by closing the SDL window or hitting Ctrl-C.

## What's connected

| Piece | Source | How |
|---|---|---|
| Widgets | `main/display/widgets/*.c` | Same .c files the firmware compiles |
| Ride screen | `main/display/screen_ride.c` | Same |
| Sim cycle | `main/simulator/sim_engine.c` | Real pthread via FreeRTOS shim |
| Vehicle state | `main/vehicle/vehicle_data.c` | Real pthread mutex |
| Fonts | `main/display/fonts/*.c` | Same baked tables |

## What's *not* connected

- **Boot screen** — relies on the firmware's `EMBED_FILES` for `boot.gif`,
  which is ESP-IDF-only. The simulator loads straight into the ride screen.
- **PPA / DSI / actual panel timing** — the simulator renders to a window;
  panel-level performance characteristics (PSRAM bandwidth, refresh
  tearing) don't show up here.
- **Touch** — the panel has GT911 touch; SDL gives us mouse events instead.

## Files

```
simulator/
├── CMakeLists.txt        # finds SDL2 + LVGL, links the firmware widgets
├── main.c                # SDL2 window, LVGL init, render loop
├── lv_conf.h             # LVGL build config (SDL driver + pthread OS)
├── external/
│   ├── esp_compat/       # esp_heap_caps.h, esp_log.h shims
│   └── freertos_shim/    # pthread-backed FreeRTOS surface
└── README.md
```

If you add a widget to `main/display/widgets/`, add it to the source
list in `CMakeLists.txt` here too.
