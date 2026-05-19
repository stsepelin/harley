#pragma once
#include "lvgl.h"
#include "phone.h"

// Single-line info-slot widget for the currently-playing track.
// Lays out the same as clock_display / trip_display so screen_ride can
// drop it into the rotation when media is playing.
lv_obj_t *now_playing_display_create(lv_obj_t *parent);
void      now_playing_display_set(lv_obj_t *cont, const now_playing_t *np);
