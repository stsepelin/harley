# Phase 2.5: Off-bike Feature Work

> **Status: ⏳ in progress**
>
> Inserted between Phase 2 (gauge UI, complete) and Phase 3 (J1850 + GPS,
> blocked on parts) to keep the project moving while hardware ships. All
> work in this phase runs on the Waveshare ESP32-P4 board we already
> have — no bike harness, no GPS module needed.

## Goal

Land four off-bike features in a sensible order, each independently
usable on the bench:

1. **Touch input + screen-switching infrastructure** — foundation
2. **NVS-persisted settings screen + kph/mph units toggle** — first
   user-visible feature, exercises the new screen-switch path
3. **BLE phone integration** — separate workstream, biggest scope
4. **Speed-camera alert framework** — write the engine + alert UI now,
   defer end-to-end validation until GPS arrives

## What we have already

- Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C: 800×800 round IPS panel,
  ESP32-P4 dual-core RISC-V @ 360 MHz, 32 MB PSRAM, 32 MB flash,
  microSD slot, **ESP32-C6 co-processor on board** wired via
  ESP-HOSTED for BLE5/WiFi6.
- GT911 touch controller initialised by the BSP — already shows up as
  an LVGL indev (per the `Touch input device registered successfully`
  serial log from earlier work).
- `vehicle_data_t` as the single data abstraction between producers
  (sim now, J1850 driver later) and the UI thread.
- `screen_ride` is currently the only LVGL screen; the boot_screen
  GIF hands off to it via `lv_screen_load`.

## What we're waiting on (and why it's blocked)

| Hardware | Blocks |
|---|---|
| NEO-6M/M8N GPS module | Real GPS speed/position → speed-camera validation, GPS time → real RTC |
| IRLZ44N + 2N2222 + zener + resistors | J1850 bidirectional transceiver → Phase 3 |
| GT 12-pin connector + T-taps + buck converter | Wiring to the bike harness |

## Stage 1 — Touch + screen-switching framework

**Why first**: nothing else in this phase can be built without it.

### Scope

- Make sure the GT911 LVGL indev (the BSP already registers it) is
  reachable from screen-level event handlers. Concretely, the active
  screen receives `LV_EVENT_PRESSED`, `LV_EVENT_LONG_PRESSED`,
  `LV_EVENT_GESTURE`, etc.
- Extend `ui_manager` to lazily create and hold both screens (ride +
  settings), with `ui_manager_show_ride()` / `ui_manager_show_settings()`
  swapping via `lv_screen_load`.
- A trigger to enter settings from ride. Default: **long-press
  anywhere** on the ride screen (intentional vs. accidental swipe;
  works with gloves; no precision needed). The settings screen has a
  big "Back" button.
- Update task keeps ticking regardless of which screen is loaded —
  `screen_ride_update` modifies the ride widgets' internal state even
  when off-screen so they're current when the user returns.

### Out of scope (deferred)

- Multi-screen navigation beyond ride ↔ settings. If we later need a
  third screen (e.g. trip computer, ride log) we'll generalise.
- Gesture-based scrolling. Long-press is enough for now.

## Stage 2 — NVS persistence + settings screen + units toggle

**Why bundled**: the settings screen exists to expose persisted prefs,
and the units toggle is the first prefs entry that needs both.

### Scope

- `settings_store` module wrapping ESP-IDF NVS:
  - `settings_load(settings_t *out)`
  - `settings_save(const settings_t *)`
  - Initial fields: `units` (kph/mph), `brightness` (0-100), eventually
    flags for shift-light / beam-rotation / etc.
- `screen_settings.c` UI: round-display-friendly layout (probably a
  centred column of tap targets — full-width rows with big text;
  scrolling if needed via `lv_obj_set_scroll_dir`).
- Trip reset buttons (zero `trip1_m` / `trip2_m` in the producer —
  needs a write path back into the producer, which currently is the
  sim).
- **Units conversion**: define a `display_units_t` shared header and
  thread it through `speed_display`, `odometer_display`,
  `trip_display`. Each widget reads the current pref + converts on
  set, or — cleaner — the producer publishes always-SI metric and
  the widgets convert on display.

### Tests to add

