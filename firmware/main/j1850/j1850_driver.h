#pragma once
#include "j1850_vpw.h"
#include "odo_meter.h"

// Phase 3 Stage 3 producer: turns decoded J1850 frames into vehicle_data.
// It keeps a running aggregate (each broadcast carries one field) and
// pushes it to vehicle_data_set() whenever a frame updated something.
// The sniffer's decode path feeds it; enabled by CONFIG_VROD_J1850, which
// is mutually exclusive with sim_engine (both write vehicle_data).

void j1850_driver_init(void);

// Apply one decoded frame. Ignores unrecognised / bad-CRC frames.
void j1850_driver_feed(const j1850_frame_t *f);

// Odometer / trip persistence + user actions. The driver owns the live
// odo_meter (accumulated from the bus counters); odo_store persists it and the
// Trip settings page drives the resets. Each publishes to vehicle_data so the
// display updates immediately.
void j1850_driver_seed(const odo_meter_t *odo);   // restore saved totals at boot
void j1850_driver_snapshot(odo_meter_t *out);     // read totals for persistence
void j1850_driver_reset_trip(int idx);            // zero trip idx (0/1)
void j1850_driver_set_odometer(uint32_t meters);  // one-time mileage set

// Speed calibration: set/read the raw ECM count -> mph divisor at runtime
// (config write-back from the companion's GPS calibration).
void     j1850_driver_set_speed_divisor(uint16_t divisor);
uint16_t j1850_driver_speed_divisor(void);
