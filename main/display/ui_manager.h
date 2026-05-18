#pragma once

// Loads the gauge screen (replacing the boot splash) and starts a 30 FPS
// update task pinned to core 1 that pumps vehicle_data into the UI.
// Acquires the LVGL lock internally.
void ui_manager_init(void);

// Same as ui_manager_init() but assumes the LVGL lock is already held by the
// caller — used from boot_screen's lv_timer callback, which runs on the LVGL
// task with the lock implicitly held.
void ui_manager_show_ride(void);
