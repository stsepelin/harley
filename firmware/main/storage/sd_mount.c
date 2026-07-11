#include "sd_mount.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_mount";

// Board SD wiring: SDMMC slot 0 at the fixed IO_MUX pins, powered from the P4's
// on-chip LDO channel 4. Fixed board constants, not user pins.
#define SD_LDO_CHAN 4
#define SD_PIN_CLK  43
#define SD_PIN_CMD  44
#define SD_PIN_D0   39
#define SD_PIN_D1   40
#define SD_PIN_D2   41
#define SD_PIN_D3   42

static sdmmc_card_t        *s_card;
static sd_pwr_ctrl_handle_t s_pwr;

#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
// esp_hosted creates and owns the single SD/MMC controller very early at boot,
// and the IDF >= 6.0 driver refuses a second creation. These stubs replace the
// driver's init/deinit so mounting slot 0 reuses the live controller instead of
// creating another (esp-idf#16233).
static esp_err_t sdmmc_host_init_stub(void)
{
    return ESP_OK;
}
static esp_err_t sdmmc_host_deinit_stub(void)
{
    return ESP_OK;
}
#endif

esp_err_t sd_mount(void)
{
    if (s_card)
        return ESP_OK;  // already mounted

    // The P4 feeds SD IO power from an on-chip LDO; bring it up before the host.
    sd_pwr_ctrl_ldo_config_t ldo = {.ldo_chan_id = SD_LDO_CHAN};
    esp_err_t                err = ESP_OK;
    if (!s_pwr && (err = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &s_pwr)) != ESP_OK) {
        ESP_LOGE(TAG, "SD LDO init failed (%s)", esp_err_to_name(err));
        return err;
    }

    sdmmc_host_t host    = SDMMC_HOST_DEFAULT();
    host.slot            = SDMMC_HOST_SLOT_0;
    host.pwr_ctrl_handle = s_pwr;
#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    host.init   = sdmmc_host_init_stub;
    host.deinit = sdmmc_host_deinit_stub;
#endif

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
        .format_if_mount_failed = false,  // never reformat the card
        .max_files              = 14,     // ride log + up to 9 paged map cells + slack
        .allocation_unit_size   = 16 * 1024,
    };
    err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s) - no card?", esp_err_to_name(err));
        s_card = NULL;
        return err;
    }
    ESP_LOGI(TAG, "SD mounted at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

bool sd_is_mounted(void)
{
    return s_card != NULL;
}
