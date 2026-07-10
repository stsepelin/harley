#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "units.h"

// PWM duty cycles below this value put the Waveshare LCD into a
// non-visible state on this BSP (observed: 20% reads as full black).
// Floor everything below this so the user can't lock themselves out.
#define SETTINGS_BRIGHTNESS_MIN  30u

// Speed calibration bounds. Raw ECM count / divisor = mph. 188 is the
// gear-ratio-physics value confirmed by Ride 1 AND Ride 2 (~117 counts/km-h)
// and by a roadside-radar cross-check (see ride-2-findings.md); a companion
// GPS calibration can still refine it and persist over this default.
#define SETTINGS_SPEED_DIVISOR_DEFAULT 188u
#define SETTINGS_SPEED_DIVISOR_MIN     50u
#define SETTINGS_SPEED_DIVISOR_MAX     400u

// Persisted user preferences. Loaded once at boot, written by the
// settings screen on change. Keep small — anything bigger than a few
// bytes per field should probably be its own NVS namespace.

// Which driving view the cluster shows. Persisted so the choice is permanent
// (not a per-boot / accidental toggle) and so a classic setting never loads the
// map at all until it is selected.
typedef enum {
    LAYOUT_CLASSIC = 0,  // full round gauge
    LAYOUT_MAP     = 1,  // compact moving-map cluster
} layout_t;

typedef struct {
    display_units_t units;                 // kph or mph
    temp_units_t    temp_units;            // celsius or fahrenheit
    uint8_t         brightness;            // 30..100 (mapped to the BSP duty cycle)
    bool            sound_enabled;         // master mute / unmute
    uint8_t         volume;                // 0..100 (codec out_vol)
    bool            ble_visible_override;  // force undirected BLE adv even when bonded
    uint16_t        speed_divisor;         // raw ECM count -> mph (GPS-calibrated)
    layout_t        layout;                // classic gauge or moving map
} settings_t;

// Hardcoded fallback values. Used when NVS is empty or unreadable.
void settings_default(settings_t *out);

// Clamp fields to valid ranges in-place. Idempotent — call after every
// load so the rest of the firmware can treat the struct as trusted.
void settings_validate(settings_t *out);
