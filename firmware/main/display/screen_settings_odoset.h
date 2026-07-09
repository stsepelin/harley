#pragma once
#include "lvgl.h"

// Set-odometer sub-page (reached from the Trip page's odometer row). A stepper:
// pick a step size, +/- to dial the value, SAVE writes it to the odometer.
// Recreated on each entry so it starts from the current mileage.
lv_obj_t *screen_settings_odoset_create(void);
