#pragma once
#include <stdint.h>

// On-device mirror of tools/maptiles/styles.py. The baker stores only a style
// id per feature; the id -> colour/width mapping lives here so restyling never
// needs a re-bake. Colours are RGB565 (the cluster's native format).

#define MAP_RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define MAP_STYLE_MINOR    0   // residential, service, unclassified
#define MAP_STYLE_MID      1   // secondary, tertiary
#define MAP_STYLE_PRIMARY  2   // primary
#define MAP_STYLE_MAJOR    3   // motorway, trunk
#define MAP_STYLE_WATER    10  // water fill
#define MAP_STYLE_BUILDING 11  // building footprint fill

#define MAP_BG565 MAP_RGB565(0x10, 0x11, 0x16)

typedef struct {
    uint16_t color;
    uint8_t  width;  // stroke width in px at 256 px/tile; scaled at render time
} map_style_t;

// Base stroke width at 256 px/tile; the renderer scales by (ppt / 256).
static inline map_style_t map_style(uint8_t id)
{
    switch (id) {
    case MAP_STYLE_MID:
        return (map_style_t){MAP_RGB565(0x7A, 0x80, 0x8A), 6};
    case MAP_STYLE_PRIMARY:
        return (map_style_t){MAP_RGB565(0xC9, 0xA2, 0x4B), 9};
    case MAP_STYLE_MAJOR:
        return (map_style_t){MAP_RGB565(0xE6, 0xB8, 0x4B), 12};
    case MAP_STYLE_WATER:
        return (map_style_t){MAP_RGB565(0x1E, 0x33, 0x49), 0};
    case MAP_STYLE_BUILDING:
        return (map_style_t){MAP_RGB565(0x26, 0x28, 0x2F), 0};
    case MAP_STYLE_MINOR:
    default:
        return (map_style_t){MAP_RGB565(0x53, 0x57, 0x5E), 3};
    }
}
