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

#include <stdio.h>
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
    sim_engine_start();

    // 3) Init settings (desktop shim — defaults only) and build the ride
    //    screen against the running sim. The ui_manager shim caches both
    //    screens so the settings → back path rejoins the original instead
    //    of rebuilding the ride each time.
    ui_manager_init();

    // 4) Main loop: pump vehicle data + settings into the UI, then let
    //    LVGL render. The sim updates s_data on its own thread;
    //    vehicle_data_get gives us a snapshot under the mutex so we never
    //    see a torn struct.
    while (1) {
        vehicle_data_t snapshot;
        vehicle_data_get(&snapshot);
        screen_ride_update(&snapshot, settings_store_current());

        lv_timer_handler();
        usleep(UI_TICK_MS * 1000);
    }
}
