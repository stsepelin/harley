#pragma once
#include "lvgl.h"

// Bluetooth sub-page of the settings screen. Reached by tapping the
// BLUETOOTH button on the main settings screen; BACK returns there.
// Hosts: connection status, BT VISIBILITY toggle, FORGET ALL DEVICES.
lv_obj_t *screen_settings_bluetooth_create(void);
