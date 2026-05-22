#pragma once
#include <stdint.h>

// Pure geodesic helpers shared by poi_db (query) and poi_alert
// (state transitions). Kept out of those modules so they can be unit-
// tested on host without pulling in LVGL / FreeRTOS, and so the floating-
// point math is paid for once per poi_db_query rather than scattered.
//
// All inputs are 1e-7 degree integers; outputs are real-world units
// (meters / degrees) computed via the equirectangular approximation.
// Accurate to better than 1 m for the distances we care about
// (POI alert radius is sub-kilometer).

// Great-circle distance in metres between two points. Symmetric.
uint32_t poi_math_distance_m(int32_t lat_a_e7, int32_t lon_a_e7,
                                int32_t lat_b_e7, int32_t lon_b_e7);

// Initial bearing from point a to point b in degrees, 0..359 from true
// north (north = 0, east = 90). Returns 0 when the two points coincide.
uint16_t poi_math_bearing_deg(int32_t lat_a_e7, int32_t lon_a_e7,
                                 int32_t lat_b_e7, int32_t lon_b_e7);

// Signed difference between two compass headings, normalised to
// (-180, 180]. e.g. heading_delta(10, 350) = -20 (350 is 20° behind 10).
int16_t poi_math_heading_delta(uint16_t heading_a_deg,
                                  uint16_t heading_b_deg);
