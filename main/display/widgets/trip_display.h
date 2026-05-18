#pragma once
#include "lvgl.h"
#include "units.h"
#include <stdint.h>

// `label` is the short caption shown before the distance, e.g. "TRIP1".
// The string is copied internally; the caller does not need to keep it alive.
lv_obj_t *trip_display_create(lv_obj_t *parent, const char *label);
void      trip_display_set(lv_obj_t *cont, uint32_t meters, display_units_t units);
