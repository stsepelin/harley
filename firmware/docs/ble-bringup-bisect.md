# BLE peripheral bring-up — bisection & resolution

**Status: RESOLVED.** Cluster firmware boots cleanly on v1.3 silicon
with BLE peripheral + esp_hosted VHCI + Waveshare BSP + LVGL all enabled.
Fix landed via a single `main/CMakeLists.txt` change (LVGL fast-mem to
flash) plus a small `sdkconfig.defaults` addition and a chip-driver
table in `main/main.c`. Detailed below.

The original bisection notes that preceded the fix are kept further
down as historical reference, because the underlying upstream bugs
(binutils + IDF) are still open and the next ESP-IDF / toolchain bump
could re-expose them.

---

## The trap (what was happening)

ESP-IDF v6.0.1 / **binutils ld 2.45** (toolchain bundle
`esp-15.2.0_20251204`) / RISC-V. When the full cluster app was linked
with `CONFIG_BT_NIMBLE_ENABLED=y` + esp_hosted VHCI transport + the
Waveshare BSP + LVGL, `ld` ran forever at the final link step
(2207/2210), ~56 % CPU, zero-byte `.elf` output.

It turned out to be **two bugs stacked**:

1. **binutils 2.45 regression**. Under `-Wl,--enable-non-contiguous-regions`,
   2.45 hangs indefinitely instead of erroring when it would have to
   discard sections that don't fit. Proven by swapping in just the `ld`
   + `ld.bfd` binaries from `esp-14.2.0_20260121` (binutils 2.43.1):
   same link, same .o files, finishes in 84 s with 63 explicit
   `error: --enable-non-contiguous-regions discards section …` lines and
   `rc=2`. 2.45 silently spins on the exact same input.

2. **IDF placement assumption on P4 < v3**.
   `esp_system/CMakeLists.txt:160` adds
   `-Wl,--enable-non-contiguous-regions` unconditionally for
   `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` (because pre-v3 P4 has
   discontiguous SRAM). At our app's link size — BT + esp_hosted
   whole-archive + Waveshare BSP + LVGL fast-mem in IRAM + spi_flash
   chip drivers all wanting IRAM — the placer can't fit ~4388 bytes
   of `.text.spi_flash_*` sections. Under 2.43.1 → 63 errors. Under
   2.45 → hang. The board is silicon rev v1.3, so this gate is on.

The 63 errors (visible only via the 2.43.1 swap):
- 58 from `libspi_flash.a` — chip_generic / memspi / os_func / chip_winbond / chip_gd / chip_mxic / chip_issi / chip_boya / chip_th / hpm_enable. The vendor-specific drivers are *compiled* unconditionally into `libspi_flash.a` even when their `SPI_FLASH_SUPPORT_*_CHIP` Kconfig is off (which is the case for P4 by default for everything except GD + XMC); they only get gated out of `default_registered_chips`.
- 5 from picolibc `libc.a` + `libstdc++.a` `.sdata` sections.
- 1 `Total discarded sections size is 4388 bytes`.

## What didn't ship as a workaround (and why)

- **`REV_MIN_300`** (set `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=n`,
  `CONFIG_ESP32P4_REV_MIN_300=y`). Drops the
  `--enable-non-contiguous-regions` flag entirely → clean link, clean
  build on a v3+ board. **The cluster's board is v1.3** (`esptool
  chip_id` confirms). The 2nd-stage bootloader refuses to boot an
  image with `REV_MIN_300` on v1.3 silicon, so this is a non-starter
  unless the hardware is swapped.
- **`SPI_FLASH_AUTO_SUSPEND=y` + `SPI_FLASH_PLACE_FUNCTIONS_IN_IRAM=n`**.
  This is the IDF-blessed way to legally push spi_flash op code out of
  IRAM (the `#error` in `esp_flash_spi_init.c:41` enforces the pairing).
  Builds clean. Boot panic at
  `assert failed: __esp_system_init_fn_init_flash esp_flash_spi_init.c:665`
  with `E (1284) spi_flash: Suspend and resume may not supported for
  this flash model yet.` — **the flash chip on the Waveshare module
  does not implement the SUS1/SUS2 suspend commands**, and IDF asserts
  rather than silently downgrading.
- **A custom linker fragment forcing the discarded sections to flash
  without `AUTO_SUSPEND`**. Would compile but be unsound: any NVS write
  runs spi_flash op code with cache disabled, and would crash if that
  code is in flash-XIP-only memory. Not viable.

## What did ship

