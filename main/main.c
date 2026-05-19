#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "boot_screen.h"
#include "phone_data.h"
#include "vehicle_data.h"
#include "sim_engine.h"
#include "settings_store.h"
#include "sound.h"

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

    // Load persisted brightness and apply it BEFORE enabling the
    // backlight; otherwise the panel would flash at 100% for a beat
    // until we set the user-chosen duty cycle.
    settings_store_init();
    bsp_display_brightness_set(settings_store_current()->brightness);
    bsp_display_backlight_on();

    sound_init();
    sound_set_volume(settings_store_current()->volume);
    sound_set_enabled(settings_store_current()->sound_enabled);

    vehicle_data_init();
    phone_data_init();
    sim_engine_start();

    // boot_screen_show() plays the embedded GIF splash, then hands off
    // (on LV_EVENT_READY or the safety timer) to the ride screen and
    // starts the UI update task.
    bsp_display_lock(-1);
    boot_screen_show();
    bsp_display_unlock();

    ESP_LOGI(TAG, "boot complete, simulator running");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        vehicle_data_t d;
        vehicle_data_get(&d);
        size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "speed=%u km/h  rpm=%u  gear=%d  L=%d  PSRAM=%u/%uK (%.0f%%)",
                 d.speed_kmh, d.rpm, d.gear, d.turn_left,
                 (unsigned)((psram_total - psram_free) / 1024),
                 (unsigned)(psram_total / 1024),
                 100.0 * (double)(psram_total - psram_free) / (double)psram_total);
    }
}
