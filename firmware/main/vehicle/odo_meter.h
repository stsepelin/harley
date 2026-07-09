#pragma once
#include <stdint.h>

// Odometer + dual trip totals, accumulated from the bus distance/fuel ticks.
// Pure state + operations (no NVS, no LVGL): odo_store owns the persistence and
// the driver owns the live instance. Distance is metres, fuel is raw ticks
// (mL/tick calibrated later; economy = trip_fuel/trip_m is computed downstream).
#define ODO_TRIP_COUNT 2

typedef struct {
    uint32_t odometer_m;              // lifetime distance; only set_odometer changes it directly
    uint32_t trip_m[ODO_TRIP_COUNT];  // per-trip distance, user-resettable
    uint32_t trip_fuel[ODO_TRIP_COUNT];
    uint32_t fuel_total;  // lifetime fuel ticks, never reset; paired with odometer_m so the
                          // display can derive current (windowed) economy
} odo_meter_t;

// Add a frame's distance (metres) and fuel (ticks) to the odometer and to both
// trips. A distance frame passes fuel_ticks 0 and vice-versa.
void odo_meter_add(odo_meter_t *m, uint32_t dist_m, uint32_t fuel_ticks);

// Zero one trip's distance + fuel. Out-of-range idx is ignored.
void odo_meter_reset_trip(odo_meter_t *m, int idx);

// Set the lifetime odometer (one-time per install; the bus carries no absolute
// mileage). Leaves the trips untouched.
void odo_meter_set_odometer(odo_meter_t *m, uint32_t meters);
