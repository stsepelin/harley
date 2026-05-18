#pragma once
#include "lvgl.h"
#include "units.h"
#include <stdint.h>

// Big digital speed readout + unit subtitle, designed to be centered inside
// the tach_arc. `kmh` is always metric — the widget converts on display
// according to `units`, and re-labels its subtitle on unit change.
lv_obj_t *speed_display_create(lv_obj_t *parent);
void speed_display_set_value(lv_obj_t *cont, uint16_t kmh, display_units_t units);
