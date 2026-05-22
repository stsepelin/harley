#pragma once
#include "lvgl.h"
#include <stdint.h>

// Main RPM tachometer: 270 degree scale (bottom-left around top to bottom-right)
// with tick marks, zoom-on-cursor labels, and a baked Gaussian cursor sprite
// riding the bezel edge. Center is left empty so a speed_display can be
// overlaid.
lv_obj_t *tach_arc_create(lv_obj_t *parent);
void tach_arc_set_value(lv_obj_t *cont, uint16_t rpm);

// Optional: bake the heavy 800x800 background sprite + cursor sprites
// ahead of tach_arc_create(). Currently unused — calling it from
// app_main before bsp_display_lock(-1) caused a boot loop, so the
// hook is parked here until a safer placement (e.g. inside the boot
// screen GIF playback path) is wired up. The intent is to overlap
// the bake (~tens of ms in PSRAM) with the boot animation rather
// than the boot→ride transition. Idempotent — first call does the
// work, subsequent calls are no-ops. Safe to skip; tach_arc_create()
// does the same baking on first run if it hasn't been done yet.
void tach_arc_prebake(void);
