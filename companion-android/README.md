# V-Rod Companion (Android)

Phone-side half of the cluster's BLE phone bridge. Mirrors notifications
+ media state from your Android device to the cluster, and accepts
commands back (accept/reject calls, media transport).

## Status

**Stage A — scaffolding.** Project builds, Compose status screen renders,
wire-format encoder is unit-tested against the cluster's C parser
fixtures. None of the platform integration (NotificationListener, BLE
central, foreground service) is wired yet.

## Requirements

- Android 16 (API 36) device or emulator.
- Android Studio Ladybug or newer (AGP 8.7 + Kotlin 2.1).
- JDK 17.

## Build + run

```sh
cd companion-android
# First time: let Gradle generate the wrapper jar (or open the project
# in Android Studio and click Sync — same effect).
gradle wrapper

./gradlew :app:assembleDebug
./gradlew :app:test                 # unit tests (Protocol.kt fixtures)
./gradlew :app:installDebug         # to a connected device
```

## Wire format

`app/src/main/java/com/vrodcluster/companion/ble/Protocol.kt` mirrors
`main/phone/phone_protocol.c` byte-for-byte. The cluster's host tests
(`test_apps/host/tests/test_phone_protocol.c`) are the canonical
fixtures; `ProtocolTest.kt` asserts the same shapes from the encoder
side. Touch one, touch both.

## Coming up

- BLE central: scan for `V-Rod Cluster`, bond, open the GATT service,
  hold the write characteristic and subscribe to the command notify
  characteristic.
- Foreground service hosting the BLE link (`FOREGROUND_SERVICE_CONNECTED_DEVICE`).
- `NotificationListenerService` → `Protocol.encodeNotif`.
- `MediaSessionManager` listener → `Protocol.encodeMedia`.
- Inbound command path → Telecom (`TelecomManager.acceptRingingCall`,
  `endCall`) and `MediaController.transportControls` for prev/play/next.
- Status / pairing UI on top of `StatusScreen`.

## Why a custom companion app

The cluster's ESP32-C6 radio is BLE-only (no Bluetooth Classic, so HFP /
AVRCP / MAP are off the table). iOS has ANCS + AMS natively; Android
doesn't expose an equivalent, so we need to forward over our own
characteristic. Keeping the wire format custom (rather than re-using a
spec like ANCS) lets the cluster work the same on both platforms.
