# Phase 2.5: Off-bike Feature Work

> **Status: ✅ complete** (June 2026 — all 8 stages landed; loose ends
> carried forward are listed at the bottom of this file)
>
> **Historical record.** This documents what Phase 2.5 built at the time.
> **Stage 7 (the speed-camera / POI alert framework) and its fake-GPS test
> harness were REMOVED in July 2026** when onboard GPS was dropped — speed
> comes from the J1850 bus and the camera feature depended on GPS position
> (see the PROJECT-BRIEF changelog). The Stage 7 text below is kept for
> history; that code no longer exists.
>
> Inserted between Phase 2 (gauge UI, complete) and Phase 3 (J1850 bus,
> which was blocked on parts) to keep the project moving while hardware
> shipped. All work in this phase ran on the Waveshare ESP32-P4 board we
> already had — no bike harness needed. The parts have since arrived;
> Phase 3 is the active phase.

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
  ESP32-P4 dual-core RISC-V @ 360 MHz, 32 MB PSRAM, 16 MB flash,
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
> trap — see `firmware/docs/ble-bringup-bisect.md` for the resolution notes and
> the `default_registered_chips[]` + LVGL-fast-mem-to-flash workaround
> that's now baked into `firmware/main/`. What remains in Stage 3 is the
> *protocol* work below; the bring-up blocker is gone.
>
> **Connection surface (added later)**: the cluster now exposes
> connection state to the UI — a blue dot at the top of the ride
> screen when a central is connected, and a PHONE row in settings
> showing `ADVERTISING` / `TAP TO DISCONNECT` + the peer's address.
> The companion app (Android) is what writes notifications into the
> RX characteristic; iOS is **not** covered by this path (see "iOS
> scope decision" below).

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
  the parser, assert title / sender / app extraction (deferred along
  with the iOS scope decision below).
- BLE protocol layer is hard to unit-test on host; rely on the
  simulator + on-device exercising.

### End-to-end verification — Android path

Procedure for proving Stage 3 actually works after a fresh flash +
install. Run through this once on hardware; record outcome in the
commit message that closes the stage.

1. **Flash + boot cluster** — `cd firmware && idf.py build flash monitor`.
   Boot animation → ride screen, no blue dot at the top.
2. **Settings row** — long-press the ride screen. The PHONE row should
   read `ADVERTISING` in dim text, with no address line.
3. **Install companion** — `cd companion && ./gradlew installDebug`.
   Open the app. First-run, you'll see two grant buttons:
   - **Grant notification access** → opens Android's Notification
     Access screen; toggle V-Rod Companion on.
   - **Grant Bluetooth permissions** → grants `BLE_SCAN` +
     `BLE_CONNECT` + `POST_NOTIFICATIONS`.
4. **Connect** — tap **Connect cluster**. The status line above the
   button cycles `Scanning…` → `Connecting to V-Rod Cluster…` →
   `Connected to V-Rod Cluster` within a few seconds. On success:
   - Cluster ride screen grows the blue dot at the top.
   - Cluster settings PHONE row flips to `TAP TO DISCONNECT` and shows
     the phone's address (matches what Android Bluetooth settings
     reports).
5. **Notification** — trigger a notification from any app on the
   companion's allow-list (SMS test send is the easiest). Cluster
   should render the notification banner at the bottom; swipe-up on
   the cluster should reveal the media banner if Spotify (or similar)
   is playing.
6. **Media** — start playback on the phone. Cluster's media banner
   ticker should update with track + artist within a second or two.
   (Play/pause/skip from cluster → phone is **not** wired yet —
   `access_tx_cb` flags this as pending — so those buttons currently
   produce a no-op. Document failure mode for now; fix is its own
   commit.)
7. **Disconnect** — tap the PHONE row in cluster settings. The blue
   dot on ride disappears, the row reverts to `ADVERTISING`. Companion
   status line flips to `Disconnected`, button reverts to **Connect
   cluster**. Tap it again to reconnect.
