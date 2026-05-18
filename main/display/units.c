#include "units.h"

// 1 mile = 1609.344 m. Encoded as micrometres-per-mile × 1 so integer
// arithmetic preserves the .344 without floating-point.
#define UM_PER_MILE  1609344u

uint16_t units_speed_display(uint16_t kmh, display_units_t units)
{
    if (units != UNITS_MPH) return kmh;
    // mph = round(kmh × 1000 / 1609.344). Adding UM_PER_MILE/2 gives
    // round-to-nearest before integer division. Worst-case input is a
    // few hundred km/h, well clear of uint32 overflow.
    uint32_t scaled = (uint32_t)kmh * 1000000u + (UM_PER_MILE / 2u);
    return (uint16_t)(scaled / UM_PER_MILE);
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
