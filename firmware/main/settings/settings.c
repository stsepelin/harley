#include "settings.h"

void settings_default(settings_t *out)
{
    out->units                = UNITS_KPH;
    out->temp_units           = UNITS_CELSIUS;
    out->brightness           = 60;
    out->sound_enabled        = true;
    out->volume               = 70;
    out->ble_visible_override = false;
    out->speed_divisor        = SETTINGS_SPEED_DIVISOR_DEFAULT;
    out->layout               = LAYOUT_CLASSIC;
}

void settings_validate(settings_t *out)
{
    if (out->units != UNITS_KPH && out->units != UNITS_MPH) {
        out->units = UNITS_KPH;
    }
    if (out->temp_units != UNITS_CELSIUS && out->temp_units != UNITS_FAHRENHEIT) {
        out->temp_units = UNITS_CELSIUS;
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
    if (out->speed_divisor < SETTINGS_SPEED_DIVISOR_MIN ||
        out->speed_divisor > SETTINGS_SPEED_DIVISOR_MAX) {
        out->speed_divisor = SETTINGS_SPEED_DIVISOR_DEFAULT;
    }
    if (out->layout != LAYOUT_CLASSIC && out->layout != LAYOUT_MAP) {
        out->layout = LAYOUT_CLASSIC;
    }
}
