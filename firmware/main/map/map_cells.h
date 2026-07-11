#pragma once
#include <stdbool.h>
#include <stdint.h>

// Pure cell-paging logic for the worldwide map (firmware/docs/map-worldwide-plan.md).
// The card is tiled into a grid of lat/lon cells (one ZMTA per cell); the device
// keeps only the cells within a small radius of the rider's GPS resident and
// pages them as it moves. This module is the decision logic - which cell a
// position is in, which cells form the working set, which one to prefetch ahead
// - with no LVGL / SD / FreeRTOS, so it host-tests directly. The cell manager
// (map_source_open_cells) owns the actual open/close of cell files around it.
//
// Cells are addressed by integer index: a cell's south-west corner is
// (lon,lat) = (idx.lon, idx.lat) * cell_size, where cell_size is in 1/256 deg to
// match the on-card world.hdr. 1 deg cells => cell_size = 256.

typedef struct {
    int32_t lat, lon;  // cell index; SW corner = idx * cell_size (1/256 deg units)
} map_cell_t;

// The cell a position (1e-7 deg, as in gps_source) falls in. Floor division, so
// negative lat/lon (S/W) land in the right cell.
map_cell_t map_cell_of(int32_t lon_e7, int32_t lat_e7, uint16_t cell_size_256);

bool map_cell_eq(map_cell_t a, map_cell_t b);

// True if `cell` is within the (2*radius+1) square centred on `center`.
bool map_cell_in_window(map_cell_t cell, map_cell_t center, int radius);

// Fill `out` with the (2*radius+1)^2 cells of the working set centred on
// `center`, row-major (lat descending? no - lat then lon ascending). Returns the
// count, or 0 if `cap` is too small. The manager keeps exactly these open.
int map_cell_window(map_cell_t center, int radius, map_cell_t *out, int cap);

// The neighbour cell one step ahead along `heading_deg` (0 = north, clockwise),
// for prefetching the cell the rider is about to enter. Returns `center`
// unchanged when heading < 0 (unknown / stationary - nothing to prefetch).
map_cell_t map_cell_ahead(map_cell_t center, double heading_deg);