The IRAM shortfall is 4388 bytes. Map analysis (`idf_size.py --archives`
on a known-good `REV_MIN_300` build) revealed `liblvgl__lvgl.a` was
contributing **118,584 bytes** to DIRAM through this project's *own*
section annotation:

```cmake
# (old) main/CMakeLists.txt
target_compile_definitions(${LVGL_LIB} PUBLIC
    "LV_ATTRIBUTE_FAST_MEM=__attribute__((section(\".iram1.lvgl_fast\")))"
)
```

The annotation existed to dodge a GCC 15 issue with LVGL's bundled
`IRAM_ATTR` macro (`__COUNTER__` was generating mismatched
`.iram1.N` section names between the header decl and the `.c` def).
But the choice of `.iram1.lvgl_fast` *kept* the code in IRAM, which is
exactly what we needed to give up. Redirecting to `.text.lvgl_fast`
(flash, via XIP through the L2 cache) preserves the GCC 15 fix while
freeing 116 KB of IRAM — 26× the shortfall.

There's a small secondary saving (~7 KB) from dropping the unused
vendor-specific spi_flash chip drivers via
`CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST=y` + providing
`default_registered_chips[]` in user code. Strictly, the LVGL fix
alone is enough; the chip-driver trim makes the underlying placement
margin healthier and removes 13 of the 63 discards in any future
ld 2.45 + non-contig regression scenario.

### Three required changes

**`main/CMakeLists.txt`**:

```diff
 idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
+# Keep LVGL fast-mem out of IRAM. On P4 <v3 with --enable-non-contiguous-regions,
+# 118 KB of LVGL .iram1.* sections crowded spi_flash out of placement and
+# triggered the ld 2.45 hang. Flash-XIP (.text.lvgl_fast) is fast enough
+# at 80 MHz QIO + 256 KB L2 cache for steady-state rendering. A stable
+# section name is still required to dodge GCC 15's IRAM_ATTR / __COUNTER__
+# mismatch — just point it at a flash section, not an IRAM section.
 target_compile_definitions(${LVGL_LIB} PUBLIC
-    "LV_ATTRIBUTE_FAST_MEM=__attribute__((section(\".iram1.lvgl_fast\")))"
+    "LV_ATTRIBUTE_FAST_MEM=__attribute__((section(\".text.lvgl_fast\")))"
 )

-set(_ANIM_GIF_H "${CMAKE_SOURCE_DIR}/managed_components/lvgl__lvgl/src/libs/gif/AnimatedGIF/src/AnimatedGIF.h")
+# Pre-existing bug: this path was stale after LVGL flattened the directory.
+# The patch had been silently no-op'ing every build; the boot GIF was failing
+# to decode (MAX_WIDTH 480 < our 800-pixel GIF).
+set(_ANIM_GIF_H "${CMAKE_SOURCE_DIR}/managed_components/lvgl__lvgl/src/libs/gif/AnimatedGIF.h")
 if(EXISTS "${_ANIM_GIF_H}")
     file(READ "${_ANIM_GIF_H}" _content)
     ...
```

Also add `spi_flash` to `REQUIRES` (for the chip-driver-table header
include).

**`main/main.c`** — provide the override chip table:

```c
#include "esp_flash_chips/spi_flash_chip_driver.h"
#include "esp_flash_chips/spi_flash_chip_generic.h"

// CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST=y → IDF's default list isn't
// compiled or linked. Generic driver accepts any chip ID; that's enough
// for the Waveshare module's flash, and dropping winbond/mxic/issi/boya/th
// also drops them out of the IRAM placement competition.
const spi_flash_chip_t *default_registered_chips[] = {
    &esp_flash_chip_generic,
    NULL,
};
```

`app_main()` ordering also needs to change so the boot screen paints
*before* `bsp_display_brightness_set()` lights the panel (see the
"Boot sequence" section in `ARCHITECTURE.md` if it grows or write it
inline in main.c).

**`sdkconfig.defaults`** — add the BLE-enable block plus the trim
knob:

```
# --- BLE peripheral via esp_hosted VHCI on co-proc ESP32-C6 ---
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=n
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
# CONFIG_BT_NIMBLE_ROLE_CENTRAL is not set
# CONFIG_BT_NIMBLE_ROLE_OBSERVER is not set
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_NVS_PERSIST=y
# CONFIG_BT_NIMBLE_TRANSPORT_UART is not set
CONFIG_ESP_HOSTED_ENABLED=y
CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y

# Drop vendor-specific chip-driver registration; user main.c provides
# default_registered_chips[] with just the generic driver. Frees 13 of
# the 63 sections that would otherwise compete for IRAM placement on
# pre-v3 P4 silicon under --enable-non-contiguous-regions.
CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST=y
```

