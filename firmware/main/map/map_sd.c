#include "map_sd.h"
#include "gps_source.h"
#include "map_tile.h"
#include "phone_data.h"
#include "screen_map.h"
#include "settings_store.h"
#include "ui_manager.h"
#include "vehicle_data.h"

#include "bsp/esp-bsp.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "map_sd";

// Board microSD wiring - identical to ride_log.c (SDMMC slot 0, IO_MUX pins,
// on-chip LDO channel 4). Kept local so a map-only build needs neither the
// J1850 ride-log nor a refactor of its validated mount.
#define MOUNT_POINT "/sdcard"
#define SD_LDO_CHAN 4
#define SD_PIN_CLK  43
#define SD_PIN_CMD  44
#define SD_PIN_D0   39
#define SD_PIN_D1   40
#define SD_PIN_D2   41
#define SD_PIN_D3   42

#define MAP_SD_PPT      340.0
#define MAP_SD_FRAME_MS 66
#define LOC_STALE_MS    5000
#define GPS_MODULE_STALE_MS 3000  // module fix older than this -> fall back to the phone

static map_tileset_t       *s_ts;
static lv_obj_t            *s_screen;
static sdmmc_card_t        *s_card;
static sd_pwr_ctrl_handle_t s_pwr;

static bool mount_card(void)
{
    if (s_card)
        return true;

    sd_pwr_ctrl_ldo_config_t ldo = {.ldo_chan_id = SD_LDO_CHAN};
    if (!s_pwr && sd_pwr_ctrl_new_on_chip_ldo(&ldo, &s_pwr) != ESP_OK) {
        ESP_LOGE(TAG, "SD LDO init failed");
        return false;
    }

    // Slot 0 shares the P4's single SD/MMC controller with the C6 radio's SDIO
    // link on slot 1; esp_hosted owns the controller, so the driver just attaches
    // slot 0 here (init/deinit are esp_hosted's). Mirrors ride_log.c's mount.
    sdmmc_host_t host    = SDMMC_HOST_DEFAULT();
    host.slot            = SDMMC_HOST_SLOT_0;
    host.pwr_ctrl_handle = s_pwr;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width               = 4;
    slot.clk                 = SD_PIN_CLK;
    slot.cmd                 = SD_PIN_CMD;
    slot.d0                  = SD_PIN_D0;
    slot.d1                  = SD_PIN_D1;
    slot.d2                  = SD_PIN_D2;
    slot.d3                  = SD_PIN_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (err == ESP_ERR_INVALID_STATE)
        return true;  // another consumer (ride log) already mounted /sdcard
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s) - no card?", esp_err_to_name(err));
        s_card = NULL;
        return false;
    }
    ESP_LOGI(TAG, "SD mounted at %s", MOUNT_POINT);
    return true;
}

// Centre of the baked area, so the map shows something before the first fix.
static void tileset_center(double *tx, double *ty)
{
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (int i = 0; i < s_ts->ntiles; i++) {
        double x = s_ts->tiles[i].tx, y = s_ts->tiles[i].ty;
        minx = fmin(minx, x);
        maxx = fmax(maxx, x);
        miny = fmin(miny, y);
        maxy = fmax(maxy, y);
    }
    *tx = (minx + maxx + 1) / 2.0;
    *ty = (miny + maxy + 1) / 2.0;
}

static void anim_task(void *arg)
{
    (void)arg;
    double cx, cy;
    tileset_center(&cx, &cy);  // hold here until a fix arrives

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
        double       heading = -1.0;
        gps_source_t g;
        gps_source_get(&g);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (g.fix_ok && (now_ms - g.time_ms) < GPS_MODULE_STALE_MS) {
            map_lonlat_to_tilef(g.lon_e7 / 1e7, g.lat_e7 / 1e7, s_ts->zoom, &cx, &cy);
            heading = g.heading_deg;
        } else {
            phone_location_t loc;
            phone_data_get_location(&loc);
            if (loc.valid && loc.age_ms < LOC_STALE_MS) {
                map_lonlat_to_tilef(loc.lon_e7 / 1e7, loc.lat_e7 / 1e7, s_ts->zoom, &cx, &cy);
                heading = loc.heading_cd == 0xFFFF ? -1.0 : loc.heading_cd / 100.0;
            }
        }

        screen_map_render(cx, cy, MAP_SD_PPT, heading);
        bsp_display_lock(-1);
        screen_map_commit(&vd, settings_store_current());
        screen_map_set_no_coverage(!map_tileset_covers(s_ts, (uint32_t)cx, (uint32_t)cy));
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(MAP_SD_FRAME_MS));
    }
}

void map_sd_load(void)
{
    if (s_ts)
        return;  // already loaded (lazy + idempotent)
    if (!mount_card())
        return;

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

    bsp_display_lock(-1);
    s_screen = screen_map_create(s_ts, 800, 800);
    ui_manager_set_map_screen(s_screen);
    bsp_display_unlock();

    xTaskCreatePinnedToCore(anim_task, "map_sd", 8192, NULL, 4, NULL, 0);
}
