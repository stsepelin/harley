#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_flash_chips/spi_flash_chip_driver.h"
#include "esp_flash_chips/spi_flash_chip_generic.h"
#include "lvgl.h"

#include "ble_peripheral.h"
#include "boot_screen.h"
#include "emoji_font.h"
#include "phone_data.h"
#include "vehicle_data.h"
#include "sim_engine.h"
#include "settings_store.h"
#include "sound.h"

// CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST=y disables IDF's built-in
// chip-driver registration table so the vendor-specific drivers
// (winbond/mxic/issi/boya/th) drop out of the IRAM placement competition
// on P4 < v3 silicon. Generic driver accepts any chip ID and is enough for
// the Waveshare module's flash. See docs/ble-bringup-bisect.md.
const spi_flash_chip_t *default_registered_chips[] = {
    &esp_flash_chip_generic,
    NULL,
};

static const char *TAG = "vrod_gauge";

void app_main(void)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    bsp_display_start_with_config(&cfg);
    settings_store_init();
    // FreeType + memfs are up by the time bsp_display_start has called
    // lv_init. Attach the emoji fallback before any widgets get created
    // so the very first frame can render emoji correctly.
    emoji_font_init();

    // Data sources used by ui_manager_show_ride() at boot-hand-off must
    // exist before boot_screen_show(); otherwise a GIF decode failure
    // would fire the safety timer's fast-path into the ride screen
    // against an uninitialised vehicle_data store.
    vehicle_data_init();
    phone_data_init();
    sim_engine_start();

    // Backlight strategy. bsp_display_brightness_init() leaves the LEDC
    // channel at duty 0 (backlight off). bsp_display_brightness_set() and
    // bsp_display_backlight_on() are the *same* function — the latter just
    // calls the former with 100. Any brightness change lights the panel.
    // A naive "set brightness early, then init, then boot screen" sequence
    // flashed white at the user's saved duty: the framebuffer still held
    // PSRAM init garbage (≈ 0xFFFF / white in RGB565) when the panel lit
    // up. Instead: keep duty at 0 through init, paint black through all
    // three triple-partial framebuffers, then put the GIF on top, then
    // light the panel in one step at the saved brightness.
    bsp_display_lock(-1);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_invalidate(scr);
        lv_refr_now(NULL);
        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(20));
        bsp_display_lock(-1);
    }
    boot_screen_show();
    lv_refr_now(NULL);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(20));
    bsp_display_brightness_set(settings_store_current()->brightness);

    // Non-display init continues in parallel with the boot animation
    // (LVGL task is pinned to core 1, the calls below run on core 0).
    sound_init();
    sound_set_volume(settings_store_current()->volume);
    sound_set_enabled(settings_store_current()->sound_enabled);
    ble_peripheral_init();

    ESP_LOGI(TAG, "boot complete");
    // app_main can return — all the real work runs in the FreeRTOS tasks
    // we spawned (ui_update_task, event_watcher_task, sim_engine_task,
    // NimBLE host, LVGL render). The previous busy-log loop here would
    // also block any future TWDT IDLE-task feed on core 0.
}