**Do not add** `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y` or
`CONFIG_FREERTOS_PLACE_SNAPSHOT_FUNS_INTO_FLASH=y` — those Kconfig
symbols no longer exist in IDF v6 and would only emit warnings on
every build. They were tried during the original investigation and
recorded in the earlier revision of this doc as if they had been
load-bearing; they weren't.

**Do not add** `CONFIG_SPI_FLASH_ROM_IMPL=y`,
`CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH=y`, the `add_compile_options(-msmall-data-limit=0)`
in top-level CMake, or the PSRAM XIP-off lines either. They were the
"IRAM relief" knobs the bisect doc had us reach for first. They
don't hurt, but they aren't necessary once the LVGL fast-mem fix is
in place. Keep `sdkconfig.defaults` minimal.

### Memory result (after fix)

| Region | Used | Total | % used |
|---|---:|---:|---:|
| DIRAM (internal SRAM, IRAM/DRAM aliased) | 109,628 B | 445,392 B | **24.6 %** |
| Flash (.text + .rodata + …) | 2,166,334 B | 8 MB partition | 25.8 % |
| App `.bin` | **2.24 MB** | 8 MB | 28 % |

DIRAM utilization is fully comfortable. The 4388-byte placement
shortfall has been replaced by 335 KB of free DIRAM.

## Pre-existing bugs surfaced during the bisect

1. **`main/CMakeLists.txt` AnimatedGIF MAX_WIDTH patch path stale.**
   The patch targeted `…/gif/AnimatedGIF/src/AnimatedGIF.h`; current
   LVGL keeps the file at `…/gif/AnimatedGIF.h`. The patch had been
   silently no-op'ing every build, and the boot GIF (800 px wide) was
   failing to decode (MAX_WIDTH 480 rejects). Fixed in the diff above.

2. **`main/main.c` boot sequence white-flashes the panel.**
   `bsp_display_backlight_on()` is implemented as
   `bsp_display_brightness_set(100)`. So *any* `brightness_set()` call
   lights the panel — including the early one that applied the user's
   saved value. While the framebuffer still held PSRAM init garbage
   (≈ 0xFFFF = white in RGB565), the panel flashed white at 30 % duty
   for ~hundreds of ms until the boot screen was painted. Fix:
   keep LEDC at duty 0 throughout init, paint black through all three
   triple-partial framebuffers, then `bsp_display_brightness_set()`
   once at the end.

3. **`main/ble/ble_peripheral.c` GATT table fails validation.**
   `ble_gatts_count_cfg()` returned `rc=3` (`BLE_HS_EINVAL`) because
   the TX characteristic had `access_cb = NULL`. NimBLE's
   `ble_gatts_chr_is_sane()` rejects entries with a null callback,
   *even* for notify-only characteristics. Fix: provide a stub
   `access_tx_cb` that returns `BLE_ATT_ERR_READ_NOT_PERMITTED`.

## Sandbox reproducer (still useful if the upstream bugs re-bite)

`/tmp/bleprph_test` was used as a fast iteration target — clone of
the esp_hosted `host_nimble_bleprph_host_only_vhci` example with our
cluster sources copied in. It boots and runs the cluster firmware
(including the fix above) end-to-end. Rebuild recipe:

```bash
cp -R "$IDF_PATH/../../managed_components/espressif__esp_hosted/examples/host_nimble_bleprph_host_only_vhci" /tmp/bleprph_test
cd /tmp/bleprph_test
# Apply the cluster's sdkconfig.defaults
# Copy main/{ble,phone,display,settings,sound,vehicle,simulator,assets,main.c,CMakeLists.txt} from this repo's main/
# Update main/idf_component.yml to add esp_hosted ^2.12 + esp_wifi_remote ^1.5 + lvgl/lvgl ^9
# Copy our partitions.csv
idf.py set-target esp32p4
idf.py build           # ~70 s clean, succeeds
```

To re-verify the ld 2.45 hang is still latent: revert `main/CMakeLists.txt`
to use `.iram1.lvgl_fast`, then `rm -rf build && idf.py build`. The
build will hit the 3-minute ceiling at step 2207/2210.

To re-verify the 2.43.1 explicit errors:

