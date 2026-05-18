#pragma once
#include "lvgl.h"

// Settings screen for the V-Rod cluster. Reached by long-pressing the
// ride screen; "Back" returns to ride. Content is intentionally minimal
// for Stage 1 — the actual setting rows (units, brightness, trip reset)
// land in Stage 2 with NVS persistence.
lv_obj_t *screen_settings_create(void);
