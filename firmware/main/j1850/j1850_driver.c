#include "j1850_driver.h"
#include "gear_calc.h"
#include "j1850_parse.h"
#include "odo_meter.h"
#include "trip_meter.h"
#include "vehicle_data.h"
#include <string.h>

// Running aggregate: each J1850 broadcast updates one field, so we hold
// the last-known full picture and re-publish it on every recognised
// frame. RPM alone arrives ~15/s, well within what vehicle_data + the UI
// absorb, so no extra rate-limiting is needed here.
static vehicle_data_t s_vd;
static odo_meter_t    s_odo;         // odometer + trip totals (persisted by odo_store)
static trip_meter_t   s_dist_meter;  // A8 69 10 rolling counter -> distance ticks
static trip_meter_t   s_fuel_meter;  // A8 83 10 rolling counter -> fuel ticks
// Raw speed count -> mph divisor. Runtime-settable (config write-back from the
// companion's GPS calibration), persisted by the caller; defaults to the
// provisional constant until calibrated.
static uint16_t s_speed_divisor = J1850_SPEED_DIVISOR;

// A per-frame counter delta beyond this is an ECM reset or a dropped-frame gap,
// not a real 16-bit wrap (odo steps ~100 ticks/frame, fuel ~20), so the meter
// discards it. Generous enough to clear any realistic gap.
#define METER_MAX_JUMP 20000u

// Odometer tick = 0.4 m (2/5). Ride-1 timing-confirmed; refine with GPS.
static uint32_t odo_ticks_to_m(uint16_t ticks)
{
    return (uint32_t)ticks * 2u / 5u;
}

// Mirror the odometer/trip totals into the published snapshot for the display.
static void publish_odo(void)
{
    s_vd.odometer_m       = s_odo.odometer_m;
    s_vd.trip1_m          = s_odo.trip_m[0];
    s_vd.trip2_m          = s_odo.trip_m[1];
    s_vd.trip1_fuel_ticks = s_odo.trip_fuel[0];
    s_vd.trip2_fuel_ticks = s_odo.trip_fuel[1];
}

// Distance (A8 69 10 06) and fuel (A8 83 10 0A) are rolling 16-bit counters in
// data[4..5]. They're accumulated into the odo_meter here (stateful) rather
// than in the stateless parser. Returns true if the frame was one of these.
static bool accumulate(const j1850_frame_t *f)
{
    static const uint8_t DIST[] = {0xA8, 0x69, 0x10, 0x06};
    static const uint8_t FUEL[] = {0xA8, 0x83, 0x10, 0x0A};
    if (!f->crc_ok)
        return false;
    if (f->len >= 7 && memcmp(f->data, DIST, 4) == 0) {
        uint16_t raw = (uint16_t)((f->data[4] << 8) | f->data[5]);
        odo_meter_add(&s_odo, odo_ticks_to_m(trip_meter_delta(&s_dist_meter, raw, METER_MAX_JUMP)),
                      0);
        publish_odo();
        return true;
    }
    if (f->len >= 7 && memcmp(f->data, FUEL, 4) == 0) {
        uint16_t raw = (uint16_t)((f->data[4] << 8) | f->data[5]);
        odo_meter_add(&s_odo, 0, trip_meter_delta(&s_fuel_meter, raw, METER_MAX_JUMP));
        publish_odo();
        return true;
    }
    return false;
}

void j1850_driver_init(void)
{
    vehicle_data_get(&s_vd);  // seed from whatever the boot state left
    s_odo        = (odo_meter_t){0};
    s_dist_meter = (trip_meter_t){0};
    s_fuel_meter = (trip_meter_t){0};
}

void j1850_driver_feed(const j1850_frame_t *f)
{
    bool updated = j1850_parse(f, &s_vd);
    updated |= accumulate(f);
    if (updated) {
        // Re-apply the runtime divisor to the raw ECM count (parse used the
        // provisional default); a calibrated divisor from the phone lands here.
        // s_speed_divisor is never 0 (inits to J1850_SPEED_DIVISOR; the setter
        // rejects 0), so no divide-by-zero guard is needed - same as the setter.
        s_vd.speed_mph = (uint16_t)(s_vd.speed_raw / s_speed_divisor);
        // The bus carries no gear position (no sensor on this bike), so derive
        // it from the latest RPM:speed ratio. Recomputed on every republish;
        // RPM/speed frames keep both inputs fresh.
        s_vd.gear = gear_calc(s_vd.rpm, s_vd.speed_mph, s_vd.gear);
        vehicle_data_set(&s_vd);
    }
}

// Apply a calibrated raw->mph divisor and re-publish so the change is visible
// immediately. Ignores 0 (would divide by zero). Persistence is the caller's.
void j1850_driver_set_speed_divisor(uint16_t divisor)
{
    if (divisor == 0)
        return;
    s_speed_divisor = divisor;
    s_vd.speed_mph  = (uint16_t)(s_vd.speed_raw / s_speed_divisor);
    vehicle_data_set(&s_vd);
}

uint16_t j1850_driver_speed_divisor(void)
{
    return s_speed_divisor;
}

void j1850_driver_seed(const odo_meter_t *odo)
{
    s_odo = *odo;
    publish_odo();
    vehicle_data_set(&s_vd);
}

void j1850_driver_snapshot(odo_meter_t *out)
{
    *out = s_odo;
}

void j1850_driver_reset_trip(int idx)
{
    odo_meter_reset_trip(&s_odo, idx);
    publish_odo();
    vehicle_data_set(&s_vd);
}

void j1850_driver_set_odometer(uint32_t meters)
{
    odo_meter_set_odometer(&s_odo, meters);
    publish_odo();
    vehicle_data_set(&s_vd);
}