8. **Power-cycle reconnect** — confirms the link recovers from a
   cluster reboot (which happens every ignition cycle on the bike).
   1. Make sure you're connected (status line `Connected to V-Rod
      Cluster`, blue dot on).
   2. Unplug the cluster's USB-C cable, wait ~3 s, plug it back in.
   3. Watch all three: cluster serial shows
      `advertising as 'V-Rod Cluster'` within ~2 s of boot; cluster
      ride screen shows the blue dot return; companion status line
      flips `Disconnected` → `Scanning…` → `Connected` on its own.
   4. Send a test SMS — banner renders, proving the data path
      survived the reboot, not just the link.

   - **Success**: companion auto-reconnects, blue dot back within
     ~10 s, no taps needed.
   - **Acceptable**: status sticks at `Disconnected`; tapping
     **Connect cluster** brings it back. Means auto-reconnect-on-
     advert isn't wired in `BleService` yet — follow-up, not a
     blocker.
   - **Bad**: status still says `Connected` but notifications drop
     silently. Stale state in `BleService` — file an issue.

Known gaps to write up but **not** fix in this commit:

- No bonding / security model — just-works pairing, any nearby Android
  with the companion can connect. Add `ble_sm_*` config + IRK storage
  in a follow-up.
- Cluster → phone TX channel (CALL_ACCEPT, media prev/play/next) is
  in the GATT table but unused — needs a companion-side handler.
- No "Forget device" because we don't bond yet. The settings row's
  tap action drops the link only.

### iOS scope decision — needs your call

The original Stage 3 plan named ANCS + AMS as iOS support — i.e. the
cluster becomes a GATT *client* of the iPhone's well-known notification
service. That's meaningfully different work from the current
Android-companion path:

- Requires NimBLE security manager config (`ble_sm_*`) so iOS will
  expose ANCS at all.
- Requires GATT discovery on the connected peer (the iPhone), not just
  serving a local service.
- Adds two parsers: ANCS Notification Source/Data Source and AMS
  Now-Playing — both ~150-300 lines.
- Doesn't reuse the existing `phone_protocol` TLV layer at all.

Three options, ordered by ambition:

- **A. Defer iOS to Phase 4.** Honest. Phase 4 in the master plan
  already names "iOS ANCS/AMS via ESP32-C6". Leave Stage 3 at
  Android-only and call it done.
- **B. Add iOS as Stage 3b in this phase.** Real chunk of work
  (~1 weekend) but lands a polished story end-to-end.
- **C. Skip iOS entirely.** Only viable if Android is the primary
  phone for the bike.

Recommendation: **A**. Phase 2.5's purpose was to fill bench time
while parts ship; ANCS is a meaningful protocol implementation that
deserves its own phase. The decision belongs to the rider — which
phone do you actually use on the bike?

## Stage 4 — Host-side notification emulator

**Why now**: every render-side tweak (banner geometry, sanitize rules,
font subset, emoji fallback chain) currently requires building + flashing
the cluster *and* re-installing the companion APK + sending a real
notification. That's a >2-minute cycle for a 1-line UI change. A
host-side bridge collapses it to <5 seconds: edit, rebuild the SDL2 sim
on macOS, fire a payload via CLI, see the rendered banner.

### Scope

- `firmware/simulator/test_bridge.c` — TCP listener on `localhost:7700`,
  reads raw bytes off the socket → `phone_protocol_parse` →
  `phone_data_apply`. Same pipeline the cluster's BLE RX uses, so the
  wire format is the single source of truth. (7700 chosen so we don't
  collide with PHP-FPM on 9000.)
- `tools/notify.py` — small CLI that encodes payloads using the same
  TLV layout (mirrors `companion/.../Protocol.kt` byte-for-byte):
  - `--call "Alice"` → CALL notification, banner shows REJECT/ACCEPT
  - `--sms "Mom" "running late 🚗"` → SMS, banner with body
  - `--app pkg "title" "body"` → app notification
  - `--media playing "Foo Fighters" "Everlong"`
  - `--dismiss <id>` → NOTIF_DISMISS
- The existing `phone_mock.c` scripted timeline stays for the boot
  demo; the socket bridge sits alongside it for ad-hoc poking.
- Same socket can drive the cluster→phone TX channel — bridge prints
  decoded command bytes to stdout so we can verify e.g. that tapping
  ACCEPT on the sim fires CALL_ACCEPT bytes.

### Tests to add

- `test_test_bridge.c` (host-only) — feed canned TLV bytes through the
  bridge entrypoint, assert that `phone_data_get()` reflects the
  applied state.
- The notify.py encoder is cross-checked against `phone_protocol`'s
  existing parser tests — same fixtures, two languages.

## Stage 5 — BLE pairing + bonding

**Why now**: replace the current just-works "any nearby central can
connect and write" model with proper bonding. Once the cluster is on
the bike, an unbonded central writing garbage to the RX characteristic
shouldn't be possible.

### Scope

- NimBLE security-manager config: `BT_NIMBLE_SM_SC=y` (LE Secure
  Connections), set `ble_hs_cfg.sm_bonding=1`, `sm_mitm=1`, `sm_sc=1`,
  reasonable `sm_our_key_dist` / `sm_their_key_dist`.
- IO capability: `BLE_HS_IO_DISPLAY_ONLY` — cluster has a screen but
  no keyboard. That selects numeric-comparison passkeys (6 digits
  shown on cluster + phone; rider eyeballs that they match before
  confirming on the touchscreen).
- New `screen_pairing.c` (or extend `screen_settings`): when
  `BLE_GAP_EVENT_PASSKEY_ACTION` fires, render the passkey + ACCEPT /
  CANCEL buttons. Tap → `ble_sm_inject_io`.
- Bonds persist in NVS (`CONFIG_BT_NIMBLE_NVS_PERSIST=y` already on).
  Settings PHONE row grows a "Forget all devices" entry →
  `ble_store_clear()`.
- Settings PHONE row shows the bonded peer name with a visible
  "trusted" cue (e.g. lock glyph in front of the address).
- Companion side: nothing protocol-wise changes — Android's pairing
  flow handles SC numeric comparison natively.

### Developer escape hatch

Once enabled, the cluster won't talk to an unbonded phone. If the NVS
bond storage gets wiped (fresh flash, NVS partition resize), re-pairing
from the phone side recovers. For bench work where we want to skip
pairing entirely, add a build-time flag `CONFIG_VROD_BLE_INSECURE`
(default `n`) that compiles the SM config out and reverts to the
just-works model. CI doesn't set the flag; bench flashes do if needed.

### Tests to add

- `test_pairing_state.c` — unit-test the pure state-machine glue that
  routes GAP passkey-action events into the screen.
- Round-trip pairing flow is hard to host-test (Android security stack
  + NimBLE host both involved); rely on the device + companion.

## Stage 6 — No-sim build flag

**Why now**: small but useful. The synthetic 32-second driving cycle is
fine on the bench, but adds noise to the gauge dial when we're trying
to demo notification rendering, eats a few KB of flash, and won't be
the data source once Phase 3 lands.

### Scope

- `CONFIG_VROD_INCLUDE_SIM_ENGINE` Kconfig knob, default `y`.
- `firmware/main/CMakeLists.txt`: `simulator/sim_engine.c` +
  `simulator/sim_math.c` move under an `if(CONFIG_VROD_INCLUDE_SIM_ENGINE)`
  guard. `simulator/gear_table.c` stays unconditional — the gear
  indicator widget consumes it independently.
- `main.c`: `#if CONFIG_VROD_INCLUDE_SIM_ENGINE … sim_engine_start();
  #endif`.
