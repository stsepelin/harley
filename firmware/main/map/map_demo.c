#include "map_demo.h"
#include "map_tile.h"
#include "phone_data.h"
#include "screen_map.h"
#include "ui_manager.h"
#include "vehicle_data.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <math.h>
#include <stdio.h>

// Embedded by main/CMakeLists.txt (EMBED_FILES) when CONFIG_VROD_MAP_DEMO=y.
extern const uint8_t corridor_zmta_start[] asm("_binary_corridor_zmta_start");
extern const uint8_t corridor_zmta_end[] asm("_binary_corridor_zmta_end");
extern const char    track_txt_start[] asm("_binary_track_txt_start");
extern const char    track_txt_end[] asm("_binary_track_txt_end");

static const char *TAG = "map_demo";

#define MAP_DEMO_PPT      340.0  // px per tile (view zoom)
#define MAP_DEMO_FRAME_MS 66     // ~15 fps playback
#define LOC_STALE_MS      5000   // beyond this with no fix, fall back to the demo track
#define MAP_TACH_REDLINE  9000   // rpm mapped to a full tach bar

static map_tileset_t *s_ts;
static lv_obj_t      *s_screen;  // the map screen; rendered only while it's active

// Instrument strip from the real bus data (gear N when out of 1..6).
static void strip_from_vehicle(const vehicle_data_t *vd, int *speed, int *gear, int *tach,
                               int *temp)
{
    *speed  = vd->speed_mph;
    *gear   = (vd->gear >= GEAR_1 && vd->gear <= GEAR_6) ? (int)vd->gear : 0;
    int pct = (int)((uint32_t)vd->rpm * 100u / MAP_TACH_REDLINE);
    *tach   = pct > 100 ? 100 : pct;
    *temp   = vd->engine_temp_c;
}

// Advance one track line ("lat lon speed_mph"); returns pointer past the newline
// or NULL at the end. Parses into *lat/*lon/*mph.
static const char *next_point(const char *p, const char *end, double *lat, double *lon, float *mph)
{
    while (p < end && (*p == '\n' || *p == '\r' || *p == ' '))
        p++;
    if (p >= end)
        return NULL;
    if (sscanf(p, "%lf %lf %f", lat, lon, mph) != 3)
        return NULL;
    while (p < end && *p != '\n')
        p++;
    return p;
}

static void anim_task(void *arg)
{
    (void)arg;
    const char *end    = track_txt_end;
    const char *cursor = track_txt_start;  // demo-track playback position
    for (;;) {
        // Only draw while the map is the visible screen; idle on the gauge.
        if (lv_screen_active() != s_screen) {
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }
        double tx, ty;
        int    speed, gear, tach, temp;

        phone_location_t loc;
        phone_data_get_location(&loc);
        if (loc.valid && loc.age_ms < LOC_STALE_MS) {
            // Real: position from the phone's GPS, strip from the J1850 bus.
            map_lonlat_to_tilef(loc.lon_e7 / 1e7, loc.lat_e7 / 1e7, s_ts->zoom, &tx, &ty);
            vehicle_data_t vd;
            vehicle_data_get(&vd);
            strip_from_vehicle(&vd, &speed, &gear, &tach, &temp);
        } else {
            // Fallback: walk the baked demo route + synthesise the strip.
            double      lat, lon;
            float       mph;
            const char *np = next_point(cursor, end, &lat, &lon, &mph);
            if (np == NULL) {
                cursor = track_txt_start;  // loop the route
                continue;
            }
            cursor = np;
            map_lonlat_to_tilef(lon, lat, s_ts->zoom, &tx, &ty);
            speed = (int)lrint(mph);
            screen_map_synth(speed, &gear, &tach, &temp);
        }

        // Heavy rasterise off the lock; only the swap + strip run under it, so
        // the LVGL render task is never blocked on our CPU work.
        screen_map_render(tx, ty, MAP_DEMO_PPT);
        bsp_display_lock(-1);
        screen_map_commit(speed, gear, tach, temp);
        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(MAP_DEMO_FRAME_MS));
    }
}

void map_demo_start(void)
{
    size_t len = (size_t)(corridor_zmta_end - corridor_zmta_start);
    s_ts       = map_tileset_load_mem(corridor_zmta_start, len);
    if (!s_ts || s_ts->ntiles == 0) {
        ESP_LOGE(TAG, "tileset load failed (%zu bytes)", len);
        return;
    }
    ESP_LOGI(TAG, "loaded %d tiles z%d (%zu KB embedded)", s_ts->ntiles, s_ts->zoom, len / 1024);

    // Build the screen and hand it to ui_manager; it stays off-screen until a
    // double-tap toggles to it (the gauge is the default view).
    bsp_display_lock(-1);
    s_screen = screen_map_create(s_ts, 800, 800);
    ui_manager_set_map_screen(s_screen);
    bsp_display_unlock();

    xTaskCreatePinnedToCore(anim_task, "map_demo", 8192, NULL, 4, NULL, 1);
}
