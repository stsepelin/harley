#include "map_sd.h"
#include "map_tile.h"
#include "phone_data.h"
#include "screen_map.h"
#include "settings_store.h"
#include "ui_manager.h"
#include "vehicle_data.h"

#include "bsp/esp-bsp.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
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

// Read a whole file into a PSRAM buffer (the archive can be several MB - too big
// for internal RAM). Returns the buffer (caller owns) + length, or NULL.
static uint8_t *read_file_psram(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "cannot open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "no PSRAM for %ld-byte archive", len);
        fclose(f);
        return NULL;
    }
    size_t got = 0;
    while (got < (size_t)len) {
        size_t n = fread(buf + got, 1, (size_t)len - got, f);
        if (n == 0)
            break;
        got += n;
    }
    fclose(f);
    if (got != (size_t)len) {
        ESP_LOGE(TAG, "short read %zu/%ld", got, len);
        heap_caps_free(buf);
        return NULL;
    }
    *len_out = got;
    return buf;
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

        double           heading = -1.0;
        phone_location_t loc;
        phone_data_get_location(&loc);
        if (loc.valid && loc.age_ms < LOC_STALE_MS) {
            map_lonlat_to_tilef(loc.lon_e7 / 1e7, loc.lat_e7 / 1e7, s_ts->zoom, &cx, &cy);
            heading = loc.heading_cd == 0xFFFF ? -1.0 : loc.heading_cd / 100.0;
        }

        screen_map_render(cx, cy, MAP_SD_PPT, heading);
        bsp_display_lock(-1);
        screen_map_commit(&vd, settings_store_current());
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

    size_t   len;
    uint8_t *bytes = read_file_psram(CONFIG_VROD_MAP_SD_PATH, &len);
    if (!bytes)
        return;
    s_ts = map_tileset_load_mem_owned(bytes, len);  // frees bytes on failure
    if (!s_ts || s_ts->ntiles == 0) {
        ESP_LOGE(TAG, "bad archive %s", CONFIG_VROD_MAP_SD_PATH);
        map_tileset_free(s_ts);
        s_ts = NULL;
        return;
    }
    ESP_LOGI(TAG, "loaded %d tiles z%d (%zu KB from SD)", s_ts->ntiles, s_ts->zoom, len / 1024);

    bsp_display_lock(-1);
    s_screen = screen_map_create(s_ts, 800, 800);
    ui_manager_set_map_screen(s_screen);
    bsp_display_unlock();

    xTaskCreatePinnedToCore(anim_task, "map_sd", 8192, NULL, 4, NULL, 0);
}
