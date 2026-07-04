#pragma once
#include <stdbool.h>
#include <stdint.h>

// Producer-agnostic GPS snapshot. Filled by gps_sim during off-bike
// development, and by the NEO-6M / M8N NMEA parser in Phase 5 when the
// real module lands. Mirrors the vehicle_data pattern: a single
// mutex-guarded latest-value store; consumers grab a snapshot.
//
// Coordinates are integer-encoded as 1e-7 degrees (the NMEA wire
// precision). This keeps the on-disk POI DB compact (int32 each)
// and avoids floating-point comparisons in the hot path; conversion
// to floats happens only inside poi_math during distance/bearing
// computation.
typedef struct {
    int32_t  lat_e7;        // signed, 1e-7 deg. Tallinn ≈ +594370000.
    int32_t  lon_e7;        // signed, 1e-7 deg. Tallinn ≈ +247536000.
    uint16_t speed_mph;     // ground speed, mph-canonical (see vehicle_data.h)
    uint16_t heading_deg;   // 0..359 from true north
    bool     fix_ok;        // false until first valid fix
    uint32_t time_ms;       // monotonic since boot; NMEA time after Phase 5
} gps_source_t;

void gps_source_init(void);
void gps_source_set(const gps_source_t *new_data);
void gps_source_get(gps_source_t *out);
