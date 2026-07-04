#include "units.h"

// 1 mile = 1609.344 m. Encoded as micrometres-per-mile × 1 so integer
// arithmetic preserves the .344 without floating-point.
#define UM_PER_MILE  1609344u

uint16_t units_speed_display(uint16_t mph, display_units_t units)
{
    if (units == UNITS_MPH)
        return mph;
    // km/h = round(mph × 1609.344 / 1000). Adding 500000 gives round-to-
    // nearest before the /1000000. Worst-case input is a few hundred mph,
    // well clear of uint32 overflow.
    uint32_t scaled = (uint32_t)mph * UM_PER_MILE + 500000u;
    return (uint16_t)(scaled / 1000000u);
}

uint32_t units_distance_whole(uint32_t meters, display_units_t units)
{
    if (units != UNITS_MPH) return meters / 1000u;
    // miles = meters / 1609.344, truncated. 64-bit intermediate so
    // anything below ~4 billion metres (~2.5M mi) is safe.
    return (uint32_t)(((uint64_t)meters * 1000u) / UM_PER_MILE);
}

uint32_t units_distance_tenths(uint32_t meters, display_units_t units)
{
    if (units != UNITS_MPH) return meters / 100u;
    return (uint32_t)(((uint64_t)meters * 10000u) / UM_PER_MILE);
}

const char *units_speed_label(display_units_t units)
{
    return (units == UNITS_MPH) ? "mph" : "km/h";
}

const char *units_distance_label(display_units_t units)
{
    return (units == UNITS_MPH) ? "mi" : "km";
}