- Desktop SDL2 sim's `CMakeLists.txt` is separate and always includes
  the sim sources — it has no other data source on the desktop.

When the flag is off and no other producer is plugged in, the gauge
sits at zeros. That's fine — it's what makes notification-rendering
bench work cleaner. Once Phase 3 lands, the J1850 driver becomes the
producer in the off-sim build.

### Tests to add

- No new tests — the existing host tests use the sim_math /
  sim_engine pure-logic modules directly via their own includes,
  unaffected by the firmware-side Kconfig.

## Stage 7 — Speed-camera alert framework (off-bike portion)

**Last in the phase** — the framework is fully off-bike testable via
the SDL2 sim + a fake GPS producer + a hand-authored test camera DB.
The on-bike pieces (real NEO-6M wiring + real SCDB import + ride
validation) live in Phase 5.

### Scope (here)

- `gps_source_t` abstraction — a struct of `{lat, lon, speed_kmh,
  heading_deg, fix_ok, time_utc}` published the same way as
  `vehicle_data_t`. Stub producer: `gps_sim.c` that walks a canned
  route at variable speed.
- Camera DB binary format:
  `[lat_int32, lon_int32, kind:8, limit_kmh:8, heading_deg:16]` per
  record. Spatial-index on boot, look up by bounding box on each tick.
