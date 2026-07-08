#include "gear_calc.h"
#include <stddef.h>

// Expected engine-rev-per-mph for each gear, x100 fixed point. Derived from the
// spec overall ratios (1st 10.969 / 2nd 7.371 / 3rd 5.9 / 4th 5.095 / 5th 4.563)
// times K = 13.15 rpm/mph (rear 240/40R18 ~2.04 m circumference, at the current
// speed divisor). Only K's magnitude is uncertain (~5%, tracks the speed cal);
// the ratios between gears are exact, so the band shape is right.
//   1st 144.3  2nd 96.9  3rd 77.6  4th 67.0  5th 60.0  (rpm/mph)
// Bands are the geometric midpoints between adjacent gears.
typedef struct {
    uint32_t lo, hi;  // rpm-per-mph x100, [lo, hi)
    gear_t   gear;
} band_t;

static const band_t BANDS[] = {
    {11826, 16590, GEAR_1}, {8672, 11826, GEAR_2}, {7211, 8672, GEAR_3},
    {6342, 7211, GEAR_4},   {5200, 6342, GEAR_5},
};
#define NBANDS (sizeof(BANDS) / sizeof(BANDS[0]))

// Below these the ratio is meaningless (creeping / clutch in) -> unknown.
#define GEAR_CALC_MIN_MPH 5u
#define GEAR_CALC_MIN_RPM 600u
// Boundary hysteresis: keep the previous gear while the ratio stays within its
// band widened by this margin (x100 rpm/mph), so it doesn't flicker at a shift
// point. 400 = 4 rpm/mph, comfortably inside the tightest gap (4th/5th ~7).
#define GEAR_CALC_HYST 400u

gear_t gear_calc(uint16_t rpm, uint16_t speed_mph, gear_t prev)
{
    if (speed_mph < GEAR_CALC_MIN_MPH || rpm < GEAR_CALC_MIN_RPM)
        return GEAR_UNKNOWN;

    uint32_t m = (uint32_t)rpm * 100u / speed_mph;  // rpm per mph, x100

    // Hysteresis: if the last gear's (widened) band still holds, stay there.
    for (size_t i = 0; i < NBANDS; i++)
        if (BANDS[i].gear == prev && m >= BANDS[i].lo - GEAR_CALC_HYST &&
            m < BANDS[i].hi + GEAR_CALC_HYST)
            return prev;

    for (size_t i = 0; i < NBANDS; i++)
        if (m >= BANDS[i].lo && m < BANDS[i].hi)
            return BANDS[i].gear;

    return GEAR_UNKNOWN;  // out of every band (slipping clutch / bad sample)
}
