#pragma once
#include <stdint.h>

// Display units for speed and distance readouts. Persisted to NVS as a
// uint8_t (see settings_store), so the enumerator values are part of the
// on-flash format — don't reorder.
typedef enum {
    UNITS_KPH = 0,
    UNITS_MPH = 1,
} display_units_t;

// Temperature display unit. Persisted to NVS as a uint8_t (see settings_store),
// so the enumerator values are part of the on-flash format — don't reorder.
typedef enum {
    UNITS_CELSIUS    = 0,
    UNITS_FAHRENHEIT = 1,
} temp_units_t;

// Vehicle data is published in mph (speed) and metres (distance). These
// helpers convert at the very last step, in the widget, so the producer
// side and the cache logic stay unit-agnostic.

// Speed in mph → displayed value (mph, or km/h when metric). Rounded to
// the nearest whole.
uint16_t units_speed_display(uint16_t mph, display_units_t units);

// Metres → whole displayed unit (km or mi), truncated. Used by the
// odometer where we only render an integer.
uint32_t units_distance_whole(uint32_t meters, display_units_t units);

// Metres → tenths of the displayed unit (0.1 km or 0.1 mi), truncated.
// Used by the trip counters to drive "12.3" formatting.
uint32_t units_distance_tenths(uint32_t meters, display_units_t units);

// Engine temp in °C → displayed value (°C, or °F when imperial), rounded.
int units_temp_display(int celsius, temp_units_t units);

// Fuel economy from cumulative consumption ticks + distance, in the display
// unit x10 (68 = 6.8 L/100km; 345 = 34.5 mpg). 0 when the distance is too
// small to be meaningful. Uses a provisional mL/tick (ride 1) until a fill-up
// calibration firms up the ticks->litres factor.
uint32_t units_econ_x10(uint32_t fuel_ticks, uint32_t dist_m, display_units_t units);

// Static suffix labels. The returned pointer has program lifetime, so
// it's safe to hand directly to lv_label_set_text.
const char *units_speed_label(display_units_t units);
const char *units_distance_label(display_units_t units);
// Temp scale letter, "C" or "F" (the degree glyph is drawn by the widget).
const char *units_temp_label(temp_units_t units);
// Economy suffix, "L/100km" or "mpg".
const char *units_econ_label(display_units_t units);