- `test_settings_format.c` — round-trip a `settings_t` through NVS
  with a stubbed NVS layer (similar to the existing FreeRTOS stub).
- `test_units.c` — kph↔mph, metres↔miles, tenths-of-km↔tenths-of-mi.
- Widget cache tests already pass for the units-aware widgets if the
  cache stays keyed on the displayed integer.

## Stage 3 — BLE phone integration

**Independent workstream**, biggest scope of the four. Can run in
parallel with stages 1–2 by another contributor (or in sequence).

> **Status update**: the cluster-side BLE peripheral skeleton is now
> in the build and runs on hardware. Advertises as `V-Rod Cluster`
> with a Nordic-UART-shaped GATT layout (RX write / TX notify), via
> ESP-HOSTED's SDIO link to the onboard ESP32-C6 controller. Getting
> here required working around a binutils 2.45 / IDF P4-rev<3 link
> trap — see `ble-bringup-bisect.md` for the resolution notes and
> the `default_registered_chips[]` + LVGL-fast-mem-to-flash workaround
> that's now baked into `main/`. What remains in Stage 3 is the
> *protocol* work below; the bring-up blocker is gone.

### Scope

- ESP-HOSTED to the onboard ESP32-C6 (acts as the BLE radio for the
  P4 which has no native BLE). Pulls in `esp_hosted` managed component.
- ANCS (Apple Notification Center Service) — receive incoming call
  notifications, SMS, app pushes from a paired iPhone.
- AMS (Apple Media Service) — pull "now playing" track / artist / album
  + send play/pause/skip commands.
- UI: an overlay banner widget (probably top half of the round display)
  for call alerts; a small "now playing" line that can replace one of
  the rotating info-slot items.
- Pairing UX: BLE advertises as `V-Rod Cluster`; iPhone bonds once,
  remembers thereafter. Store the bond key in NVS.

### Out of scope

- Android companion app — needs an APK side-project, defer to Phase 4.
- Navigation banner — would need turn-by-turn intent from a phone app,
  defer.

### Tests to add

- `test_ancs_parse.c` — feed canned ANCS notification payloads through
  the parser, assert title / sender / app extraction.
- BLE protocol layer is hard to unit-test on host; rely on the
  simulator + on-device exercising.

## Stage 4 — Speed-camera alert framework

**Last in the phase** because it benefits most from real GPS arriving
mid-stage so end-to-end validation lands naturally.

### Scope

- `gps_source_t` abstraction — a struct of `{lat, lon, speed_kmh,
  heading_deg, fix_ok, time_utc}` published the same way as
  `vehicle_data_t`. Stub producer for now: a `gps_sim.c` that walks a
  canned route.
- Camera DB format: tightly packed binary on the microSD card.
  `[lat_int32, lon_int32, kind:8, limit_kmh:8, heading_deg:16]` per
  record. Load + spatial-index on boot.
- Alert engine: every GPS tick, check whether any camera is within
  alert radius along our current heading. Fire alert events.
- UI: a transient warning popup near the top of the ride screen,
  showing camera kind + distance + posted limit. Auto-dismiss after
  the camera is passed or N seconds.

### Tests to add

- `test_gps_format.c` — coordinate / heading / km-to-degrees math.
- `test_camera_index.c` — bounding-box + heading filtering on a
  synthetic DB.
- `test_alert_engine.c` — feed a sequence of `gps_source_t` updates
  and assert which alerts fire and in what order.

## Out of phase entirely (still future)

These came up during discussion but belong elsewhere:

- **Real RTC / SNTP** — needs WiFi (Phase 7 polish) or GPS time
  (Phase 3) — mock clock for now.
- **Auto-brightness** — needs BH1750 sensor (Phase 7).
- **Ignition power management** — needs the bike harness (Phase 6).
- **OTA** — Phase 7.
- **Ride logging** — Phase 7.

## Suggested order

1. Stage 1 — touch + screen switching (foundation, ~half day)
2. Stage 2 — NVS + settings + units (one weekend)
3. Decision point: hardware arrived?
   - **Yes** → jump to Phase 3 (J1850 driver replaces sim, real bike
     data lands), then BLE, then camera framework
   - **No** → continue with Stage 3 (BLE), Stage 4 (camera framework)
