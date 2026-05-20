# VRSCF Digital Cluster — Project Brief

**For Claude Code session continuation.**

---

## Project Overview

Custom digital instrument cluster replacement for a **2009 Harley-Davidson VRSCF Muscle** based on the **Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C** development board (3.4" round 800×800 IPS touch display, ESP32-P4 RISC-V + ESP32-C6 for WiFi6/BLE5).

## Hardware Status

- ✅ **Display board acquired and working** — Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
- 🛒 **Parts on order from AliExpress** (~€230 total): GPS module (NEO-6M/M8N), IRLZ44N MOSFETs, 2N2222 transistors, zener diodes, resistor kit, prototyping supplies, T-tap connectors, GT 12-pin connector, buck converter, etc.
- 🔧 **Local items needed**: Conformal coating spray, junction box

## Development Environment

- **OS**: macOS (MacBook Pro)
- **Editor**: Zed
- **Framework**: ESP-IDF v6.0.1 (with patched Waveshare BSP — `esp_lvgl_adapter ^0.4`)
- **Target chip**: esp32p4
- **Project root**: `/Users/stsepelin/Workspace/My Projects/harley/cluster`

## Repository Structure

```
cluster/
├── CLAUDE.md                          # Working conventions (read this first)
├── README docs in docs/               # Project planning + brief
│   ├── PROJECT-BRIEF.md               # This file
│   ├── 00-MASTER-PROJECT-PLAN.md      # Full v5 project plan + budget
│   └── 01-PHASE2-DISPLAY-PLAN.md      # Phase 2 plan (✅ complete)
├── main/                              # Firmware sources
│   ├── main.c                         # app_main: BSP init + boot screen
│   ├── assets/boot.gif                # Embedded boot animation
│   ├── vehicle/                       # Shared mutex-guarded state
│   ├── simulator/                     # Sim engine + sim math + gear table
│   ├── display/                       # Screens + widgets + fonts + theme
│   │   ├── boot_screen.{c,h}, screen_ride.{c,h}, ui_manager.{c,h}
│   │   ├── format.{c,h}, theme.h, widget_util.{c,h}
│   │   ├── fonts/                     # JBM Bold + MDI font subsets
│   │   └── widgets/                   # tach_arc, speed, gear, fuel, temp,
│   │                                  # turn signals, warning lamps, clock,
│   │                                  # odo, trip, smooth helper
│   └── CMakeLists.txt
├── test_apps/host/                    # Unity + Linux-target unit tests
├── simulator/                         # SDL2 + LVGL desktop simulator
├── components/                        # Patched Waveshare BSP
├── managed_components/                # LVGL, ESP LCD/Touch, esp_lvgl_adapter
├── .github/workflows/                 # firmware-build.yml + host-tests.yml
└── docs/waveshare-reference/          # Vendor examples kept for reference
```

## Current status

- ✅ **Phase 2 — Display & Gauge UI** complete (see `01-PHASE2-DISPLAY-PLAN.md`).
- ⏳ **Phase 2.5 — Off-bike feature work** in progress (see
  `02-PHASE2.5-OFFBIKE-PLAN.md`): touch + screen switching, settings,
  units toggle, BLE, speed-camera framework. All possible on the
  board we already have while J1850 + GPS hardware ships.
- 🟡 **Phase 3 — J1850 bus + IM simulation + GPS** blocked on parts.

Phase 2 deliverable summary: working 800×800 round gauge running off a
synthetic driving cycle. Includes tach (270° glow arc, redline split,
zoom-on-cursor labels, baked Gaussian cursor sprite, shift-light flash
at >9000 RPM), speed display, gear indicator, fuel bar, temperature,
turn signals + hazard, 7 warning lamps in two chevrons (oil, engine,
ABS, battery, immobiliser, low + high beam — beam slot rotates),
clock + odometer + dual trip counters cycling in a shared slot, and
an embedded GIF boot animation (LVGL's AnimatedGIF decoder; PPA HW
accel was tried and dropped — caused banding on this BSP).
Same widget code drives a desktop SDL2 simulator under `simulator/`
for iteration without flashing.

### Immediate next step: Phase 2.5 — Stage 1

Touch + screen-switching infrastructure. Foundation for the settings
screen / units toggle / BLE call overlay everything else in Phase 2.5
hangs on. See `02-PHASE2.5-OFFBIKE-PLAN.md` for the full plan.

Phase 3 (J1850 + GPS) takes over the moment hardware lands; the
data-abstraction layer (`vehicle_data_t`) means a sim → bus swap
won't touch the UI.

## Design Decisions Already Made

- **Architecture**: ESP32-P4 reads V-Rod's J1850 VPW bus (Phase 3), drives 3.4" round display, BLE for phone integration (Phase 4), GPS for position + speed camera alerts (Phase 5)
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

- **Phase 3**: J1850 bus integration + GPS
- **Phase 4**: BLE phone integration (iOS ANCS/AMS + Android companion app)
- **Phase 5**: Speed camera database from SCDB.info / OpenStreetMap
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

If you're picking up after Phase 2 (current state), the next-step is
**Phase 3 (J1850 bus + IM simulation + GPS)** — see the master plan.
