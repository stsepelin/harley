#pragma once
#include "lvgl.h"
#include "poi_alert.h"

// Transient POI-alert popup. Top-anchored on the ride screen, shown for
// the duration of an active poi_alert and hidden otherwise. One short
// line of text — kind tag + distance + posted limit — sized to read at
// a glance without crowding the speed digits below.

lv_obj_t *poi_alert_popup_create(lv_obj_t *parent);

// Cache short-circuits when nothing has changed since the last frame —
// safe to call every UI tick.
void poi_alert_popup_update(lv_obj_t *popup, const poi_alert_t *alert);
