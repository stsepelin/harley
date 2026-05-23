#include "settings.h"

void settings_default(settings_t *out)
{
    out->units                = UNITS_KPH;
    out->brightness           = 60;
    out->sound_enabled        = true;
    out->volume               = 70;
    out->ble_visible_override = false;
}

void settings_validate(settings_t *out)
{
    if (out->units != UNITS_KPH && out->units != UNITS_MPH) {
        out->units = UNITS_KPH;
    }
    if (out->brightness < SETTINGS_BRIGHTNESS_MIN) {
        out->brightness = SETTINGS_BRIGHTNESS_MIN;
    }
    if (out->brightness > 100) {
        out->brightness = 100;
    }
    if (out->volume > 100) {
        out->volume = 100;
    }
}
