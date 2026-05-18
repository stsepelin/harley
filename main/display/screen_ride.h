#pragma once
#include "lvgl.h"
#include "settings.h"
#include "vehicle_data.h"

lv_obj_t *screen_ride_create(void);
void      screen_ride_update(const vehicle_data_t *data, const settings_t *settings);
