#pragma once
#include "lvgl.h"

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

// Register the map screen once the map module has built it. Called by the map
// driver from inside map_*_load().
void ui_manager_set_map_screen(lv_obj_t *map);

// Show the driving view chosen in settings (classic gauge or moving map). Used
// at the boot hand-off and whenever the user leaves the settings menu; the map
// is lazy-loaded here on first use so a classic setting never loads it. Ensures
// the ride screen + update tasks exist regardless of which view is shown.
void ui_manager_show_home(void);

// Request a re-apply of the saved layout from another task (e.g. the BLE host
// task when the phone pushes a layout change). Deferred to the UI task so the
// map load never runs on the radio task. Safe to call before the UI is up.
void ui_manager_request_home(void);

// True once the map screen has been built (a successful map_load). Reported in
// telemetry so the companion knows whether the map view is actually working.
bool ui_manager_map_available(void);
