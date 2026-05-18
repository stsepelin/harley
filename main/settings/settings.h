#pragma once
#include <stdint.h>
#include "units.h"

// PWM duty cycles below this value put the Waveshare LCD into a
// non-visible state on this BSP (observed: 20% reads as full black).
// Floor everything below this so the user can't lock themselves out.
#define SETTINGS_BRIGHTNESS_MIN  30u

// Persisted user preferences. Loaded once at boot, written by the
// settings screen on change. Keep small — anything bigger than a few
// bytes per field should probably be its own NVS namespace.
typedef struct {
    display_units_t units;        // kph or mph
    uint8_t         brightness;   // 30..100 (mapped to the BSP duty cycle)
} settings_t;

// Hardcoded fallback values. Used when NVS is empty or unreadable.
void settings_default(settings_t *out);

// Clamp fields to valid ranges in-place. Idempotent — call after every
// load so the rest of the firmware can treat the struct as trusted.
void settings_validate(settings_t *out);
