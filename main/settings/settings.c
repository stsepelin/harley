#include "settings.h"

void settings_default(settings_t *out)
{
    out->units      = UNITS_KPH;
    out->brightness = 60;
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
}
