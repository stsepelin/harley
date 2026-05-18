// Desktop stand-in for main/display/ui_manager.c. The firmware version
// hooks into bsp_display_lock and spawns a FreeRTOS task; on the desktop
// those don't exist (and don't need to — the SDL main loop already pumps
// LVGL and the sim runs on its own pthread). This shim just gives
// screen_ride's long-press handler something to call when the user wants
// to switch screens.

#include "ui_manager.h"
#include "screen_ride.h"
#include "screen_settings.h"
#include "lvgl.h"

static lv_obj_t *s_ride     = NULL;
static lv_obj_t *s_settings = NULL;

void ui_manager_show_ride(void)
{
    if (!s_ride) s_ride = screen_ride_create();
    lv_screen_load(s_ride);
}

void ui_manager_show_settings(void)
{
    if (!s_settings) s_settings = screen_settings_create();
    lv_screen_load(s_settings);
}

void ui_manager_init(void)
{
    ui_manager_show_ride();
}
