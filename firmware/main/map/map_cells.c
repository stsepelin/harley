#include "map_cells.h"

#include <math.h>

// Floor division for signed a / positive b (C's / truncates toward zero, which
// would put a position at -0.3 deg in the wrong cell).
static int64_t floordiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if (a % b != 0 && (a < 0) != (b < 0))
        q--;
    return q;
}

map_cell_t map_cell_of(int32_t lon_e7, int32_t lat_e7, uint16_t cell_size_256)
{
    // idx = floor( (deg) / (cell_size_256/256) ) = floor( e7 * 256 / (1e7 * size) ).
    int64_t    den = (int64_t)10000000 * cell_size_256;
    map_cell_t c   = {
        .lat = (int32_t)floordiv((int64_t)lat_e7 * 256, den),
        .lon = (int32_t)floordiv((int64_t)lon_e7 * 256, den),
    };
    return c;
}

bool map_cell_eq(map_cell_t a, map_cell_t b)
{
    return a.lat == b.lat && a.lon == b.lon;
}

bool map_cell_in_window(map_cell_t cell, map_cell_t center, int radius)
{
    if (radius < 0)
        return false;
    int32_t dlat = cell.lat - center.lat, dlon = cell.lon - center.lon;
    if (dlat < 0)
        dlat = -dlat;
    if (dlon < 0)
        dlon = -dlon;
    return dlat <= radius && dlon <= radius;
}

int map_cell_window(map_cell_t center, int radius, map_cell_t *out, int cap)
{
    if (radius < 0)
        return 0;
    int side = 2 * radius + 1;
    int n    = side * side;
    if (cap < n)
        return 0;
    int i = 0;
    for (int dlat = -radius; dlat <= radius; dlat++)
        for (int dlon = -radius; dlon <= radius; dlon++)
            out[i++] = (map_cell_t){.lat = center.lat + dlat, .lon = center.lon + dlon};
    return n;
}

map_cell_t map_cell_ahead(map_cell_t center, double heading_deg)
{
    if (heading_deg < 0)
        return center;  // unknown / stationary: nothing to prefetch
    double     a = heading_deg * M_PI / 180.0;
    map_cell_t c = {
        .lat = center.lat + (int32_t)lround(cos(a)),  // 0 deg = north = +lat
        .lon = center.lon + (int32_t)lround(sin(a)),  // 90 deg = east  = +lon
    };
    return c;
}
