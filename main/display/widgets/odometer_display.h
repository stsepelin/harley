#pragma once
#include "lvgl.h"
#include "units.h"
#include <stdint.h>

lv_obj_t *odometer_display_create(lv_obj_t *parent);
void      odometer_display_set(lv_obj_t *cont, uint32_t meters, display_units_t units);
