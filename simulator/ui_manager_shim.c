// Desktop stand-in for main/display/ui_manager.c. The firmware version
// hooks into bsp_display_lock and spawns a FreeRTOS task; on the desktop
// those don't exist (and don't need to — the SDL main loop already pumps
// LVGL and the sim runs on its own pthread). This shim also stands in for
// settings_store (no NVS on the desktop — defaults only, kept in memory)
// and bsp_display_brightness_set (no-op).

#include "ui_manager.h"
#include "screen_ride.h"
#include "screen_settings.h"
#include "settings.h"
#include "settings_store.h"
#include "sound.h"
#include "lvgl.h"
#include <stdbool.h>

static lv_obj_t  *s_ride     = NULL;
static lv_obj_t  *s_settings = NULL;
static settings_t s_current;

// --- settings_store shim ---------------------------------------------------
// NVS doesn't exist on the desktop. Settings live for the lifetime of the
// process and reset on relaunch.
void settings_store_init(void)
{
    settings_default(&s_current);
}

const settings_t *settings_store_current(void)
{
    return &s_current;
}

bool settings_store_apply(const settings_t *s)
{
    s_current = *s;
    settings_validate(&s_current);
    return true;
}

// --- bsp shim --------------------------------------------------------------
// No physical backlight on the desktop. SDL window opacity could be wired
// here if we ever need visual feedback for brightness, but it's not worth
// the divergence from the real device behavior right now.
int bsp_display_brightness_set(int brightness_percent)
{
    (void)brightness_percent;
    return 0;
}

// --- sound shim ------------------------------------------------------------
// No audio on the desktop sim. SDL_audio could be wired here later if the
// click rhythm itself needs visual tuning, but for now silence is fine —
// the on-device feedback is the source of truth.
void sound_init(void)                          {}
void sound_play_turn_click(void)               {}
void sound_set_enabled(bool enabled)           { (void)enabled; }
void sound_set_volume (uint8_t pct)            { (void)pct; }

// --- ui_manager shim -------------------------------------------------------
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
    settings_store_init();
    ui_manager_show_ride();
}
