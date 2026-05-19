// Desktop V-Rod cluster simulator.
//
//   - Real production widget code (no mocks) drives the ride screen.
//   - The synthetic driving cycle from main/simulator/sim_engine.c runs on
//     a real pthread (via the FreeRTOS shim) and feeds vehicle_data.
//   - LVGL renders into an SDL2 window at native 800×800.
//
// Quit by closing the window or hitting Ctrl-C.

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"

#include "vehicle_data.h"
#include "sim_engine.h"
#include "screen_ride.h"
#include "settings_store.h"
#include "ui_manager.h"
#include "gesture.h"
#include "phone_data.h"
#include "phone_mock.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DISPLAY_W   800
#define DISPLAY_H   800
#define UI_TICK_MS  15      // ~66 FPS, matches firmware's LV_DEF_REFR_PERIOD

int main(void)
{
    // 1) LVGL core + SDL2 backend
    lv_init();
    lv_display_t *display = lv_sdl_window_create(DISPLAY_W, DISPLAY_H);
    if (!display) {
        fprintf(stderr, "failed to create SDL window\n");
        return 1;
    }
    lv_sdl_window_set_title(display, "V-Rod cluster simulator");
    lv_sdl_mouse_create();

    // 2) Vehicle state + sim cycle (starts a pthread via the FreeRTOS shim)
    vehicle_data_init();
    phone_data_init();
    sim_engine_start();
    phone_mock_start();        // scripted notification/media timeline

    // 3) Init settings (desktop shim — defaults only) and build the ride
    //    screen against the running sim. The ui_manager shim caches both
    //    screens so the settings → back path rejoins the original instead
    //    of rebuilding the ride each time.
    ui_manager_init();

    // 4) Main loop: pump vehicle data + settings into the UI, then let
    //    LVGL render. The sim updates s_data on its own thread;
    //    vehicle_data_get gives us a snapshot under the mutex so we never
    //    see a torn struct.
    //
    //    Long-press + swipe detection uses the same gesture FSM as the
    //    firmware (main/display/gesture.c). On desktop there's no
    //    FreeRTOS task to pin it to, so we run it inline.
    gesture_state_t gesture;
    gesture_init(&gesture);

    while (1) {
        vehicle_data_t snapshot;
        vehicle_data_get(&snapshot);
        screen_ride_update(&snapshot, settings_store_current());

        lv_indev_t *indev   = lv_indev_get_next(NULL);
        bool        pressed = false;
        lv_point_t  pt      = { 0, 0 };
        if (indev) {
            pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
            lv_indev_get_point(indev, &pt);
        }
        switch (gesture_update(&gesture, pressed, pt.x, pt.y, lv_tick_get())) {
        case GESTURE_LONG_PRESS:  ui_manager_show_settings();              break;
        case GESTURE_SWIPE_LEFT:  phone_data_handle_swipe(PHONE_SWIPE_LEFT);  break;
        case GESTURE_SWIPE_RIGHT: phone_data_handle_swipe(PHONE_SWIPE_RIGHT); break;
        case GESTURE_SWIPE_UP:    phone_data_handle_swipe(PHONE_SWIPE_UP);    break;
        case GESTURE_SWIPE_DOWN:  phone_data_handle_swipe(PHONE_SWIPE_DOWN);  break;
        case GESTURE_NONE:        default:                                    break;
        }

        lv_timer_handler();
        usleep(UI_TICK_MS * 1000);
    }
}
