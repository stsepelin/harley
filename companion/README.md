# companion

Android phone-side bridge for the [harley](../) cluster firmware.

Mirrors notifications and media state from the phone to the bike's
gauge over a BLE GATT link, and accepts commands back (accept / reject
calls, media transport). The firmware advertises as `V-Rod Cluster`
and this app is the central that pairs with it.

## Status

**Feature-complete for Phase 2.5.** BLE central with scan + device
picker, LE Secure Connections bonding (numeric comparison), a
foreground service (`FOREGROUND_SERVICE_CONNECTED_DEVICE`) hosting the
GATT link, `NotificationListenerService` relay with a per-app
allow-list, `MediaSessionManager` watcher for now-playing metadata,
and a `CommandHandler` that dispatches cluster-issued commands (call
accept/reject/end, media prev/play/pause/next, notification dismiss)
into `TelecomManager` / `MediaController`. The wire-format encoder is
unit-tested against the firmware's C parser fixtures.

Auto-reconnect: on link loss (`ReconnectPolicy` — supervision timeout,
i.e. cluster power cycle or out-of-range) the client re-arms a
background `connectGatt(autoConnect=true)` to the bonded address, so
the link heals on every ignition cycle without touching the phone.
Deliberate disconnects (either side's disconnect button) stay
disconnected.

## Why a custom companion app

The cluster's ESP32-C6 radio is BLE-only — no Bluetooth Classic, so
HFP / AVRCP / MAP are off the table. iOS exposes notifications and
media over ANCS + AMS natively; Android doesn't have an equivalent
profile, so we forward over our own GATT characteristic. Keeping the
wire format custom (rather than re-using a spec like ANCS) lets the
firmware speak the same protocol on both platforms.

## Requirements

- Android 16 (API 36) device or emulator
- Android Studio (AGP 9.2 + Kotlin 2.3)
- JDK 17 (the Robolectric test task self-selects JDK 21 via the toolchain)

## UI architecture

Material 3 **Expressive** (`MaterialExpressiveTheme` + the spring-based
`MotionScheme`), branded V-Rod dark theme by default (orange-on-near-black,
mirroring the cluster's own palette in `firmware/main/display/theme.h`;
Material You dynamic color is an opt-in toggle under Settings). The theme
lives in `ui/theme/` (`Color.kt` / `Type.kt` / `Theme.kt`).

Navigation is an adaptive `NavigationSuiteScaffold` (bottom bar on phones,
rail on larger screens) over a type-safe `@Serializable`-route NavHost
(`ui/Destinations.kt`, `ui/App.kt`), with a persistent connection-status
strip (`ui/ConnectionStatusBar.kt`) visible on every tab. Four top-level
destinations:

- **Ride** — live telemetry dashboard (`ui/RideScreen.kt`), reading
  `ble/TelemetryState`; offline empty-state until frames arrive.
- **Cluster** — device hub: link, bond management, setup/maintenance
  (`ui/ClusterScreen.kt`).
- **Settings** — notification/media relay + appearance + cluster config
  write-back (`ui/SettingsScreen.kt`).
- **History** — trips + economy trends (`ui/HistoryScreen.kt`, placeholder).

Version note: material3 is pinned to `1.5.0-alpha18` (newest Expressive
alpha still on the Compose 1.11 line — 1.5.0-alpha20+ / Compose 1.12 need
compileSdk 37; see `gradle/libs.versions.toml`).

## Build + run

```sh
# From the repo root via the top-level Makefile:
make build-app         # ./gradlew :app:assembleDebug
make test-app          # ./gradlew :app:test
make install-app       # ./gradlew :app:installDebug (to a connected device)

# Or directly:
cd companion
gradle wrapper         # one-time, generates gradlew jar
./gradlew :app:assembleDebug
```

## Wire format

`app/src/main/java/com/vrodcluster/companion/ble/Protocol.kt` mirrors
[`firmware/main/phone/phone_protocol.c`](../firmware/main/phone/phone_protocol.c)
byte-for-byte. The firmware's host tests
(`firmware/test_apps/host/tests/test_phone_protocol.c`) are the
canonical fixtures; `ProtocolTest.kt` asserts the same shapes from the
encoder side. Touch one, touch both.

## Layout

```
companion/
├── app/                          Main module
│   ├── src/main/
│   │   ├── AndroidManifest.xml
│   │   ├── java/com/vrodcluster/companion/
│   │   │   ├── MainActivity.kt
│   │   │   ├── ble/              GATT client, protocol encoder, state, telemetry
│   │   │   ├── media/            MediaSession watcher → wire format
│   │   │   ├── notif/            NotificationListener bridge
│   │   │   └── ui/               Compose app: theme/, nav shell, four tab screens
│   │   └── res/
│   └── src/test/                 JVM unit tests (Protocol, BleState, …)
├── build.gradle.kts
└── settings.gradle.kts
```

## Roadmap

- [x] BLE central: scan for `V-Rod Cluster`, bond, open the GATT
      service, hold the write characteristic, subscribe to the command
      notify characteristic.
- [x] Foreground service hosting the BLE link
      (`FOREGROUND_SERVICE_CONNECTED_DEVICE`).
- [x] `NotificationListenerService` → `Protocol.encodeNotif`.
- [x] `MediaSessionManager` listener → `Protocol.encodeMedia`.
- [x] Inbound command path → Telecom
      (`TelecomManager.acceptRingingCall`, `endCall`) and
      media key dispatch for prev/play/next.
- [x] Status / pairing UI (`ScanScreen`, device picker).
- [x] Auto-reconnect when the bonded cluster reappears after a power
      cycle (background `connectGatt(autoConnect=true)` armed on link
      loss — works under directed advertising, unlike a rescan).
- [x] Material 3 Expressive redesign: branded theme, adaptive
      navigation hub (Ride / Cluster / Settings / History), persistent
      connection status.
- [ ] Live telemetry stream on the Ride dashboard (Brick 1).
- [ ] GPS speed-calibration wizard (Brick 2).
- [ ] Cluster config write-back with ack/read-back (Brick 3).
- [ ] Trip history + fuel-economy trends (Brick 4).

## License

Apache 2.0 — see [`../LICENSE`](../LICENSE) at the repo root.
