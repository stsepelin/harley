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

int units_temp_display(int celsius, temp_units_t units)
{
    if (units != UNITS_FAHRENHEIT)
        return celsius;
    // F = C*9/5 + 32, integer round-to-nearest (half the /5 divisor, signed).
    int nine = celsius * 9;
    return (nine >= 0 ? (nine + 2) / 5 : (nine - 2) / 5) + 32;
}

// Provisional fuel calibration until a fill-up locks it (ride-1-findings):
// ~0.025 mL per consumption tick. L/100km x10 = ticks * uL_per_tick / dist_m
// (litres = ticks*uL/1e6; L/100km = litres*1e5/dist_m).
#define FUEL_UL_PER_TICK 25u

uint32_t units_econ_x10(uint32_t fuel_ticks, uint32_t dist_m, display_units_t units)
{
    if (dist_m < 100u)
        return 0;  // too little distance to be meaningful
    uint32_t lp100_x10 = (uint32_t)((uint64_t)fuel_ticks * FUEL_UL_PER_TICK / dist_m);
    if (units != UNITS_MPH)
        return lp100_x10;
    if (lp100_x10 == 0)
        return 0;
    return 23522u / lp100_x10;  // mpg x10 = 235.215*100 / (L/100km x10)
}

const char *units_speed_label(display_units_t units)
{
    return (units == UNITS_MPH) ? "mph" : "km/h";
}

const char *units_distance_label(display_units_t units)
{
    return (units == UNITS_MPH) ? "mi" : "km";
}

const char *units_temp_label(temp_units_t units)
{
    return (units == UNITS_FAHRENHEIT) ? "F" : "C";
}

const char *units_econ_label(display_units_t units)
{
    return (units == UNITS_MPH) ? "mpg" : "L/100km";
}
