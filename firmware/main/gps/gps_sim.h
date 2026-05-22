#pragma once

// Spawn a FreeRTOS task (via the pthread shim on the desktop sim) that
// walks a canned 200 m-radius circular route around a fixed centre at
// ~50 km/h, publishing gps_source updates every 200 ms. Calibrated so a
// POI placed at the centre triggers the alert popup roughly twice
// per minute — slow enough to feel realistic, fast enough that the
// rider sees the full approach → present → dismiss cycle without
// leaving the bench.
//
// Gated by CONFIG_VROD_INCLUDE_SIM_ENGINE — when off (real-bike build),
// Phase 5's NEO-6M NMEA parser becomes the gps_source producer instead.
void gps_sim_start(void);

// The centre of the canned route — exposed so the test POI DB can
// place its records nearby.
#define GPS_SIM_CENTER_LAT_E7   594370000   // Tallinn city centre
#define GPS_SIM_CENTER_LON_E7   247536000
#define GPS_SIM_RADIUS_M        200
