#include "ui_manager.h"
#include "gesture.h"
#include "phone_data.h"
#include "screen_ride.h"
#include "screen_settings.h"
#include "screen_settings_bluetooth.h"
#include "settings_store.h"
#include "sound.h"
#include "vehicle_data.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

// Both screens are created lazily on first show and kept alive across
// switches — `lv_screen_load` just rewires the active screen. Off-screen
// widgets stay current because the update task keeps feeding them even
// when they're not visible (their internal caches mean the work is
// near-zero in that case).
static lv_obj_t *s_ride          = NULL;
static lv_obj_t *s_settings      = NULL;
static lv_obj_t *s_settings_bt   = NULL;
static bool      s_ui_started    = false;
static bool      s_event_started = false;

#define EVENT_POLL_MS   10        // 100 Hz event polling

static void ui_update_task(void *arg)
{
    (void)arg;
    while (1) {
        vehicle_data_t d;
        vehicle_data_get(&d);
        const settings_t *s = settings_store_current();

        bsp_display_lock(-1);
        screen_ride_update(&d, s);
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(33));   // ~30 FPS; matches LVGL render budget
    }
}

// Pinned to core 0 alongside the sim, NOT core 1 (LVGL). Detects events
// that need timing accuracy independent of the LVGL render budget:
//
//   - turn-signal edges → audio click
//   - long-press on the ride screen → switch to settings
//   - horizontal swipe → dismiss an active SMS/app notification
//
// Previously these lived inside the UI task which gates on the LVGL
// lock; under heavy frames (tach + shift-light + arrows all redrawing)
// the UI task would wait, starving both checks and producing a visible
// lag on every input. This task does the detection out-of-band at 100 Hz
// and only takes the LVGL lock for the brief screen swap.
// Map a classified swipe back to phone_data's enum. Centralised so both
// the firmware watcher and the desktop sim stay in lockstep.
static void dispatch_gesture(gesture_event_t e)
{
    switch (e) {
    case GESTURE_LONG_PRESS:
        bsp_display_lock(-1);
        ui_manager_show_settings();
        bsp_display_unlock();
        break;
    case GESTURE_SWIPE_LEFT:  phone_data_handle_swipe(PHONE_SWIPE_LEFT);  break;
    case GESTURE_SWIPE_RIGHT: phone_data_handle_swipe(PHONE_SWIPE_RIGHT); break;
    case GESTURE_SWIPE_UP:    phone_data_handle_swipe(PHONE_SWIPE_UP);    break;
    case GESTURE_SWIPE_DOWN:  phone_data_handle_swipe(PHONE_SWIPE_DOWN);  break;
    case GESTURE_NONE:
    default:                  break;
    }
}

static void event_watcher_task(void *arg)
{
    (void)arg;
    bool            prev_left  = false;
    bool            prev_right = false;
    gesture_state_t gesture;
    gesture_init(&gesture);

    while (1) {
        // --- Turn-signal edge → click ---
        vehicle_data_t d;
        vehicle_data_get(&d);
        if (d.turn_left != prev_left || d.turn_right != prev_right) {
            prev_left  = d.turn_left;
            prev_right = d.turn_right;
            sound_play_turn_click();
        }

        // --- Touch input → long-press / swipe ---
        // Only consume input while the ride screen is up; settings has
        // its own back-button and ignoring stale presses keeps a long
        // press in settings from bouncing right back here.
        lv_indev_t *indev   = lv_indev_get_next(NULL);
        bool        on_ride = (lv_screen_active() == s_ride);
        bool        pressed = false;
        lv_point_t  pt = { 0, 0 };
        if (indev && on_ride) {
            pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
            if (pressed) lv_indev_get_point(indev, &pt);
        }
        dispatch_gesture(gesture_update(&gesture, pressed, pt.x, pt.y, lv_tick_get()));

        vTaskDelay(pdMS_TO_TICKS(EVENT_POLL_MS));
    }
}

void ui_manager_show_ride(void)
{
    if (!s_ride) s_ride = screen_ride_create();
    lv_screen_load(s_ride);
    if (!s_ui_started) {
        xTaskCreatePinnedToCore(ui_update_task, "ui_upd", 8192, NULL, 4, NULL, 1);
        s_ui_started = true;
    }
    if (!s_event_started) {
        xTaskCreatePinnedToCore(event_watcher_task, "evt_watch", 4096, NULL, 5, NULL, 0);
        s_event_started = true;
    }
}

void ui_manager_show_settings(void)
{
    if (!s_settings) s_settings = screen_settings_create();
    lv_screen_load(s_settings);
}

void ui_manager_show_settings_bluetooth(void)
{
    if (!s_settings_bt)
        s_settings_bt = screen_settings_bluetooth_create();
    lv_screen_load(s_settings_bt);
}

#if CONFIG_VROD_J1850_SNIFFER
#include "screen_bench.h"
void ui_manager_show_bench(void)
{
    static lv_obj_t *s_bench = NULL;
    if (!s_bench)
        s_bench = screen_bench_create();
    lv_screen_load(s_bench);
}
#endif

void ui_manager_init(void)
{
    bsp_display_lock(-1);
    ui_manager_show_ride();
    bsp_display_unlock();
}
