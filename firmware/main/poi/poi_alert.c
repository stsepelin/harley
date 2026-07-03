#include "poi_alert.h"
#include "poi_math.h"
#include <string.h>

// Single-alert design: only the closest in-cone camera is tracked.
// Multi-camera arrays could replace this if we ever drive through a
// gauntlet of red-light cameras, but the typical case is one at a time
// and the UI only has room for one popup anyway.

static const poi_db_t *s_db;
static poi_alert_t     s_alert;

// Hysteresis: once a camera is the active alert, we keep it active until
// the rider is past it by at least POI_ALERT_DISMISS_M, even if the
// bearing-from-rider flips to "behind us". That way a fast pass doesn't
// flicker the alert on the same camera.

void poi_alert_init(const poi_db_t *db)
{
    s_db = db;
    memset(&s_alert, 0, sizeof(s_alert));
}

void poi_alert_tick(const gps_source_t *gps)
{
    if (!s_db || !gps) return;
    if (!gps->fix_ok) {
        s_alert.active = false;
        s_alert.cam    = NULL;
        return;
    }

    // If we already have an active alert, check whether we should keep
    // it (still close) or dismiss it (we've passed). Reading distance
    // directly here is cheaper than a full query when the alert is hot.
    // (cam is always non-NULL while active — both are set together below.)
    if (s_alert.active) {
        uint32_t d = poi_math_distance_m(gps->lat_e7, gps->lon_e7,
                                            s_alert.cam->lat_e7,
                                            s_alert.cam->lon_e7);
        s_alert.distance_m = d;
        if (d > POI_ALERT_RADIUS_M + POI_ALERT_DISMISS_M) {
            // Far enough past or off-route to call it done.
            s_alert.active = false;
            s_alert.cam    = NULL;
        }
    }

    // Either no active alert, or it's still active — either way we want
    // to know if a closer camera has come into the cone.
    poi_hit_t hits[1];
    size_t n = poi_db_query(s_db,
                               gps->lat_e7, gps->lon_e7, gps->heading_deg,
                               POI_ALERT_RADIUS_M, POI_ALERT_BEARING_TOL,
                               hits, 1);
    if (n == 0) return;

    // First hit is closest by virtue of poi_db_query's sort order.
    // Replace the active alert only if this is a different camera (or
    // we had none) — keeps the popup steady on the same icon while we
    // approach.
    if (!s_alert.active || s_alert.cam != hits[0].cam) {
        s_alert.active     = true;
        s_alert.cam        = hits[0].cam;
        s_alert.distance_m = hits[0].distance_m;
    } else {
        s_alert.distance_m = hits[0].distance_m;
    }
}

void poi_alert_get(poi_alert_t *out)
{
    if (!out) return;
    *out = s_alert;
}