- Alert engine: every GPS tick, check whether any camera is within
  alert radius along our current heading. Fire alert events.
- UI: transient warning popup near the top of the ride screen, showing
  camera kind + distance + posted limit. Auto-dismiss after the
  camera is passed or N seconds.
- Audio hook into the existing `sound` module — one short tone on
  alert fire.

### Out of scope here — see Phase 5

- Real NEO-6M / M8N module wiring + NMEA parser → `gps_source_t`
- SCDB.info / OSM data export + import into the binary format
- microSD mount + DB loaded from a real file on the card
- On-bike validation: real fix → real camera detected → alert fires
  while riding

### Tests to add

- `test_gps_format.c` — coordinate / heading / km-to-degrees math.
- `test_camera_index.c` — bounding-box + heading filtering on a
  synthetic DB.
- `test_alert_engine.c` — feed a sequence of `gps_source_t` updates
  and assert which alerts fire and in what order.

## Stage 8 — BLE visibility hardening + directed advertising

**Why now**: Stage 5 closed the *data* path — RX writes require an
authenticated bond, so a stranger can't push fake SMS or call events
to the cluster. The *discovery* path is still wide open: any time the
phone is disconnected (out of range, off, before pairing), the
cluster advertises in undirected mode and shows up as "V-Rod Cluster"
in every BLE scanner within range. Stage 8 hides the cluster from
strangers once it's bonded, and gives the rider explicit control over
visibility for the "pair a second phone" case.

### Scope

- After a bond is stored in NVS, `start_advertising()` switches from
  general undirected (`BLE_GAP_CONN_MODE_UND`) to directed
  (`BLE_GAP_CONN_MODE_DIR`) targeting the bonded peer's identity
  address. Strangers scanning don't see the device at all — only the
  bonded phone responds. Bonded phone auto-reconnects when in range.
- Long-press on the PHONE row in settings (already wired to
  `ble_peripheral_forget_all_bonds()`) reverts the cluster to
  undirected so the next pairing can find it from any phone.
- New "BT VISIBILITY" toggle in the settings card (sits with PHONE).
  When ON, advertising forces undirected mode even with a bond
  stored — for adding a second phone without forgetting the first.
  Auto-reverts to OFF after the new bond completes.
- Toggle state persists in NVS like brightness/volume. Default OFF
  (i.e. directed advertising preferred whenever a bond exists).
- BLE peripheral state grows a `visible_override` flag that the UI
  thread can set/clear. `start_advertising()` consults
  `(has_bond, visible_override)` to choose adv mode.

### Out of scope

