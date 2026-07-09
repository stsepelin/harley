# Host tests

Fast unit tests for the project's pure-logic modules. Build with the host
compiler (Apple clang / GCC), no ESP-IDF required.

## Run

```sh
cd test_apps/host
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

## With coverage

One-time setup (macOS):

```sh
brew install lcov
```

Then from `test_apps/host/`:

```sh
./coverage.sh
```

That builds with gcov instrumentation, runs ctest, extracts the
policy-scoped subset, prints a line / branch summary, generates HTML
under `build-cov/html/`, and opens it in your browser. Pass
`--no-open` to skip the auto-open.

If you'd rather drive it by hand:

```sh
./coverage.sh            # builds with instrumentation, runs the suite,
                         # extracts the gated scope, opens the HTML report
```

The gated file list lives in `coverage.sh` (`SCOPED=`) and must stay in
sync with the CI workflow's `lcov --extract` list.

CI runs the same flow on every push (`.github/workflows/host-tests.yml`)
and fails the build if the tested-by-policy modules drop below 100 % line
and 100 % branch coverage.

## Coverage policy

We claim **100 % coverage of tested-by-policy code**, not 100 % of the
repo. The set of files in scope is whatever ends up in `vrod_pure` in
`CMakeLists.txt`, and the same list lives in the CI `lcov --extract`
filter. The two must stay in sync.

Today that's:

| File | Why it's in scope |
|---|---|
| `main/simulator/gear_table.c` | Pure math: speed → (gear, RPM) |
| `main/simulator/sim_math.c` | Distance integrator, clock advance / split, fuel cycle state machine |
| `main/display/format.c` | Pure formatters: thousand-separated integer |
| `main/display/gesture.c` | Long-press + swipe state machine shared by firmware and sim |
| `main/display/units.c` | Pure math: km/h ↔ mph and metre ↔ km/mi conversions |
| `main/display/widgets/smooth.c` | Pure math: single-pole step with snap |
| `main/display/widgets/fuel_scale.c` | Fuel band grid quantization + gap-split segments |
| `main/phone/phone_data.c` | Mutex-guarded latest-value store + notification queue. FreeRTOS-stubbed. |
| `main/phone/phone_protocol.c` | Binary TLV parser for the companion-app BLE wire format |
| `main/phone/telemetry_codec.c` | Cluster -> phone telemetry frame encoder (vehicle_data -> TLV) |
| `main/settings/settings.c` | Defaults + validate for the persisted prefs struct |
| `main/vehicle/vehicle_data.c` | Mutex-guarded latest-value store. Tested with a FreeRTOS stub. |
| `main/vehicle/gear_calc.c` | Gear from the RPM:speed ratio (no gear sensor on the bike): match to the spec's exact overall ratios + boundary hysteresis. |
| `main/vehicle/trip_meter.c` | Rolling 16-bit bus counter (odometer/fuel ticks) -> per-frame delta, wrap-safe + reset-clamp. |
| `main/vehicle/odo_meter.c` | Odometer + dual-trip totals: add distance/fuel, reset a trip, set the odometer. Pure (odo_store owns NVS). |
| `main/ble/ble_visibility.c` | Pure decision: `(has_bond, override) → adv_mode`. Stage 8. |
| `main/j1850/j1850_vpw.c` | J1850 VPW symbol codec: pulse-width decoder + encoder + CRC-8/SAE-J1850. Round-trip tested. |
| `main/j1850/j1850_parse.c` | J1850 message decoder: frame -> vehicle_data (RPM/temp/speed/turns/CEL), calibrated against real captures. Gear is not on the bus (see gear_calc). |
| `main/j1850/j1850_driver.c` | J1850 producer glue: decoded frame -> j1850_parse (+ gear_calc, + odometer/fuel tick accumulation) -> vehicle_data_set (running aggregate). |
| `main/j1850/j1850_edge.c` | Toggling edge->level tracker (no pin read): toggle + recessive-idle re-sync anchor; a missed/spurious edge self-limits to one frame. |
| `main/j1850/j1850_tx_logic.c` | J1850 TX pure logic: CRC frame build (round-tripped through encode→decode) + the watchdog dominant-length guard + on-air duration. |
| `main/j1850/ride_log_format.c` | Ride-log line/header formatting: frame -> one plain-text line (sec.ms, hex, CRC, IFR, decoded speed/temp/gear suffix), capture.py-compatible. |

`main/j1850/j1850_sniffer.c` (GPIO-ISR capture glue),
`main/j1850/j1850_tx.c` (RMT/gptimer TX driver + watchdog), and
`main/j1850/ride_log.c` (SD/FATFS mount + flush-task glue) are
FreeRTOS/driver glue and stay out of the gate.

### Widgets — also gated at 100 %

The widgets (`speed_display`, `gear_indicator`, `temp_display`,
`turn_signals`, `clock_display`, `odometer_display`, `trip_display`,
`warning_lights`, `fuel_arc`, `fuel_scale`, `notification_banner`,
`media_banner`, `now_playing_display`, `widget_util`,
plus the shared `sprite_raster.h` helpers) link against an LVGL stub in
`test_widget_caches.c` / `test_sprite_raster.c` and are **inside the
100 % line/branch gate** alongside the pure-logic modules. The stub fires
timers (`lv_timer_stub_fire_all`), synthesizes button clicks
(`lv_event_stub_click_all`), injects allocation failures
(`heap_caps_stub_fail_next`) and fakes layout heights
(`g_lv_stub_obj_height`), so the blink/click/alloc-failure paths run on
host too. The primary property remains: **the skip-if-unchanged caches
don't accidentally re-render**. If a future refactor breaks a cache, the regression
test fails; the widget files just don't appear in the coverage report.

To add a file: extract its pure-logic parts into a free-function module
with no LVGL / FreeRTOS / ESP-IDF deps, add it to `vrod_pure`, write the
test, update the lcov filter in the workflow, and add a row to the table
above.

Files **deliberately excluded** from the metric:

- `display/fonts/*.c` — generated by `lv_font_conv`. Testing them tests
  the generator.
- (none on the widget side — `tach_arc.c` joined the gate via the stub's
  canvas + block-glyph rasterizer and allocation-failure hooks; its baked
  OUTPUT is still verified visually in the simulator + on device, since a
  block glyph can't prove the real font rendering looks right.)
- `display/boot_screen.c`, `display/ui_manager.c`, `display/screen_ride.c`,
  `main.c`, `simulator/sim_engine.c` (the task body)
  — BSP / FreeRTOS / LVGL glue. Validated on hardware, not here.
  (`vehicle/vehicle_data.c` used to sit in this list but has been in
  scope since it gained the FreeRTOS-stub test — see the table above.)
- `assets/boot.gif`, `managed_components/**` — not source code we own.

This list is on purpose. Adding a widget creation path to it would be a
red flag — write a test for the *helper* the widget uses, not for the
glue around it.

## Adding a test

1. Drop a `tests/test_<name>.c` that defines `void RunTests(void)` and
   calls `RUN_TEST(case)` on each Unity test function.
2. Add `add_unit_test(test_<name>)` to `CMakeLists.txt`.
3. If the test exercises a new module, also add the module's `.c` to the
   `vrod_pure` library and to the lcov filter in the workflow.

## Why Unity, vendored

Unity (the test framework, not the game engine) is the same one ESP-IDF
uses for its own component tests. Vendoring it under `external/unity/`
keeps the host tests self-contained — no network at CI time, no version
skew between host and target. The three files are an unmodified copy
from `$IDF_PATH/components/unity/unity/src/`.
