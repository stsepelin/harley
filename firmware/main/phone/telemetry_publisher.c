#include "telemetry_publisher.h"
#include "ble_peripheral.h"
#include "settings_store.h"
#include "telemetry_codec.h"
#include "ui_manager.h"
#include "vehicle_data.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// The cluster-state bits the phone needs to reflect the layout truthfully.
static uint8_t telemetry_status(void)
{
    uint8_t status = 0;
#if CONFIG_VROD_MAP_DEMO || CONFIG_VROD_MAP_SD
    status |= TELEMETRY_STATUS_MAP_SUPPORTED;
    if (settings_store_current()->layout == LAYOUT_MAP)
        status |= TELEMETRY_STATUS_LAYOUT_MAP;
    if (ui_manager_map_available())
        status |= TELEMETRY_STATUS_MAP_AVAILABLE;
#endif
    return status;
}

// 4 Hz: smooth enough for a phone dashboard and GPS-speed correlation, far
// below what the BLE link or vehicle_data mutex care about. RPM frames alone
// arrive ~15/s, so we're deliberately downsampling the shared snapshot rather
// than notifying per bus frame.
#define TELEMETRY_PERIOD_MS 250

static void telemetry_task(void *arg)
{
    (void)arg;
    for (;;) {
        vehicle_data_t vd;
        vehicle_data_get(&vd);

        uint8_t frame[TELEMETRY_FRAME_LEN];
        size_t  n = telemetry_encode(&vd, telemetry_status(), frame, sizeof(frame));
        if (n) {
            ble_peripheral_notify(frame, (uint16_t)n);
        }
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

void telemetry_publisher_start(void)
{
    // Core 0 alongside the other producers/radio work; low priority - a
    // dropped telemetry tick is cosmetic on the phone, never on the dash.
    xTaskCreatePinnedToCore(telemetry_task, "telemetry", 3072, NULL, 4, NULL, 0);
}
