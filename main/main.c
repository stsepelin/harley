#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "boot_screen.h"
#include "vehicle_data.h"
#include "sim_engine.h"

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
    bsp_display_backlight_on();

    bsp_display_lock(-1);
    boot_screen_show();
    bsp_display_unlock();

    vehicle_data_init();
    sim_engine_start();

    ESP_LOGI(TAG, "boot complete, simulator running");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        vehicle_data_t d;
        vehicle_data_get(&d);
        ESP_LOGI(TAG, "speed=%u km/h  rpm=%u  gear=%d  L=%d",
                 d.speed_kmh, d.rpm, d.gear, d.turn_left);
    }
}
