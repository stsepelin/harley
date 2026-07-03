// Boot-margin regression canary (CONFIG_VROD_ADC_REPRO).
//
// History: linking esp_adc used to crash-loop this board before
// app_main. The instrumentation in this file found the real cause: on
// P4 rev<v3 the ~274 KB of SRAM above APP_USABLE_DIRAM_END joins the
// heap only after both CPUs schedule, so every startup task stack
// fights for a ~151 KB pool — and esp_hosted's WiFi-sized SDIO queues
// left ~2 KB of margin. Fixed via CONFIG_ESP_HOSTED_SDIO_TX/RX_Q_SIZE
// in sdkconfig.defaults; full story in docs/ble-bringup-bisect.md.
//
// Keep running this build (with sdkconfig.adc-repro) after IDF or
// toolchain bumps: it forces the heaviest known boot-time linkage and
// makes any future margin regression name itself —
//
//   - "CORRUPT HEAP" with an address  -> something shredded a heap
//   - clean abort in heap_caps_malloc  -> margin gone again; the block
//     dump in on_alloc_failed() shows who ate it
//   - pre-ctor probe already failing   -> corruption predates ctors

#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "esp_private/regi2c_ctrl.h"
#include "esp_private/sar_periph_ctrl.h"
#include "hal/adc_types.h"

// Referencing the symbol is what drags the archive (and its
// constructor) into the image; nothing here ever calls it.
static void *volatile s_force_esp_adc_link = (void *)&adc_oneshot_new_unit;

static void probe(const char *tag)
{
    esp_rom_printf("[adc-repro] %s: internal free %u\n", tag,
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// Runs inside heap_caps_malloc at the moment of failure — the state
// dump the bare "Mem alloc fail" line doesn't give us.
static void on_alloc_failed(size_t size, uint32_t caps, const char *fn)
{
    esp_rom_printf("[adc-repro] ALLOC FAILED %u bytes caps 0x%x in %s\n", (unsigned)size,
                   (unsigned)caps, fn);
    esp_rom_printf("[adc-repro]   free INTERNAL: %u  free INTERNAL|8BIT: %u\n",
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    esp_rom_printf(
        "[adc-repro]   largest INTERNAL|8BIT block: %u\n",
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    heap_caps_dump(MALLOC_CAP_INTERNAL);  // every block: address + size
}

// Priority 101 runs before default-priority constructors, i.e. before
// esp_adc's adc_hw_calibration. Replays that constructor's steps one at
// a time with heap measurements in between — whichever step eats the
// internal heap names itself. All calls are ref-counted, so the real
// constructor afterwards just bumps counts. esp_rom_printf is safe
// pre-scheduler.
static __attribute__((constructor(101))) void adc_repro_pre_ctor_probe(void)
{
    esp_rom_printf("[adc-repro] link anchor %p\n", s_force_esp_adc_link);
    heap_caps_register_failed_alloc_callback(on_alloc_failed);
    probe("baseline");
    esp_rom_printf("[adc-repro] caps breakdown: INTERNAL %u | INTERNAL|8BIT %u | DEFAULT %u | DMA "
                   "%u | 8BIT %u\n",
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    bool ok = heap_caps_check_integrity_all(true);
    esp_rom_printf("[adc-repro] heap integrity: %s\n", ok ? "OK" : "CORRUPT");

    ANALOG_CLOCK_ENABLE();
    probe("after ANALOG_CLOCK_ENABLE");

    sar_periph_ctrl_adc_oneshot_power_acquire();
    probe("after sar_periph power acquire");

    adc_calc_hw_calibration_code(ADC_UNIT_1, ADC_ATTEN_DB_12);
    probe("after calc calibration code (incl. efuse read)");

    sar_periph_ctrl_adc_oneshot_power_release();
    ANALOG_CLOCK_DISABLE();
    probe("after release + clock disable");
}

// Archive members with only constructors never get pulled out of the
// component .a — main.c calls this no-op so the linker keeps the object
// (and with it the probe ctor and the esp_adc reference).
void adc_repro_touch(void) {}
