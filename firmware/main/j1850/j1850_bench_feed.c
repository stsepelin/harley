#include "sdkconfig.h"  // must precede the CONFIG_ guard below, or it reads as 0
#include "j1850_bench_feed.h"

#if CONFIG_VROD_J1850_BENCH_SPEED

#include "j1850_driver.h"
#include "j1850_vpw.h"
#include "vehicle_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "j1850_bench";

// SPEED frame header {0x48,0x29,0x10,0x02} + raw16 (HI,LO); raw 19500 -> 100 mph
// at the provisional /195 divisor. RPM frame {0x28,0x1B,0x10,0x02} + rpm16/4.
static const uint8_t SPEED_PAYLOAD[] = {0x48, 0x29, 0x10, 0x02, 0x4C, 0x2C};  // raw 19500
static const uint8_t RPM_PAYLOAD[]   = {0x28, 0x1B, 0x10, 0x02, 0x14, 0x00};  // 0x1400 / 4

// Build a driver-acceptable frame: payload + appended CRC, crc_ok set (feed()
// drops !crc_ok frames). Mirrors the host test's frame() helper.
static j1850_frame_t make_frame(const uint8_t *payload, size_t n)
{
    j1850_frame_t f;
    memset(&f, 0, sizeof(f));
    memcpy(f.data, payload, n);
    f.data[n] = j1850_crc(payload, n);
    f.len     = n + 1;
    f.crc_ok  = true;
    return f;
}

static void bench_task(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "BENCH speed/rpm injector active - fabricated vehicle data, bench only");
    uint32_t tick = 0;
    for (;;) {
        j1850_frame_t speed = make_frame(SPEED_PAYLOAD, sizeof(SPEED_PAYLOAD));
        j1850_frame_t rpm   = make_frame(RPM_PAYLOAD, sizeof(RPM_PAYLOAD));
        j1850_driver_feed(&speed);
        j1850_driver_feed(&rpm);
        // Read back every ~2 s so serial shows the live divisor recompute when
        // the phone writes a new speed divisor over BLE.
        if ((tick++ % 4) == 0) {
            vehicle_data_t vd;
            vehicle_data_get(&vd);
            ESP_LOGI(TAG, "bench: speed_raw=%u mph=%u rpm=%u div=%u", vd.speed_raw, vd.speed_mph,
                     vd.rpm, j1850_driver_speed_divisor());
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void j1850_bench_feed_start(void)
{
    xTaskCreate(bench_task, "j1850_bench", 3072, NULL, 4, NULL);
}

#else
void j1850_bench_feed_start(void) {}
#endif
