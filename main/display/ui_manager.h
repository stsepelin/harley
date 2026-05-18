#pragma once

// Loads the gauge screen (replacing the boot splash) and starts a 30 FPS
// update task pinned to core 1 that pumps vehicle_data into the UI.
// Acquires the LVGL lock internally.
void ui_manager_init(void);

// Switch to the ride screen. Lazy-creates the screen on first call and
// starts the 30 FPS update task. Assumes the LVGL lock is already held
// (true when called from an LVGL event / timer callback).
void ui_manager_show_ride(void);

// Switch to the settings screen. Lazy-creates on first call. Assumes
// the LVGL lock is already held.
void ui_manager_show_settings(void);