```bash
cd ~/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/riscv32-esp-elf/bin
# Backup
mv ld ld.15.2.orig ; mv ld.bfd ld.bfd.15.2.orig
# Swap in 2.43.1 (download
# riscv32-esp-elf-14.2.0_20260121-aarch64-apple-darwin.tar.xz first)
ln -s /tmp/alt_toolchain/riscv32-esp-elf/riscv32-esp-elf/bin/ld
ln -s /tmp/alt_toolchain/riscv32-esp-elf/riscv32-esp-elf/bin/ld.bfd
cd /tmp/bleprph_test && rm -rf build && idf.py build
# Fails fast with 63 errors, rc=2, in ~84s
```

Don't leave the toolchain swapped in — restore the 2.45 binaries when done.

## Addendum (July 2026): the esp_adc "crash" — RESOLVED (it was never esp_adc)

**Final root cause.** On P4 rev<v3 silicon, the ~274 KB of SRAM above
`APP_USABLE_DIRAM_END` (0x4ff3afc0) is ROM-startup stack/data territory
(`components/heap/port/esp32p4/memory_layout.c`); those two heap
regions (18 KB + 256 KB) only join the allocator *after both CPUs are
scheduling*. Until then, every internal allocation — meaning **every
task stack** — must fit in RETENT_RAM + RTCRAM + TCM ≈ **151 KB**.
This firmware's boot was consuming **~146.5 KB** of it, dominated by
esp_hosted's SDIO transport at WiFi sizing: a ~47.7 KB mempool + a
~40.1 KB queue allocation (TX/RX queues 20 deep × ~2 KB buffers) plus
4 × 5.4 KB hosted task stacks. Boot margin: **~2 KB**.

Linking esp_adc added ~600 bytes of statics — enough to push CPU1's
idle-task stack allocation (1536 B, `MALLOC_CAP_INTERNAL|8BIT`) into
failure → the `vApplicationGet*TaskMemory` assert loop. esp_adc was
the straw, not the load: the instrumented repro proved a build
*without* esp_adc dies identically once heap poisoning's few KB of
overhead are added.

**The fix** (in `sdkconfig.defaults`): the SDIO link to the C6 carries
BLE HCI only — WiFi is unused on the bike — so the queues don't need
WiFi sizing:

```
CONFIG_ESP_HOSTED_SDIO_TX_Q_SIZE=6
CONFIG_ESP_HOSTED_SDIO_RX_Q_SIZE=6
```

That returns ~56 KB to the pre-scheduler pool (margin ~2 KB → ~58 KB).
Verified on hardware: the worst-case build (esp_adc linked +
comprehensive heap poisoning) now boots clean, BLE directed
advertising up, and `boot complete` arrives at ~4.5 s instead of
~9.7 s. The bench screen's ADC voltage readout is restored.

**Diagnostics kept in-tree** (`CONFIG_VROD_ADC_REPRO` +
`sdkconfig.adc-repro`): forces esp_adc's constructor into the link,
probes heap integrity pre-ctor, and dumps per-heap state + every block
on a failed allocation. Re-run it after IDF/toolchain bumps or when
adding boot-time components — it is the canary for this margin.
Anything that must allocate large internal buffers should do so from
`app_main` or later, never from a constructor or core-init function.

The original (superseded) investigation notes follow for reference.

### Original investigation notes (superseded by the above)

Adding `esp_adc` to the build (for a bench voltage readout on the
J1850 RX pin) produced a **deterministic pre-`app_main` crash loop**:

```
assert failed: vApplicationGetTimerTaskMemory port_common.c:97 (pxStackBufferTemp != NULL)
```

every cycle, right after the esp_hosted/sleep_gpio init prints —
despite `idf.py size` showing 313 KB of DIRAM headroom and heap_init
reporting ~380 KB free moments earlier. Bisect results (clean rebuilds
each step):

