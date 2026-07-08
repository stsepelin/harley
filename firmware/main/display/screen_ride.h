#pragma once
#include "lvgl.h"
#include "settings.h"
#include "vehicle_data.h"

lv_obj_t *screen_ride_create(void);
void      screen_ride_update(const vehicle_data_t *data, const settings_t *settings);

// Info slot (clock / odo / trip1 / trip2). Tapping it advances to the next
// readout — no auto-cycling. screen_ride_info_hit reports whether a screen
// point (x,y) lands on the slot; screen_ride_cycle_info advances the mode.
// Both are safe to call from the input task: cycle only flips an int (the
// visibility/value is applied by screen_ride_update under the LVGL lock), and
// hit reads a cached rectangle, so neither touches LVGL.
bool screen_ride_info_hit(int x, int y);
void screen_ride_cycle_info(void);
