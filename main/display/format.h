#pragma once
#include <stdint.h>
#include <stddef.h>

// Pure-C numeric formatters used by the readout widgets. Extracted so they
// can be unit-tested on a host build without dragging LVGL into the picture.

// Writes `km` as a thousand-separated decimal into `out`, e.g.:
//   0        -> "0"
//   123      -> "123"
//   12847    -> "12,847"
//   1234567  -> "1,234,567"
// `out_size` must be >= 15 (enough for any uint32_t).
void format_km_grouped(uint32_t km, char *out, size_t out_size);

// Writes `meters` as kilometres with one decimal place into `out`, e.g.:
//   0      -> "0.0"
//   1234   -> "1.2"
//   47800  -> "47.8"
// Truncates, doesn't round (matches the trip-counter convention).
// `out_size` must be >= 16.
void format_km_tenth(uint32_t meters, char *out, size_t out_size);
