#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// On-disk record. Packed so a microSD-loaded blob in Phase 5 has the
// exact same layout — 12 bytes per camera. ~200 known fixed speed
// cameras in Estonia = 2.4 KB, ~10 K cameras across the Baltics = 120 KB.
// Heading is the direction the camera *catches* — i.e. cars driving
// roughly along this heading get pinged. 0xFFFF means "any direction"
// (cheap red-light cameras, fixed boxes that catch both ways).
typedef struct __attribute__((packed)) {
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint8_t  kind;         // poi_kind_t
    uint8_t  limit_kmh;    // posted speed limit, 0 if not a speed camera
    uint16_t heading_deg;  // 0..359 or 0xFFFF for omnidirectional
} poi_record_t;

typedef enum {
    POI_KIND_SPEED      = 0,
    POI_KIND_RED_LIGHT  = 1,
    POI_KIND_SECTION    = 2,    // average-speed section
    POI_KIND_LAST,
} poi_kind_t;

typedef struct {
    const poi_record_t *records;
    size_t                 count;
} poi_db_t;

// Wrap a raw byte buffer (microSD file contents or static array) as a
// db. Validates that `len` is a multiple of sizeof(poi_record_t) —
// returns false otherwise and leaves db zeroed. No copy: the caller
// keeps the buffer alive for the db's lifetime.
bool poi_db_open(poi_db_t *db, const uint8_t *buf, size_t len);

// Result of one query hit. Filled in distance-ascending order.
typedef struct {
    const poi_record_t *cam;
    uint32_t distance_m;
    int16_t  bearing_delta_deg;   // signed angle between rider heading
                                  // and bearing-to-camera, (-180,180]
} poi_hit_t;

// Find every camera within `radius_m` whose bearing-from-rider is
// within ±`bearing_tol_deg` of the rider's heading. Up to `out_cap`
// hits are written sorted by distance ascending; returns the count.
// Heading filter on the camera's own facing direction is also applied
// (cameras with heading_deg == 0xFFFF match any direction).
size_t poi_db_query(const poi_db_t *db,
                       int32_t lat_e7, int32_t lon_e7,
                       uint16_t heading_deg,
                       uint32_t radius_m,
                       int16_t  bearing_tol_deg,
                       poi_hit_t *out, size_t out_cap);
