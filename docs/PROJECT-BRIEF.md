# VRSCF Digital Cluster — Project Brief

**For Claude Code session continuation.**

> **Changelog (July 2026): onboard GPS and the speed-camera / POI
> feature were dropped.** Speed comes from the J1850 bus, so onboard GPS
> was a large separate effort (module, UART producer, NMEA parsing,
> antenna) for little benefit, and the speed-camera alerts depended on
> GPS position. Both were removed from the firmware and the plans. A
> phone GPS over the existing BLE link could refine speed calibration
> later if wanted, with no new hardware.

---

## Project Overview

Custom digital instrument cluster replacement for a **2009 Harley-Davidson VRSCF Muscle** based on the **Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C** development board (3.4" round 800×800 IPS touch display, ESP32-P4 RISC-V + ESP32-C6 for WiFi6/BLE5).

## Hardware Status

- ✅ **Display board acquired and working** — Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
- ✅ **Parts arrived** (June 2026, ~€230 from AliExpress): IRLZ44N MOSFETs, 2N2222 transistors, zener diodes, resistor kit, prototyping supplies, T-tap connectors, GT 12-pin connector, buck converter, etc. — Phase 3 is unblocked.
- 🔧 **Local items needed**: Conformal coating spray, junction box

## Development Environment

- **OS**: macOS (MacBook Pro)
- **Editor**: Zed
- **Framework**: ESP-IDF v6.0.1 (with patched Waveshare BSP — `esp_lvgl_adapter >=0.4.0,<0.4.3`; 0.4.3 needs a post-v6.0.1 IDF)
- **Target chip**: esp32p4
- **Project root**: `/Users/stsepelin/Workspace/My Projects/harley` (firmware in `firmware/`, Android app in `companion/`)

## Repository Structure

```
harley/
├── CLAUDE.md                          # Top-level pointer → per-component CLAUDE.md
├── docs/                              # Cross-system docs
│   ├── PROJECT-BRIEF.md               # This file
│   ├── 00-MASTER-PROJECT-PLAN.md      # Full v5 project plan + budget
│   ├── 02-PHASE2.5-OFFBIKE-PLAN.md    # Phase 2.5 (✅ touch / settings / BLE)
│   ├── 03-PHASE3-J1850-PLAN.md    # Phase 3 (active: J1850 + IM sim)
│   └── schematics/                    # schemdraw sources + rendered SVGs
├── firmware/                          # ESP-IDF cluster firmware
│   ├── CLAUDE.md                      # Firmware-specific working notes
│   ├── docs/                          # Firmware-internal docs
│   │   ├── 01-PHASE2-DISPLAY-PLAN.md  # Phase 2 plan (✅ complete)
│   │   ├── ARCHITECTURE.md            # Threading, render pipeline, boot
│   │   ├── DISPLAY-PERF-AND-MEMORY.md # Render/RAM rules — read before drawing
│   │   ├── ble-bringup-bisect.md      # Resolution notes for the link trap
│   │   └── waveshare-reference/       # Vendor examples kept for reference
│   ├── CMakeLists.txt                 # ESP-IDF project root
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   ├── main/                          # app_main + per-feature subdirs
│   │   ├── main.c                     # boot sequence + chip-driver table
│   │   ├── assets/boot.gif            # Embedded boot animation
│   │   ├── ble/                       # NimBLE peripheral over esp_hosted VHCI
│   │   ├── phone/                     # Phone-payload protocol parser
│   │   ├── vehicle/                   # Shared mutex-guarded state
│   │   ├── simulator/                 # Sim engine + math + gear table
│   │   ├── settings/                  # NVS-backed user settings
│   │   ├── sound/                     # ES8311 audio + click samples
│   │   └── display/                   # Screens + widgets + fonts + theme
│   ├── components/                    # Patched Waveshare BSP
│   ├── managed_components/            # LVGL, ESP LCD/Touch, esp_hosted, etc. (gitignored)
│   ├── simulator/                     # Desktop SDL2 + LVGL simulator
│   └── test_apps/host/                # Unity + Linux-target unit tests
├── companion/                         # Android BLE-central app
│   ├── README.md
│   ├── app/                           # Kotlin sources (ble/, media/, notif/, ui/)
│   ├── build.gradle.kts
│   └── gradlew
├── .github/workflows/                 # firmware-build.yml + host-tests.yml + lint.yml
└── LICENSE
```

## Current status

- ✅ **Phase 2 — Display & Gauge UI** complete (see `firmware/docs/01-PHASE2-DISPLAY-PLAN.md`).
- ✅ **Phase 2.5 — Off-bike feature work** complete (see
  `02-PHASE2.5-OFFBIKE-PLAN.md`): touch + screen switching, settings +
  units toggle, Android BLE phone integration with SC bonding +
  directed advertising, host notification emulator, no-sim build flag
  — plus the BMW-style gauge redesign and the 100% host-test coverage
  gate. (A speed-camera framework was also built here and later removed
  with GPS in July 2026 — see the changelog at the top.) Loose ends are
  listed at the bottom of the phase plan; media TX and companion
  auto-reconnect have since been closed. Still open: the Stage 8 +
  reconnect on-hardware E2E record, and the iOS decision.
- ⏳ **Phase 3 — J1850 bus + IM simulation** is active (see
  `03-PHASE3-J1850-PLAN.md`). Parts arrived June 2026; work is the
  J1850 transceiver bring-up, sniffing/decode, and IM replay.

Phase 2 deliverable summary (as redesigned at the end of Phase 2.5,
BMW-EfficientDynamics styling): working 800×800 round gauge running
off a synthetic driving cycle. Includes tach (270° scale with inner
shadow bezel, capsule ticks, two-segment rounded redline, zoom +
colour-coupled labels 2/4/6/8/10 + OFF, baked Gaussian cursor sprite,
shift-light blink via the gear digit at >9000 RPM), speed display
(three digit slots, pegs at 999), gear selector with baked outline
(orange N), fuel arc (solid fill band, red when low, white section
majors), temperature, turn signals + hazard, 7 warning lamps in two
chevrons (oil, engine, ABS, battery, immobiliser, low + high beam —
beam slot rotates), clock + odometer + dual trip counters cycling in
a shared slot, and an embedded GIF boot animation (LVGL's AnimatedGIF
decoder; PPA HW accel was tried and dropped — caused banding on this
BSP). Everything visual is pre-baked into ARGB sprites (see
`firmware/docs/DISPLAY-PERF-AND-MEMORY.md`); the same widget code
drives a desktop SDL2 simulator under `firmware/simulator/` for
iteration without flashing.

### Immediate next step: Phase 3 — J1850 bring-up

Full staged plan in `03-PHASE3-J1850-PLAN.md`. Short version:
build the transceiver RX-only on a breadboard → passive J1850 sniff
through the proxy-box T-taps (bike keeps its stock cluster) → decode
against the HarleyDroid table → IM message replay via TX (verify no
U1255 / TSSM lockout). The software consumer side is built and
host-tested: `vehicle_data_t` means the sim → bus swap won't touch
the UI.

## Design Decisions Already Made

- **Architecture**: ESP32-P4 reads V-Rod's J1850 VPW bus (Phase 3), drives 3.4" round display, BLE for phone integration (Phase 4)
- **Data abstraction**: `vehicle_data_t` struct as single source of truth — UI doesn't care if data comes from simulator, J1850 bus, or BLE
- **Dual-core split**: Core 0 = J1850 + BLE + simulator, Core 1 = LVGL rendering at 30 FPS
- **Cluster replacement strategy**: Build proxy box with T-taps for safe development; final install replaces stock cluster entirely
- **IM simulation**: P4 must send periodic J1850 messages impersonating stock IM to avoid U1255 DTC and TSSM lockout
- **Bidirectional J1850 circuit**: IRLZ44N MOSFET for TX + 2N2222 voltage divider for RX (replaces SwapSmart which was out of stock)

## V-Rod 12-Pin Instrument Module Pinout (Pin 7 = J1850 Data Bus)

| Pin | Wire Color | Signal |
|---|---|---|
| 1 | R/O | +12V Battery constant |
| 2 | White | High Beam |
| 3 | Violet | Left Turn |
| 4 | Brown | Right Turn |
| 5 | BK/GN | Ground |
| 6 | Grey | +12V Ignition switched |
| 7 | LGN/V | **J1850 Data Bus** |
| 8 | (sub) | Vehicle Speed Sensor |
| 9 | GN/Y | Oil Pressure warning |
| 10 | TN | Neutral indicator |
| 11 | Y/W | Fuel Level sender |
| 12 | O/W | Accessories |

## Future Phases (not active yet)

- **Phase 3**: J1850 bus integration + IM simulation
- **Phase 4**: BLE phone integration (iOS ANCS/AMS + Android companion app)
- **Phase 5**: (removed — GPS + speed cameras dropped July 2026; numbering kept)
- **Phase 6**: Full cluster replacement + 3D-printed mounting bracket + conformal coating
- **Phase 7**: Polish — auto-brightness, themes, handlebar button, ride logging, OTA updates with on-screen progress (USB flashing impractical once the cluster is housed)

## Key References

- Waveshare board docs: https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-XC
- Waveshare GitHub: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC
- ESP-IDF P4 guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/
- LVGL docs: https://docs.lvgl.io/master/
- HarleyDroid (J1850 decode table): https://github.com/stelian42/HarleyDroid

## How to Use This Brief

When starting a new Claude Code session, the repo's `CLAUDE.md` is read
automatically — it has the always-true conventions. For project history
and roadmap context, point at this file plus `00-MASTER-PROJECT-PLAN.md`.

If you're picking up at the current state (Phase 2.5 complete, parts
in hand), the next step is **Phase 3 (J1850 bus + IM simulation)** —
see the master plan.
