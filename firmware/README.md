# firmware

ESP-IDF cluster firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C.

This is the bike-side half of [harley](../). Drives the 800×800 round
IPS gauge, reads the J1850 VPW bus (planned), hosts a BLE peripheral
that the [Android companion](../companion/) connects to.

## Build

```sh
# From the repo root, via the top-level Makefile (sources IDF for you):
make build-fw          # idf.py build
make flash-monitor     # flash + monitor (PORT=/dev/cu.usbmodemXXX to override)
make test-fw           # host unit tests + 100% coverage gate
make sim               # desktop SDL2 + LVGL simulator (brew install sdl2)

# Or directly with IDF in your shell:
cd firmware
. $IDF_PATH/export.sh
idf.py build
```

## Layout

```
firmware/
├── CLAUDE.md                Working conventions (read first)
├── CMakeLists.txt           ESP-IDF project root
├── partitions.csv           Custom partition table (16 MB flash)
├── sdkconfig.defaults       BT + esp_hosted + LVGL + IRAM workaround knobs
├── main/                    app_main + per-feature subdirs
│   ├── main.c               Boot sequence + chip-driver table
│   ├── ble/                 NimBLE peripheral over esp_hosted VHCI
│   ├── phone/               Phone-payload protocol parser
│   ├── vehicle/             Mutex-guarded vehicle_data_t
│   ├── simulator/           Synthetic driving cycle producer
│   ├── settings/            NVS-backed user settings
│   ├── sound/               ES8311 audio + click samples
│   └── display/             Screens + widgets + fonts + theme
├── components/              Patched Waveshare BSP
├── managed_components/      LVGL, ESP LCD/Touch, esp_hosted (gitignored)
├── simulator/               Desktop SDL2 + LVGL build of the gauge
├── test_apps/host/          Unity + Linux host tests (100 % policy scope)
└── docs/                    Firmware-internal docs (see ARCHITECTURE.md)
```

## Targets + toolchain

- **MCU**: ESP32-P4 dual-core RISC-V @ 360 MHz, **silicon rev v1.3** (see
  `docs/ble-bringup-bisect.md` for why the silicon rev matters)
- **ESP-IDF**: v6.0.1 (`esp-15.2.0_20251204` toolchain, GCC 15.2 / binutils 2.45)
- **LVGL**: 9.x via `lvgl/lvgl` managed component
- **BSP**: Waveshare `esp32_p4_wifi6_touch_lcd_xc` (under `components/`)
- **BLE controller**: ESP32-C6 coprocessor, talked to via SDIO (esp_hosted)

## Tests

```sh
make test-fw
```

Coverage gate: 100 % line + branch on the pure-logic modules listed in
`test_apps/host/README.md`. Widget caches have behaviour tests against
the LVGL stub. See `docs/ARCHITECTURE.md` for the testing model.

CI runs the same gate via `.github/workflows/host-tests.yml`. Firmware
itself is built in `.github/workflows/firmware-build.yml` against the
`espressif/idf:v6.0.1` container.

## Working notes

See [`CLAUDE.md`](CLAUDE.md) for conventions (LVGL setter caches, V-Rod
palette, font discipline, sdkconfig gotchas, the binutils 2.45 link-trap
workaround).

## License

Apache 2.0 — see [`../LICENSE`](../LICENSE) at the repo root. Vendor BSP
and managed components keep their own licenses (Apache 2.0 / MIT) in
their respective subdirectories.
