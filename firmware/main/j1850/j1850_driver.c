#include "j1850_driver.h"
#include "gear_calc.h"
#include "j1850_parse.h"
#include "trip_meter.h"
#include "vehicle_data.h"
#include <string.h>

// Running aggregate: each J1850 broadcast updates one field, so we hold
// the last-known full picture and re-publish it on every recognised
// frame. RPM alone arrives ~15/s, well within what vehicle_data + the UI
// absorb, so no extra rate-limiting is needed here.
static vehicle_data_t s_vd;
static trip_meter_t   s_dist_meter;  // A8 69 10 odometer ticks
static trip_meter_t   s_fuel_meter;  // A8 83 10 fuel-consumption ticks

// A per-frame counter delta beyond this is an ECM reset or a dropped-frame gap,
// not a real 16-bit wrap (odo steps ~100 ticks/frame, fuel ~20), so the meter
// discards it. Generous enough to clear any realistic gap.
#define METER_MAX_JUMP 20000u

// Odometer tick = 0.4 m (2/5). Ride-1 timing-confirmed; refine with GPS.
static uint32_t odo_ticks_to_m(uint16_t ticks)
{
    return (uint32_t)ticks * 2u / 5u;
}

// Distance (A8 69 10 06) and fuel (A8 83 10 0A) are rolling 16-bit counters in
// data[4..5]. They're accumulated here rather than in j1850_parse because that
// stays stateless. Returns true if the frame was one of these counters.
static bool accumulate(const j1850_frame_t *f, vehicle_data_t *vd)
{
    static const uint8_t DIST[] = {0xA8, 0x69, 0x10, 0x06};
    static const uint8_t FUEL[] = {0xA8, 0x83, 0x10, 0x0A};
    if (!f->crc_ok)
        return false;
    if (f->len >= 7 && memcmp(f->data, DIST, 4) == 0) {
        uint16_t raw = (uint16_t)((f->data[4] << 8) | f->data[5]);
        uint32_t m   = odo_ticks_to_m(trip_meter_delta(&s_dist_meter, raw, METER_MAX_JUMP));
        vd->odometer_m += m;
        vd->trip1_m += m;
        vd->trip2_m += m;
        return true;
    }
    if (f->len >= 7 && memcmp(f->data, FUEL, 4) == 0) {
        uint16_t raw = (uint16_t)((f->data[4] << 8) | f->data[5]);
        vd->fuel_ticks += trip_meter_delta(&s_fuel_meter, raw, METER_MAX_JUMP);
        return true;
    }
    return false;
}

void j1850_driver_init(void)
{
    vehicle_data_get(&s_vd);  // seed from whatever the boot state left
    s_dist_meter = (trip_meter_t){0};
    s_fuel_meter = (trip_meter_t){0};
}

void j1850_driver_feed(const j1850_frame_t *f)
{
    bool updated = j1850_parse(f, &s_vd);
    updated |= accumulate(f, &s_vd);
    if (updated) {
        // The bus carries no gear position (no sensor on this bike), so derive
        // it from the latest RPM:speed ratio. Recomputed on every republish;
        // RPM/speed frames keep both inputs fresh.
        s_vd.gear = gear_calc(s_vd.rpm, s_vd.speed_mph, s_vd.gear);
        vehicle_data_set(&s_vd);
    }
}
