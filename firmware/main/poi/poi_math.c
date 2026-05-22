#include "poi_math.h"
#include <math.h>

// Single-precision throughout — the ESP32-P4 has hardware FP for floats
// only (RV32IMAFC); doubles are runtime-emulated and ~100× slower. At
// 30 Hz from the alert-tick loop on the UI core, that cost is the
// difference between a smooth gauge and a visibly choppy one. The
// sub-km accuracy required by the alert radius is well within float
// precision.
#define EARTH_RADIUS_M     6371000.0f
#define DEG_PER_E7         1.0e-7f
#define DEG_TO_RAD         (3.14159265358979323846f / 180.0f)
#define RAD_TO_DEG         (180.0f / 3.14159265358979323846f)

uint32_t poi_math_distance_m(int32_t lat_a_e7, int32_t lon_a_e7,
                                int32_t lat_b_e7, int32_t lon_b_e7)
{
    // Equirectangular approximation — exact great-circle (haversine) is
    // overkill for sub-km alert radii and the cos(mean_lat) latitude
    // correction is enough for accuracy comfortably under 1 m.
    float lat_a_rad  = (float)lat_a_e7 * DEG_PER_E7 * DEG_TO_RAD;
    float lat_b_rad  = (float)lat_b_e7 * DEG_PER_E7 * DEG_TO_RAD;
    float mean_lat   = 0.5f * (lat_a_rad + lat_b_rad);
    float dlat_rad   = (float)(lat_b_e7 - lat_a_e7) * DEG_PER_E7 * DEG_TO_RAD;
    float dlon_rad   = (float)(lon_b_e7 - lon_a_e7) * DEG_PER_E7 * DEG_TO_RAD;
    float x          = dlon_rad * cosf(mean_lat);
    float y          = dlat_rad;
    float d          = EARTH_RADIUS_M * sqrtf(x * x + y * y);
    if (d < 0.0f) d = 0.0f;
    if (d > (float)UINT32_MAX) d = (float)UINT32_MAX;
    return (uint32_t)(d + 0.5f);
}

uint16_t poi_math_bearing_deg(int32_t lat_a_e7, int32_t lon_a_e7,
                                 int32_t lat_b_e7, int32_t lon_b_e7)
{
    if (lat_a_e7 == lat_b_e7 && lon_a_e7 == lon_b_e7) return 0;

    float lat_a_rad = (float)lat_a_e7 * DEG_PER_E7 * DEG_TO_RAD;
    float lat_b_rad = (float)lat_b_e7 * DEG_PER_E7 * DEG_TO_RAD;
    float dlon_rad  = (float)(lon_b_e7 - lon_a_e7) * DEG_PER_E7 * DEG_TO_RAD;

    float y = sinf(dlon_rad) * cosf(lat_b_rad);
    float x = cosf(lat_a_rad) * sinf(lat_b_rad)
            - sinf(lat_a_rad) * cosf(lat_b_rad) * cosf(dlon_rad);
    float brg = atan2f(y, x) * RAD_TO_DEG;
    if (brg < 0.0f) brg += 360.0f;
    // atan2 + the +360 normalisation can theoretically yield exactly 360
    // in floating point — clamp to keep the contract "0..359".
    if (brg >= 360.0f) brg -= 360.0f;
    return (uint16_t)(brg + 0.5f);
}

int16_t poi_math_heading_delta(uint16_t heading_a_deg, uint16_t heading_b_deg)
{
    int32_t d = (int32_t)heading_b_deg - (int32_t)heading_a_deg;
    while (d > 180)  d -= 360;
    while (d <= -180) d += 360;
    return (int16_t)d;
}
