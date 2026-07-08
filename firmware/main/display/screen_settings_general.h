#pragma once
#include "lvgl.h"

// General settings sub-page: units (km/mi + C/F), sound (on/off + volume),
// brightness. Reached from the settings menu; BACK returns there.
lv_obj_t *screen_settings_general_create(void);
