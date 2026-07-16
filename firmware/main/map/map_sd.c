#include "map_sd.h"
#include "ble_peripheral.h"
#include "gps_source.h"
#include "map_source.h"
#include "map_tile.h"
#include "phone_data.h"
#include "screen_map.h"
#include "settings_store.h"
#include "ui_manager.h"
#include "vehicle_data.h"

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sd_mount.h"

static const char *TAG = "map_sd";

#define MAP_SD_PPT      340.0
#define MAP_SD_FRAME_MS     33  // ~30 fps: the fixed-point rotate + PPA leave headroom, and
#define LOC_STALE_MS    5000
#define GPS_MODULE_STALE_MS 3000  // module fix older than this -> fall back to the phone

#if !CONFIG_VROD_MAP_SD_CELLS
static map_tileset_t *s_ts;  // single-archive path only; the cell grid owns its own
#endif
static map_source_t *s_src;
static lv_obj_t     *s_screen;

static void anim_task(void *arg)
{
    (void)arg;
    uint32_t frame = 0;
    double   cx, cy;
    map_source_center(s_src, &cx, &cy);  // hold here until a fix arrives

    for (;;) {
        if (lv_screen_active() != s_screen) {
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }

        vehicle_data_t vd;
        vehicle_data_get(&vd);

        // Dual-source position: prefer the onboard GPS module (fresh, low
        // latency, real course-over-ground); fall back to the phone's GPS over
        // BLE when the module has no recent fix. When no module is compiled in,
        // gps_source stays empty (fix_ok false) and we always use the phone.
        double           heading = -1.0;
        int              zoom    = map_source_zoom(s_src);
        map_nav_source_t source  = MAP_NAV_NONE;  // what's actually driving the view
        gps_source_t     g;
        gps_source_get(&g);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (g.fix_ok && (now_ms - g.time_ms) < GPS_MODULE_STALE_MS) {
            map_lonlat_to_tilef(g.lon_e7 / 1e7, g.lat_e7 / 1e7, zoom, &cx, &cy);
            heading = g.heading_deg;
            source  = MAP_NAV_MODULE;
        } else {
            phone_location_t loc;
            phone_data_get_location(&loc);
            if (loc.valid && loc.age_ms < LOC_STALE_MS) {
                map_lonlat_to_tilef(loc.lon_e7 / 1e7, loc.lat_e7 / 1e7, zoom, &cx, &cy);
                heading = loc.heading_cd == 0xFFFF ? -1.0 : loc.heading_cd / 100.0;
                source  = MAP_NAV_PHONE;
            }
        }

        map_source_set_center(s_src, cx, cy, heading);  // paged sources prefetch here
        screen_map_render(cx, cy, MAP_SD_PPT, heading);
        bsp_display_lock(-1);
        screen_map_commit(&vd, settings_store_current());
        screen_map_set_no_coverage(!map_source_covers(s_src, (uint32_t)cx, (uint32_t)cy));
        screen_map_set_nav_source(source, g.sats_in_view, g.fix_ok);  // SAT n / BT badge
        ble_peripheral_state_t bt;
        ble_peripheral_get_state(&bt);
        screen_map_set_phone_link(bt.connected);  // blue phone-link dot
        bsp_display_unlock();

        // Map-health telemetry (~every 2 s): if the map blanks mid-ride this tells
        // leak (free_kb falls steadily) from fragmentation (free_kb steady but
        // largest_kb collapses) from SD trouble (read_fails climbs). PSRAM = SPIRAM.
        if (++frame % 60 == 0) {
            ESP_LOGI(TAG, "map heap: free=%uKB largest=%uKB min_ever=%uKB read_fails=%u",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
                     (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024),
                     (unsigned)map_tileset_read_fails());
        }
        vTaskDelay(pdMS_TO_TICKS(MAP_SD_FRAME_MS));
    }
}

void map_sd_load(void)
{
    if (s_src)
        return;  // already loaded (lazy + idempotent)
    if (sd_mount() != ESP_OK)
        return;

#if CONFIG_VROD_MAP_SD_CELLS
    // GPS-paged cell grid: read <dir>/world.hdr and keep only the cells near the
    // rider open, so a whole continent lives on the card (map-worldwide-plan.md).
    s_src = map_source_open_cells(CONFIG_VROD_MAP_SD_DIR);
    if (!s_src) {
        ESP_LOGE(TAG, "bad/missing cell map at %s/world.hdr", CONFIG_VROD_MAP_SD_DIR);
        return;
    }
    ESP_LOGI(TAG, "paged cell map z%d from %s", map_source_zoom(s_src), CONFIG_VROD_MAP_SD_DIR);
#else
    // Streaming: read only the tile index into RAM and keep the file open, so a
    // country-sized archive (tens of MB) costs ~index bytes, not the whole file
    // in PSRAM. Tiles are read off the card on demand as the map scrolls.
    s_ts = map_tileset_open_file(CONFIG_VROD_MAP_SD_PATH);
    if (!s_ts || s_ts->ntiles == 0) {
        ESP_LOGE(TAG, "bad/missing archive %s", CONFIG_VROD_MAP_SD_PATH);
        map_tileset_free(s_ts);
        s_ts = NULL;
        return;
    }
    ESP_LOGI(TAG, "streaming %d tiles z%d from %s", s_ts->ntiles, s_ts->zoom,
             CONFIG_VROD_MAP_SD_PATH);
    s_src = map_source_from_tileset(s_ts, true);  // owns the streamed tileset
#endif

    bsp_display_lock(-1);
    s_screen = screen_map_create(s_src, 800, 800);
    ui_manager_set_map_screen(s_screen);
    bsp_display_unlock();

    xTaskCreatePinnedToCore(anim_task, "map_sd", 8192, NULL, 4, NULL, 0);
}