| Build | Result |
|---|---|
| sniffer build, no esp_adc | ✅ boots |
| + `adc_oneshot` + `adc_cali` | ❌ assert loop |
| + `adc_oneshot` only (nominal mV scaling) | ❌ assert loop |
| esp_adc in REQUIRES but no calls (archive GC'd) | ✅ boots |

The failing alloc is a few KB with hundreds of KB nominally free
(heap_init reports ~380 KB internal moments earlier), so this is not
exhaustion. Static analysis narrowed it further:

- The assert fires at **scheduler start** (`vTaskStartScheduler` →
  `xTimerCreateTimerTask` → `pvPortMalloc` returns NULL) — before any
  project code runs.
- The only esp_adc code that *executes* before that point is its
  **global constructor**: `adc_hw_calibration()` in
  `esp_adc/adc_common.c`, which runs at `do_global_ctors` time and does
  real analog-domain work — `ANALOG_CLOCK_ENABLE()` (ref-counted
  analog-I2C-master clock, which the bootloader deliberately leaves
  always-on and the ctor's ENABLE/DISABLE pair can hard-gate OFF),
  SAR power acquire/release, efuse calibration reads, and possibly
  full self-calibration.
- A build with esp_adc in REQUIRES but no references (archive
  garbage-collected, constructor never linked) **boots fine** — it is
  the constructor's presence, not the component's code size or
  placement, that correlates with the failure.
- Consistent with (but not identical to) upstream
  [IDFGH-15337 / esp-idf#15996] "TimerTask stack allocated in TCM
  crashes" — same neighbourhood: P4's exotic heap regions (RETENT_RAM,
  RTCRAM, TCM/SPM) interacting with early task-stack allocation.

The exact corruption mechanism needs one instrumented run on the
device. The repro kit is checked in and ready:

```sh
cd firmware
idf.py -B build-adc-repro \
       -DSDKCONFIG=build-adc-repro/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.adc-repro" \
       -p /dev/cu.usbmodem5B5F0299541 build flash
../tools/j1850_capture.py --reset -o /tmp/adc-repro.log
```

`main/adc_repro.c` (behind `CONFIG_VROD_ADC_REPRO`) forces the
constructor into the link and probes heap integrity right before it
runs; the overlay enables comprehensive heap poisoning,
abort-on-failed-alloc, and halt-on-panic. The one crash dump then
says which of three worlds we're in: heap corrupted by the ctor
(poisoning prints where), allocation legitimately unsatisfiable
(abort prints the caps), or corruption that predates esp_adc
entirely (pre-ctor probe already fails).

Interim state: the ADC readout was removed (`screen_bench.c` shows
level/edges/counters only; pin voltage stays a DMM job). **This
blocks Phase 6's fuel-level ADC** — run the repro after each
IDF/toolchain bump; the crash dump plus the analysis above is the
upstream bug report when it reproduces.

## Upstream issues to file

The cluster-side workaround stands on its own and doesn't require
either of these to be fixed first. They're still worth filing so
the next person hitting them doesn't have to repeat the bisect:

1. **`espressif/esp-idf` issue** — binutils 2.45 (shipped in toolchain
   `esp-15.2.0_20251204`) hangs forever under
   `-Wl,--enable-non-contiguous-regions` instead of erroring on
   section discards. 2.43.1 errors out fast on the same input.
   Provide the minimal repro from `/tmp/bleprph_test`.

2. **`espressif/esp-idf` issue** — `components/spi_flash/CMakeLists.txt`
   unconditionally compiles every chip driver
   (`spi_flash_chip_winbond.c`, `_mxic.c`, `_issi.c`, `_gd.c`,
   `_boya.c`, `_th.c`, `_mxic_opi.c`) regardless of whether the
   matching `SPI_FLASH_SUPPORT_*_CHIP` Kconfig is set. Their unused
   IRAM-marked code competes for placement under
   `--enable-non-contiguous-regions`, which is what tipped us over
   the IRAM budget on P4 < v3 silicon. Suggest gating each `.c` in
   CMake on its Kconfig the way other components do.

(`espressif/binutils-gdb` has Issues disabled; the `esp-idf` umbrella
is the right venue for the binutils-side bug.)

---

## Historical bisect table (kept for reference)

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
| 8 (continued bisect) | strip simulator/sound/settings/vehicle/display: only main+ble+phone | ✅ 23s, 670 KB |
| 9 | + simulator + vehicle (invoked) | ✅ 9s, 677 KB |
| 10 | + settings | ✅ 39s, 678 KB |
| 11 | + sound | ✅ 23s, 770 KB |
| 12 | + display/format + display/units | ✅ 24s, 770 KB |
| 13 | + `bsp_display_start_with_config()` call | ❌ ld hangs (the trigger) |
| 14 | only brightness + backlight, no `start_with_config` | ✅ 9s, 773 KB |
| 15 (rev) | switch `REV_MIN_300` (no `--enable-non-contiguous-regions`) | ✅ 84s, 1.25 MB (won't boot on v1.3) |
| 16 (LVGL fast-mem to flash) | revert to v1.3 silicon, `.iram1.lvgl_fast` → `.text.lvgl_fast` | ✅ 64s, 2.24 MB, **boots and runs** |
