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
#include "gps_source.h"
#include "gps_sim.h"
#include "phone_data.h"
#include "poi_alert.h"
#include "poi_db.h"
#include "test_bridge.h"
#include "emoji_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DISPLAY_W   800
#define DISPLAY_H   800
#define UI_TICK_MS  15      // ~66 FPS, matches firmware's LV_DEF_REFR_PERIOD

// Bench-only demo POI DB — mirrors the firmware's app_main() copy. One
// speed camera at the boot position of the gps_sim canned route, so the
// alert engine has something to fire on at least once per orbit.
static const poi_record_t s_demo_poi[] = {
    { 594388000, 247536000, POI_KIND_SPEED, 50, 0xFFFF },
};
static poi_db_t s_demo_db;

int main(void)
{
    // 1) Vehicle state + bridge listener. Comes before SDL window creation
    //    so headless smoke tests (no display, e.g. CI) can still bind the
    //    bridge port and validate the parse/apply path even when window
    //    creation will fail seconds later.
    vehicle_data_init();
    phone_data_init();
    gps_source_init();
    poi_db_open(&s_demo_db, (const uint8_t *)s_demo_poi, sizeof(s_demo_poi));
    poi_alert_init(&s_demo_db);
    test_bridge_start();       // localhost:7700 listener for ad-hoc payloads

    // 2) LVGL core + color-emoji fallback chain. emoji_font_init only
    //    depends on lv_init, not on the SDL window, so it runs ahead of
    //    the window check — that way headless smoke tests still exercise
    //    the FreeType linkage (good CI canary).
    lv_init();
    emoji_font_init();

    // 3) SDL2 backend. No display → keep the process alive so the
    //    bridge thread (already on localhost:7700) can still service
    //    notify.py round-trips for protocol-level smoke testing.
    lv_display_t *display = lv_sdl_window_create(DISPLAY_W, DISPLAY_H);
    if (!display) {
        fprintf(stderr, "no SDL display — staying alive for bridge-only mode\n");
        pause();
        return 0;
    }
    lv_sdl_window_set_title(display, "V-Rod cluster simulator");
    lv_sdl_mouse_create();

    // 4) Producer for the driving cycle. phone_data has no built-in
    //    producer here — push events from the test bridge (tools/notify.py)
    //    instead.
    sim_engine_start();
    gps_sim_start();

    // 5) Init settings (desktop shim — defaults only) and build the ride
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

        gps_source_t gps;
        gps_source_get(&gps);
        poi_alert_tick(&gps);

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
