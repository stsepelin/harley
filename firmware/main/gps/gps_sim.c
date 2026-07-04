#include "gps_sim.h"
#include "gps_source.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define TICK_MS        200            // 5 Hz, matches M8N max rate
#define SPEED_KMH      50             // loop angular velocity (physics stays metric)
#define SPEED_MPH      31             // 50 km/h in mph, stored to gps_source
#define TWO_PI         6.28318530718f
#define HALF_PI        1.57079632679f
#define RAD_TO_DEG     (180.0f / 3.14159265f)

// One degree of latitude in metres is ~111 km regardless of where you
// are. One degree of longitude is ~111 km * cos(lat). At Tallinn lat,
// cos(lat) ≈ 0.51, so a 200 m radius circle needs lat_span ≈ 0.0018°
// and lon_span ≈ 0.0035°.
//
// Single-precision throughout — the ESP32-P4 has hardware FP for floats
// only; doubles are software-emulated and would burn CPU on core 0
// every 200 ms next to LVGL / NimBLE / ESP-Hosted.
#define DEG_PER_M_LAT  (1.0f / 111111.0f)
#define COS_TALLINN    0.5076f

static void gps_sim_task(void *arg)
{
    (void)arg;
    const float radius_lat_e7 = GPS_SIM_RADIUS_M * DEG_PER_M_LAT * 1.0e7f;
    const float radius_lon_e7 = GPS_SIM_RADIUS_M * DEG_PER_M_LAT * 1.0e7f / COS_TALLINN;
    const float dtheta        = (TICK_MS * 0.001f) * (SPEED_KMH / 3.6f) / GPS_SIM_RADIUS_M;

    float    theta = 0.0f;
    uint32_t t     = 0;
    for (;;) {
        // Position on the circle (clockwise from north so heading
        // increases monotonically as we go around).
        float lat = GPS_SIM_CENTER_LAT_E7 + radius_lat_e7 * cosf(theta);
        float lon = GPS_SIM_CENTER_LON_E7 + radius_lon_e7 * sinf(theta);
        // Tangent direction: heading is theta + 90°, normalised to 0..359.
        float heading_rad = theta + HALF_PI;
        float heading_deg = fmodf(heading_rad * RAD_TO_DEG + 360.0f, 360.0f);

        gps_source_t g = {
            .lat_e7      = (int32_t)lat,
            .lon_e7      = (int32_t)lon,
            .speed_mph   = SPEED_MPH,
            .heading_deg = (uint16_t)heading_deg,
            .fix_ok      = true,
            .time_ms     = t,
        };
        gps_source_set(&g);

        theta += dtheta;
        if (theta > TWO_PI) theta -= TWO_PI;
        t += TICK_MS;
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

void gps_sim_start(void)
{
    // Pinned to core 0 alongside the other producers (sim_engine,
    // event_watcher). Core 1 belongs to LVGL.
    xTaskCreatePinnedToCore(gps_sim_task, "gps_sim", 4096, NULL, 4, NULL, 0);
}
