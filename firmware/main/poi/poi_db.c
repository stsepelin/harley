#include "poi_db.h"
#include "poi_math.h"
#include <string.h>

#define HEADING_ANY  0xFFFF

bool poi_db_open(poi_db_t *db, const uint8_t *buf, size_t len)
{
    if (!db) return false;
    memset(db, 0, sizeof(*db));
    if (!buf) return len == 0;
    if (len % sizeof(poi_record_t) != 0) return false;
    db->records = (const poi_record_t *)buf;
    db->count   = len / sizeof(poi_record_t);
    return true;
}

// Insertion sort into out[], keeping it ordered by distance ascending.
// Returns the new count after insertion.
static size_t insert_sorted(poi_hit_t *out, size_t count, size_t cap,
                            const poi_hit_t *hit)
{
    if (cap == 0) return 0;
    // Find the position where this hit's distance fits.
    size_t pos = count;
    while (pos > 0 && out[pos - 1].distance_m > hit->distance_m) pos--;
    if (pos >= cap) return count;   // dropped — caller's bucket is full
                                    //          and we're worse than every kept hit
    // Shift the slower-than-us tail right, dropping the last one if full.
    size_t end = (count < cap) ? count : cap - 1;
    for (size_t i = end; i > pos; i--) out[i] = out[i - 1];
    out[pos] = *hit;
    return (count < cap) ? count + 1 : cap;
}

size_t poi_db_query(const poi_db_t *db,
                       int32_t lat_e7, int32_t lon_e7,
                       uint16_t heading_deg,
                       uint32_t radius_m,
                       int16_t  bearing_tol_deg,
                       poi_hit_t *out, size_t out_cap)
{
    if (!db || !db->records || db->count == 0 || !out || out_cap == 0) return 0;

    size_t count = 0;
    for (size_t i = 0; i < db->count; i++) {
        const poi_record_t *c = &db->records[i];

        uint32_t d = poi_math_distance_m(lat_e7, lon_e7, c->lat_e7, c->lon_e7);
        if (d > radius_m) continue;

        uint16_t bearing  = poi_math_bearing_deg(lat_e7, lon_e7,
                                                    c->lat_e7, c->lon_e7);
        int16_t  bd       = poi_math_heading_delta(heading_deg, bearing);
        if (bd < -bearing_tol_deg || bd > bearing_tol_deg) continue;

        // Camera-facing filter: omnidirectional cameras always match;
        // directional ones only catch riders heading along their lane.
        if (c->heading_deg != HEADING_ANY) {
            int16_t fd = poi_math_heading_delta(heading_deg, c->heading_deg);
            if (fd < -bearing_tol_deg || fd > bearing_tol_deg) continue;
        }

        poi_hit_t hit = { .cam = c, .distance_m = d, .bearing_delta_deg = bd };
        count = insert_sorted(out, count, out_cap, &hit);
    }
    return count;
}
