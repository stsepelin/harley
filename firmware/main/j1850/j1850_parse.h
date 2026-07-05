#pragma once
#include "j1850_vpw.h"
#include "vehicle_data.h"
#include <stdbool.h>

// Pure J1850 message decoder: a decoded VPW frame -> vehicle_data fields.
// Decode table is HarleyDroid-derived and bench-confirmed against the
// 2026-07-04 on-bike capture (firmware/docs/captures/). Host-tested.

// SPEED — units settled, magnitude provisional.
// This is a US-market V-Rod: the stock cluster reads MILES, so the ECM's
// bus value is mph-based ("native"). vehicle_data.speed_mph is now
// mph-canonical (the sim producer converts to mph too), so the decoded
// value is stored raw with no unit conversion here; the DISPLAY layer does
// the mph->km/h conversion when the user selects metric.
//
// Only the DIVISOR MAGNITUDE is still PROVISIONAL — 128 is a guess for
// raw->mph. Confirm on a ride against the STOCK SPEEDOMETER, which is
// mechanically driven off this same J1850 bus: the sniffer logs the decoded
// speed ("speed:" line); read the stock dial in its native MILES (ignore
// the km/h sticker — the mechanism reads mph) at steady 30/50/70 and match.
// The stock speedo may read ~5-10% optimistic, so this is a coarse check;
// correct DIV if off by a clean factor. (No onboard GPS.)
#define J1850_SPEED_DIVISOR 128

// Decode one frame into *vd. Each broadcast is single-purpose, so this
// updates only the field(s) that frame carries and leaves the rest of
// *vd untouched — the caller keeps a running aggregate and pushes it to
// vehicle_data_set() on a cadence. Returns true if the frame was a
// recognised vehicle message; IM keep-alives / ECM diagnostics / bad-CRC
// frames return false and touch nothing.
bool j1850_parse(const j1850_frame_t *f, vehicle_data_t *vd);
