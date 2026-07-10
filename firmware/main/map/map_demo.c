#include "map_demo.h"
#include "map_tile.h"
#include "phone_data.h"
#include "screen_map.h"
#include "settings_store.h"
#include "ui_manager.h"
#include "vehicle_data.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "esp_timer.h"
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
#define LOC_STALE_MS      5000   // beyond this with no fix, fall back to the route
#define MPH_TO_MPS        0.44704
#define MAX_ROUTE_PTS     512

static map_tileset_t *s_ts;
static lv_obj_t      *s_screen;  // the map screen; rendered only while it's active

// The embedded track, parsed to route geometry only (the speed column is
// ignored - the sim engine's speed drives how fast we move along it, so the
// gauge, the map scroll, and the strip are one coherent synthetic drive).
static double s_lat[MAX_ROUTE_PTS], s_lon[MAX_ROUTE_PTS], s_cum[MAX_ROUTE_PTS];
static int    s_npts;
static double s_total_m;  // route length
static double s_dist_m;   // distance travelled so far

static double haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    double R   = 6371000.0;
    double dla = (lat2 - lat1) * M_PI / 180.0;
    double dlo = (lon2 - lon1) * M_PI / 180.0;
    double a   = sin(dla / 2) * sin(dla / 2) +
                 cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) * sin(dlo / 2) * sin(dlo / 2);
    return 2.0 * R * asin(sqrt(a));
}

// Initial compass bearing from point 1 to point 2 (degrees, 0 = north, CW).
static double bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * M_PI / 180.0, p2 = lat2 * M_PI / 180.0;
    double dl = (lon2 - lon1) * M_PI / 180.0;
    double y  = sin(dl) * cos(p2);
    double x  = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double b  = atan2(y, x) * 180.0 / M_PI;
    return b < 0 ? b + 360.0 : b;
}

// Parse the embedded track ("lat lon speed" lines) into route points +
// cumulative distances (metres). Speed is discarded.
static void parse_route(void)
{
    const char *p   = track_txt_start;
    const char *end = track_txt_end;
    s_npts          = 0;
    while (s_npts < MAX_ROUTE_PTS) {
        while (p < end && (*p == '\n' || *p == '\r' || *p == ' '))
            p++;
        if (p >= end)
            break;
        double lat, lon;
        float  mph;
        if (sscanf(p, "%lf %lf %f", &lat, &lon, &mph) == 3) {
            s_lat[s_npts] = lat;
            s_lon[s_npts] = lon;
            s_npts++;
        }
        while (p < end && *p != '\n')
            p++;
    }
    s_cum[0] = 0.0;
    for (int i = 1; i < s_npts; i++)
        s_cum[i] = s_cum[i - 1] + haversine_m(s_lat[i - 1], s_lon[i - 1], s_lat[i], s_lon[i]);
    s_total_m = s_npts ? s_cum[s_npts - 1] : 0.0;
}

// Interpolate the lat/lon at distance `d` along the route.
static void route_at(double d, double *lat, double *lon)
{
    if (s_npts == 0) {
        *lat = *lon = 0;
        return;
    }
    if (d <= 0 || s_total_m <= 0) {
        *lat = s_lat[0];
        *lon = s_lon[0];
        return;
    }
    int i = 1;
    while (i < s_npts && s_cum[i] < d)
        i++;
    if (i >= s_npts) {
        *lat = s_lat[s_npts - 1];
        *lon = s_lon[s_npts - 1];
        return;
    }
    double seg = s_cum[i] - s_cum[i - 1];
    double t   = seg > 0 ? (d - s_cum[i - 1]) / seg : 0.0;
    *lat       = s_lat[i - 1] + t * (s_lat[i] - s_lat[i - 1]);
    *lon       = s_lon[i - 1] + t * (s_lon[i] - s_lon[i - 1]);
}

static void anim_task(void *arg)
{
    (void)arg;
    for (;;) {
        // Only draw while the map is the visible screen; idle on the gauge.
        if (lv_screen_active() != s_screen) {
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }

        // One shared source of truth: the gauge widgets read vehicle_data, and
        // the map scroll below advances at that same speed.
        vehicle_data_t vd;
        vehicle_data_get(&vd);

        double           lat, lon, heading;
        phone_location_t loc;
        phone_data_get_location(&loc);
        if (loc.valid && loc.age_ms < LOC_STALE_MS) {
            lat     = loc.lat_e7 / 1e7;  // a real phone GPS fix overrides the route
            lon     = loc.lon_e7 / 1e7;
            heading = loc.heading_cd == 0xFFFF ? -1.0 : loc.heading_cd / 100.0;
        } else {
            // Demo: advance along the baked route at the gauge's own speed, so
            // the map scrolls exactly as fast as the speedo reads.
            s_dist_m += vd.speed_mph * MPH_TO_MPS * (MAP_DEMO_FRAME_MS / 1000.0);
            if (s_total_m > 0 && s_dist_m >= s_total_m)
                s_dist_m = fmod(s_dist_m, s_total_m);  // loop the route
            route_at(s_dist_m, &lat, &lon);
            double lat2, lon2;  // a few metres ahead, for the travel direction
            route_at(s_dist_m + 8.0, &lat2, &lon2);
            heading = bearing_deg(lat, lon, lat2, lon2);
        }

        double tx, ty;
        map_lonlat_to_tilef(lon, lat, s_ts->zoom, &tx, &ty);
        // Heavy rasterise off the lock AND on core 0, so it never competes with
        // the LVGL renderer (core 1) for CPU; only the swap + strip run under
        // the lock.
        int64_t t0 = esp_timer_get_time();
        screen_map_render(tx, ty, MAP_DEMO_PPT, heading);
        int64_t us = esp_timer_get_time() - t0;
        bsp_display_lock(-1);
        screen_map_commit(&vd, settings_store_current());
        screen_map_set_no_coverage(!map_tileset_covers(s_ts, (uint32_t)tx, (uint32_t)ty));
        bsp_display_unlock();

        static int frame = 0;
        if (++frame % 30 == 0)
            ESP_LOGI(TAG, "rasterise %lld us (%d mph)", us, vd.speed_mph);
        vTaskDelay(pdMS_TO_TICKS(MAP_DEMO_FRAME_MS));
    }
}

void map_demo_load(void)
{
    if (s_ts)
        return;  // already loaded (lazy + idempotent)
    size_t len = (size_t)(corridor_zmta_end - corridor_zmta_start);
    s_ts       = map_tileset_load_mem(corridor_zmta_start, len);
    if (!s_ts || s_ts->ntiles == 0) {
        ESP_LOGE(TAG, "tileset load failed (%zu bytes)", len);
        return;
    }
    ESP_LOGI(TAG, "loaded %d tiles z%d (%zu KB embedded)", s_ts->ntiles, s_ts->zoom, len / 1024);

    parse_route();
    ESP_LOGI(TAG, "route: %d points, %.1f km", s_npts, s_total_m / 1000.0);

    // Build the screen and hand it to ui_manager; it stays off-screen until a
    // double-tap toggles to it (the gauge is the default view).
    bsp_display_lock(-1);
    s_screen = screen_map_create(s_ts, 800, 800);
    ui_manager_set_map_screen(s_screen);
    bsp_display_unlock();

    // Core 0 (the compute core: sim, event watcher, audio) - NOT core 1, which
    // is LVGL's. Keeps the heavy rasterise from stealing cycles from rendering.
    xTaskCreatePinnedToCore(anim_task, "map_demo", 8192, NULL, 4, NULL, 0);
}
