#pragma once

// Loads the gauge screen (replacing the boot splash) and starts a 30 FPS
// update task pinned to core 1 that pumps vehicle_data into the UI.
// Acquires the LVGL lock internally.
void ui_manager_init(void);

// Switch to the ride screen. Lazy-creates the screen on first call and
// starts the 30 FPS update task. Assumes the LVGL lock is already held
// (true when called from an LVGL event / timer callback).
void ui_manager_show_ride(void);

// Switch to the settings menu. Lazy-creates on first call. Assumes the
// LVGL lock is already held.
void ui_manager_show_settings(void);

// Settings sub-pages, reached from the menu; BACK on each returns to the
// menu. Lazy-created; assume the LVGL lock is held.
void ui_manager_show_settings_general(void);  // units / sound / brightness
void ui_manager_show_settings_trip(void);     // odometer / trips / reset
void ui_manager_show_settings_odoset(void);   // set the odometer value
void ui_manager_show_settings_bluetooth(void);

#if CONFIG_VROD_J1850_SNIFFER
// Bench diagnostics sub-page (sniffer builds only). Lazy-creates on
// first call. Assumes the LVGL lock is already held.
void ui_manager_show_bench(void);
#endif
