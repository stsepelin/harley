# Zeppl

[![firmware build](https://github.com/stsepelin/zeppl/actions/workflows/firmware-build.yml/badge.svg)](https://github.com/stsepelin/zeppl/actions/workflows/firmware-build.yml)
[![host tests](https://github.com/stsepelin/zeppl/actions/workflows/host-tests.yml/badge.svg)](https://github.com/stsepelin/zeppl/actions/workflows/host-tests.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

> **Zeppl** — DIY digital instrument cluster for a 2009 Harley-Davidson VRSCF
> Muscle: an 800×800 round IPS gauge that drops into the stock dash, reads the
> bike's J1850 bus, and bridges phone notifications + media over BLE.

Built on the Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C (ESP32-P4 dual-core
RISC-V @ 360 MHz + ESP32-C6 BLE/WiFi coprocessor, 32 MB PSRAM, 16 MB flash).
LVGL 9 over MIPI-DSI on ESP-IDF v6, paired with the Zeppl Android companion
app for phone integration.

## What's in the repo

| Path | What it is |
|---|---|
| [`firmware/`](firmware/) | ESP-IDF cluster firmware — gauge UI, sim engine, BLE peripheral, host tests, desktop simulator |
| [`companion/`](companion/) | **Zeppl** Android BLE-central app — notifications + media bridge, live telemetry, and speed/fuel calibration for the cluster |
| [`docs/`](docs/) | Cross-system docs (project brief, master plan, current phase) |
| [`firmware/docs/`](firmware/docs/) | Firmware-internal docs (architecture, phase plans, bisect notes) |

Start with [`docs/PROJECT-BRIEF.md`](docs/PROJECT-BRIEF.md) for the whole-system view,
or jump straight to a component:

- [`firmware/CLAUDE.md`](firmware/CLAUDE.md) — firmware working notes
- [`companion/README.md`](companion/README.md) — Android app working notes

## Quick start

```sh
make help              # list every target
make build-fw          # build the firmware
make flash-monitor     # flash + open serial monitor
make test-fw           # run host unit tests (no MCU required)
make sim               # desktop SDL2 + LVGL simulator
make build-app         # build the Android app
make test-app          # run companion unit tests
```

The Makefile delegates to `idf.py` / `gradlew` in the right subdirectory
and sources the ESP-IDF environment for you. Override
`IDF_EXPORT=/path/to/export.sh` or `PORT=/dev/cu.usbmodemXXXX` on the
command line if defaults don't match your setup.

## Status

Phase 2 (gauge UI) and Phase 2.5 (off-bike work: touch + settings +
BLE phone integration) are complete — a synthetic driving cycle drives
the full widget set at 30 FPS on the round display. Phase 3 (J1850 bus
+ IM simulation) is well along: the RX sniffer, decode → vehicle_data
producer, on-board ride log, and the full **companion Stage 5** (live
telemetry, GPS speed calibration, config write-back to NVS, fuel
economy/range) are in and bench-validated. Remaining: the on-bike GPS
calibration ride to lock the speed divisor
([`firmware/docs/ride-2-calibration-plan.md`](firmware/docs/ride-2-calibration-plan.md)),
then TX / IM replay on the bench. See
[`docs/03-PHASE3-J1850-PLAN.md`](docs/03-PHASE3-J1850-PLAN.md).

See [`docs/00-MASTER-PROJECT-PLAN.md`](docs/00-MASTER-PROJECT-PLAN.md)
for the full roadmap and [`docs/02-PHASE2.5-OFFBIKE-PLAN.md`](docs/02-PHASE2.5-OFFBIKE-PLAN.md)
for the active stage breakdown.

## Hardware

- **Board**: Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C — [vendor page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-3.4c.htm)
- **Bike**: 2009 Harley-Davidson VRSCF Muscle (VRSC family, J1850 VPW bus,
  12-pin instrument harness)
- **Companion**: Android 16+ device (minSdk 36); the companion app is
  side-loadable in dev builds

The firmware *should* compile on any ESP32-P4 board with a 16 MB flash
and PSRAM, but the BSP layer (display, touch, codec) is Waveshare-specific.

## Development

```sh
# First-time IDF setup (one-off)
git clone https://github.com/espressif/esp-idf.git -b v6.0.1 ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32p4

# In this repo, with IDF active:
. ~/esp/esp-idf/export.sh   # or have Makefile source it via IDF_EXPORT
make build-fw

# Host tests (no IDF needed beyond cmake + lcov)
brew install lcov            # macOS
make test-fw

# Android side: standard Gradle + Android Studio
make build-app
```

See each component's working notes for deeper conventions (cache
discipline on widget setters, theme palette, font choices, BLE GATT
layout, etc.).

## Contributing

This is primarily a personal project for one specific bike, but the
firmware patterns (LVGL setter caches, V-Rod palette, BLE-over-VHCI
on a P4+C6 module, the binutils 2.45 link-trap workaround) are
generic enough to be useful elsewhere. Issues + PRs welcome —
especially ports to other Harley models or display boards.

If you're filing a bug, please include:
- ESP-IDF version + toolchain bundle
- ESP32-P4 silicon revision (`esptool chip_id`)
- Board model
- The full boot log if it's a runtime issue, or `idf_size.py --archives`
  output for a build-size issue

## License

[Apache 2.0](LICENSE). Vendor BSP components under `firmware/components/`
and `firmware/managed_components/` retain their own licenses (Apache 2.0,
MIT, etc.); see the LICENSE file inside each.
