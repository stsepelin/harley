#include "settings_store.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define NS          "vrod"
#define KEY_UNITS   "units"
#define KEY_TUNITS  "temp_units"
#define KEY_BRIGHT  "brightness"
#define KEY_SND_EN  "sound_en"
#define KEY_VOLUME  "volume"
#define KEY_BLE_VIS "ble_vis"
#define KEY_SPEEDIV "speed_div"
#define KEY_LAYOUT  "layout"

static settings_t s_current;

static void load_into(settings_t *out)
{
    settings_default(out);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        // First boot: namespace not yet created, keep defaults.
        return;
    }

    uint8_t v;
    if (nvs_get_u8(h, KEY_UNITS, &v) == ESP_OK)
        out->units = (display_units_t)v;
    if (nvs_get_u8(h, KEY_TUNITS, &v) == ESP_OK)
        out->temp_units = (temp_units_t)v;
    if (nvs_get_u8(h, KEY_BRIGHT, &v) == ESP_OK)
        out->brightness = v;
    if (nvs_get_u8(h, KEY_SND_EN, &v) == ESP_OK)
        out->sound_enabled = (v != 0);
    if (nvs_get_u8(h, KEY_VOLUME, &v) == ESP_OK)
        out->volume = v;
    if (nvs_get_u8(h, KEY_BLE_VIS, &v) == ESP_OK)
        out->ble_visible_override = (v != 0);
    uint16_t sd;
    if (nvs_get_u16(h, KEY_SPEEDIV, &sd) == ESP_OK)
        out->speed_divisor = sd;
    if (nvs_get_u8(h, KEY_LAYOUT, &v) == ESP_OK)
        out->layout = (layout_t)v;
    nvs_close(h);

    settings_validate(out);
}

static bool save_from(const settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open: %s", esp_err_to_name(err));
        return false;
    }

    bool ok = nvs_set_u8(h, KEY_UNITS, (uint8_t)s->units) == ESP_OK &&
              nvs_set_u8(h, KEY_TUNITS, (uint8_t)s->temp_units) == ESP_OK &&
              nvs_set_u8(h, KEY_BRIGHT, s->brightness) == ESP_OK &&
              nvs_set_u8(h, KEY_SND_EN, s->sound_enabled ? 1u : 0u) == ESP_OK &&
              nvs_set_u8(h, KEY_VOLUME, s->volume) == ESP_OK &&
              nvs_set_u8(h, KEY_BLE_VIS, s->ble_visible_override ? 1u : 0u) == ESP_OK &&
              nvs_set_u16(h, KEY_SPEEDIV, s->speed_divisor) == ESP_OK &&
              nvs_set_u8(h, KEY_LAYOUT, (uint8_t)s->layout) == ESP_OK && nvs_commit(h) == ESP_OK;

    nvs_close(h);
    if (!ok) ESP_LOGW(TAG, "save failed");
    return ok;
}

void settings_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unreadable (%s) — erasing and re-initialising", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    }
    load_into(&s_current);
}

const settings_t *settings_store_current(void)
{
    return &s_current;
}

bool settings_store_apply(const settings_t *s)
{
    s_current = *s;
    settings_validate(&s_current);
    return save_from(&s_current);
}
