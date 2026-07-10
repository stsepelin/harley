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
#define LOC_STALE_MS      5000   // beyond this with no fix, fall back to the route
#define MAP_TACH_REDLINE  9000   // rpm mapped to a full tach bar
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

// Instrument strip from the shared vehicle_data (sim engine in the demo, J1850
// on the bike). Gear reads N when out of 1..6.
static void strip_from_vehicle(const vehicle_data_t *vd, int *speed, int *gear, int *tach,
                               int *temp)
{
    *speed  = vd->speed_mph;
    *gear   = (vd->gear >= GEAR_1 && vd->gear <= GEAR_6) ? (int)vd->gear : 0;
    int pct = (int)((uint32_t)vd->rpm * 100u / MAP_TACH_REDLINE);
    *tach   = pct > 100 ? 100 : pct;
    *temp   = vd->engine_temp_c;
}

static double haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    double R   = 6371000.0;
    double dla = (lat2 - lat1) * M_PI / 180.0;
    double dlo = (lon2 - lon1) * M_PI / 180.0;
    double a   = sin(dla / 2) * sin(dla / 2) +
                 cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) * sin(dlo / 2) * sin(dlo / 2);
    return 2.0 * R * asin(sqrt(a));
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

        // One shared source of truth: the strip always reads vehicle_data.
        vehicle_data_t vd;
        vehicle_data_get(&vd);
        int speed, gear, tach, temp;
        strip_from_vehicle(&vd, &speed, &gear, &tach, &temp);

        double           lat, lon;
        phone_location_t loc;
        phone_data_get_location(&loc);
        if (loc.valid && loc.age_ms < LOC_STALE_MS) {
            lat = loc.lat_e7 / 1e7;  // a real phone GPS fix overrides the route
            lon = loc.lon_e7 / 1e7;
        } else {
            // Demo: advance along the baked route at the gauge's own speed, so
            // the map scrolls exactly as fast as the speedo reads.
            s_dist_m += vd.speed_mph * MPH_TO_MPS * (MAP_DEMO_FRAME_MS / 1000.0);
            if (s_total_m > 0 && s_dist_m >= s_total_m)
                s_dist_m = fmod(s_dist_m, s_total_m);  // loop the route
            route_at(s_dist_m, &lat, &lon);
        }

        double tx, ty;
        map_lonlat_to_tilef(lon, lat, s_ts->zoom, &tx, &ty);
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

    parse_route();
    ESP_LOGI(TAG, "route: %d points, %.1f km", s_npts, s_total_m / 1000.0);

    // Build the screen and hand it to ui_manager; it stays off-screen until a
    // double-tap toggles to it (the gauge is the default view).
    bsp_display_lock(-1);
    s_screen = screen_map_create(s_ts, 800, 800);
    ui_manager_set_map_screen(s_screen);
    bsp_display_unlock();

    xTaskCreatePinnedToCore(anim_task, "map_demo", 8192, NULL, 4, NULL, 1);
}
