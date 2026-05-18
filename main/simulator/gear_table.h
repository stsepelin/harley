#pragma once
#include "vehicle_data.h"

// Pure mapping from road speed (km/h) to (gear, engine RPM). Free function
// with no external dependencies so it's testable on a host.
//
// `out_rpm` receives the RPM that matches `speed_kmh` in the returned gear.
// The split points and per-gear RPM curves model a V-Rod gearbox; tune here
// if the bike's shift behaviour ever needs to feel different in the sim.
gear_t gear_for_speed(float speed_kmh, float *out_rpm);
