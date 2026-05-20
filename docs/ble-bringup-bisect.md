# BLE peripheral bring-up — bisection notes

Working notes for the link-time trap that blocks compiling the cluster
firmware with BT enabled. Pick this up when the next session has time
to finish the bisect.

## The trap

ESP-IDF v6.0.1 / binutils 15.2 / RISC-V. When the full cluster app is
linked with `CONFIG_BT_NIMBLE_ENABLED=y` + esp_hosted VHCI transport,
`ld` runs forever at the final link step (2207/2210), 56% CPU,
zero-byte `.elf` output. Original failure mode (before the
sdkconfig fixes below) was different: ld emitted ~27 KB of
`--enable-non-contiguous-regions` discards (mbedtls, gdma, libstdc++,
libc `.sdata`) then segfaulted in `collect2`. After the fixes the
discards are silent but ld still spins.

Sandbox repro lives at `/tmp/bleprph_test` (clone of esp_hosted's
`host_nimble_bleprph_host_only_vhci` example with our cluster sources
copied in). 3-minute timeout is enough to detect the hang — it never
makes progress past 2207/2210 once it stalls.

## What was tried and didn't fix it

- **PSRAM XIP off** — `CONFIG_SPIRAM_XIP_FROM_PSRAM=n`,
  `SPIRAM_FETCH_INSTRUCTIONS=n`, `SPIRAM_RODATA=n`. Keep PSRAM as heap
  for the framebuffer; just don't relocate `.text`/`.rodata` there.
  Helped the original segfault but didn't unblock ld.
- **IRAM relief** — `CONFIG_SPI_FLASH_ROM_IMPL=y`,
  `HEAP_PLACE_FUNCTION_INTO_FLASH=y`,
  `FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y`,
  `FREERTOS_PLACE_SNAPSHOT_FUNS_INTO_FLASH=y`. Necessary, not sufficient.
- **Small-data section disabled** — `add_compile_options(-msmall-data-limit=0)`
  in top-level `CMakeLists.txt`. Propagated to all 2046 compile units
  (verified in `build/compile_commands.json`). Eliminated the noisy
  `.sdata` discard wall but ld still hangs silently.
- **NimBLE transport** — `CONFIG_BT_NIMBLE_TRANSPORT_UART=n` plus
  `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`. Required for the VHCI shim
  on esp_hosted to claim the HCI transport.
- **Component versions** — esp_hosted ^2.12 + esp_wifi_remote ^1.5
  (Waveshare's reference 1.4/0.14 predates IDF v6 and won't compile).

The sdkconfig.defaults + CMake snippets are in `7723c98^` if you need
to re-apply them.

## What we proved

| Step | Adds | Result |
|---|---|---|
| 0 | bleprph_vhci baseline | ✅ 27s |
| 1 | PSRAM (heap only) | ✅ 35s |
| 2 | 16 MB flash + custom partitions | ✅ 35s |
| 3 | LVGL managed component (not exercised) | ✅ 53s |
| 4 | Waveshare BSP + LVGL + gdma + esp_lcd + lvgl_adapter | ✅ 57s, 1.16 MB |
| 5 | esp_codec_dev | ✅ 14s |
| 6 | phone/phone_data.c + phone/phone_protocol.c + ble/ble_peripheral.c | ✅ 24s |
| 7 | + display + widgets + screens + fonts + boot_screen + ui_manager + settings + sound + vehicle + simulator | ❌ ld hangs |
| 7 minus fonts | font `.c` stubbed (kept symbols, dropped ~1.4 MB rodata) | ❌ same hang |
| 7 minus display subsystem | drop `display/` except `units.c` + `format.c` | ✅ 76s, 770 KB |

**Ruled out** as triggers: ESP-IDF v6, NimBLE host, VHCI transport,
esp_hosted, the Waveshare BSP, LVGL, gdma, esp_lcd, esp_codec_dev,
our phone module, our `ble_peripheral.c`, the font rodata.

**Trap lives somewhere in**: `display/widgets/*.c`, `display/screen_*.c`,
`display/ui_manager.c`, `display/boot_screen.c`, or a specific
interaction between them. Tangled enough that simple "drop half"
experiments hit transitive header chains.

## Suggested next-session bisect

The fast path: start from step 7 full (the cluster's actual code), then
neutralise one display sub-area at a time. For each, comment out the
init call in `main.c` AND drop the matching `.c` from `main/CMakeLists.txt`.
Each iteration: ~1 min if link succeeds, ~3 min if it traps.

1. **Drop boot_screen + ui_manager** (`boot_screen.c`, `ui_manager.c`,
   `boot_screen_show()` and `ui_manager_init()` calls).
   - Builds → trigger is in the LVGL screen-orchestration layer.
   - Hangs → keep going to step 2.
2. **Drop screens** (`screen_ride.c`, `screen_settings.c`).
   - Builds → trigger is in the screen composition (widget assembly).
   - Hangs → trigger is in widgets themselves.
3. **Drop widgets** (28 files in `display/widgets/`).
   - Should build by elimination; confirms.

Once narrowed to a single sub-area, swap individual files in/out of
SRCS to find the specific file that tips ld. Each swap is one more
3-min ceiling test.

## Reproduction setup

`/tmp/bleprph_test` may have been cleaned up. To rebuild it:

```bash
cp -R "$IDF_PATH/../../managed_components/espressif__esp_hosted/examples/host_nimble_bleprph_host_only_vhci" /tmp/bleprph_test
cd /tmp/bleprph_test
# Apply the cluster's sdkconfig.defaults + top-level CMake -msmall-data-limit=0
# Copy main/{ble,phone,display,settings,sound,vehicle,simulator,assets,main.c,CMakeLists.txt} from this repo's main/
# Update main/idf_component.yml to add esp_hosted ^2.12 + esp_wifi_remote ^1.5 + lvgl/lvgl ^9
# Copy our partitions.csv
idf.py set-target esp32p4
timeout 180 idf.py build           # hangs at step 2207/2210
```

The committed BLE peripheral source itself (`main/ble/ble_peripheral.{c,h}`,
`7723c98`) is correct as written — verified by step 6 building cleanly.
Don't rewrite it; the bug is purely in the link-time interaction between
the cluster's display code and `--enable-non-contiguous-regions`.
