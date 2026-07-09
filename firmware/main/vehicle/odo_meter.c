#include "odo_meter.h"

void odo_meter_add(odo_meter_t *m, uint32_t dist_m, uint32_t fuel_ticks)
{
    m->odometer_m += dist_m;
    m->fuel_total += fuel_ticks;  // lifetime, not reset by a trip
    for (int i = 0; i < ODO_TRIP_COUNT; i++) {
        m->trip_m[i] += dist_m;
        m->trip_fuel[i] += fuel_ticks;
    }
}

void odo_meter_reset_trip(odo_meter_t *m, int idx)
{
    if (idx < 0 || idx >= ODO_TRIP_COUNT)
        return;
    m->trip_m[idx]    = 0;
    m->trip_fuel[idx] = 0;
}

void odo_meter_set_odometer(odo_meter_t *m, uint32_t meters)
{
    m->odometer_m = meters;
}
