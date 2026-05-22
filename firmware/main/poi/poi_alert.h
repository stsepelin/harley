#pragma once
#include "poi_db.h"
#include "gps_source.h"

// Camera alert state machine. Stateful across ticks so the UI can render
// a single "active alert" that appears when a relevant camera enters the
// query cone and disappears once we've passed it (distance starts
// increasing past the alert threshold).
//
// Tunables hard-coded for first cut. If we ever expose them as settings,
// they go in settings.h with NVS persistence — but the defaults below
// match the typical 50-km/h-zone use case and the 500 m approach radius
// the SCDB databases publish for.
#define POI_ALERT_RADIUS_M       500
#define POI_ALERT_BEARING_TOL    45    // deg, ± from rider heading
#define POI_ALERT_DISMISS_M       50   // hysteresis past camera

typedef struct {
    bool                  active;
    const poi_record_t *cam;
    uint32_t              distance_m;
} poi_alert_t;

// Initialise the engine against a database. The pointer is borrowed;
// the caller keeps the db alive for the engine's lifetime.
void poi_alert_init(const poi_db_t *db);

// Feed one GPS sample. Updates the internal "active alert" state.
// Cheap enough to call at the GPS sample rate (1 Hz NEO-6M / 5 Hz M8N).
void poi_alert_tick(const gps_source_t *gps);

// Snapshot the current alert. `out->active = false` if no camera in
// the query cone.
void poi_alert_get(poi_alert_t *out);