- Multiple-bond support — sticks with the existing
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` and one-bond-at-a-time model.
  Pairing a different phone wipes the previous bond. Multi-bond is
  more work than the value it adds for a single-rider use case.
- Resolvable Private Address rotation. The cluster's BLE identity
  address stays stable across sessions today, which is a tracking
  risk a determined passive sniffer could exploit. Worth revisiting
  if RPA becomes table-stakes for the deployment, but out of phase.

### Tests to add

- `test_ble_visibility_state.c` — pure-logic test for the
  `(has_bond, visible_override) → adv_mode` decision matrix, plus
  the auto-revert edge on new-bond-stored.
- End-to-end "directed adv to bonded peer" is hard to host-test
  (needs a real Android central); rely on the device + companion.

### End-to-end verification

1. Fresh flash, no bond. Cluster advertises undirected. Phone sees
   "V-Rod Cluster" in scan list, pairs successfully. Cluster stores
   the bond and switches to directed.
2. Power-cycle the cluster. It advertises directed at the bonded
   MAC. A second phone's scanner does *not* see the cluster. The
   bonded phone reconnects automatically.
3. In settings, toggle BT VISIBILITY on. Cluster switches back to
   undirected. Second phone now sees + pairs. After the new bond
   commits, BT VISIBILITY auto-toggles off; advertising reverts to
   directed at the new peer (first bond was overwritten).
4. Long-press PHONE row → "Forget all devices". Cluster reverts to
   undirected, accepts pairing from any phone again.

## Out of phase entirely (still future)

These came up during discussion but belong elsewhere:

- **Real RTC / SNTP** — needs WiFi (Phase 7 polish) or GPS time
  (Phase 3) — mock clock for now.
- **Auto-brightness** — needs BH1750 sensor (Phase 7).
- **Ignition power management** — needs the bike harness (Phase 6).
- **OTA** — Phase 7.
- **Ride logging** — Phase 7.

## Suggested order

The phase is now entirely off-bike, so every stage is independently
shippable without waiting on parts.

1. **Stage 1** — touch + screen switching ✅
2. **Stage 2** — NVS + settings + units ✅
3. **Stage 3** — BLE phone integration ✅
4. **Stage 6** — no-sim build flag ✅
5. **Stage 4** — host-side notif emulator ✅
6. **Stage 5** — BLE pairing + bonding ✅
7. **Stage 7** — speed-camera framework off-bike portion ✅
8. **Stage 8** — BLE visibility hardening ✅

## Loose ends carried forward

Small items left open when the phase closed; none block Phase 3.

- ~~**Cluster → phone media TX**~~ — **done since this list was
  written**: the banner buttons now encode `PHONE_CMD_MEDIA_*` TLVs
  (`phone_data.c` → `ble_peripheral_notify`) and the companion's
  `CommandHandler` dispatches them into `MediaController` /
  `TelecomManager`.
- ~~**poi modules not in the coverage gate**~~ — **closed at Phase 3
  kickoff**: branch gaps filled (degenerate-argument paths, full-bucket
  drop, directional-camera negative delta, active-alert re-latch, the
  360° float-rounding bearing wrap) and the three files added to the
  lcov filters + policy table.
- ~~**Companion auto-reconnect-on-advert**~~ — **closed at Phase 3
  kickoff**: on link loss the client arms `connectGatt(autoConnect=
  true)` to the last device (`ReconnectPolicy` gates on GATT status,
  so deliberate disconnects don't re-arm). Uses the controller accept
  list rather than a rescan, which also works under Stage 8 directed
  advertising. Needs its on-hardware run-through together with the
  Stage 8 E2E record below.
- **Stage 8 end-to-end verification record** — the 4-step
  directed-advertising checklist (fresh pair → power-cycle
  invisibility → visibility toggle for a second phone → forget-all)
  has not been formally run through on hardware and written down.
- **iOS scope decision** — still open; the plan's recommendation is
  Option A (defer ANCS/AMS to Phase 4, Stage 3 stays Android-only).
