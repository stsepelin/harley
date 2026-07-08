#pragma once
#include "lvgl.h"

// Trip settings sub-page: lifetime odometer (tap to set), TRIP1 / TRIP2
// distance + economy with per-trip RESET. Reached from the settings menu;
// BACK returns there. Values refresh live from vehicle_data.
lv_obj_t *screen_settings_trip_create(void);
