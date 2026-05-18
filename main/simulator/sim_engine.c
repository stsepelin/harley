#include "sim_engine.h"
#include "vehicle_data.h"
#include "gear_table.h"
#include "sim_math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define CYCLE_LEN_S        32.0f
#define TICK_S             0.05f
#define CLOCK_SIM_SPEEDUP  30.0f             // sim minutes vs real minutes
#define SECONDS_PER_DAY    (24.0f * 3600.0f)

// 32-second synthetic driving cycle that exercises every gear, the redline,
// neutral, turn signals, hazard flashers, every warning lamp, and the
// shift-light. Phases:
//
//   0..3 s    idle (N, ~900 rpm, 0 km/h), HAZARD blinking, immobiliser lit,
//             oil + battery warnings (pre-start lamp test)
//   3..5 s    idle, neutral, no signals — lamps clear
//   5..16 s   accelerating through gears 1..6 up to ~125 km/h. ABS warning
//             pulses 9..11 s, check-engine 13..15 s
//  16..21 s   WOT pull held at ~9500 rpm — shift-light flashes the whole
//             time. RPM is overridden directly so engine breathing wobbles
//             it within the shift-light band rather than dropping out.
//  21..25 s   cruise at 128 km/h, gear 6, left turn signal, HIGH BEAM on
//  25..31 s   deceleration, downshifting
//  31..32 s   idle stop, neutral
//
// Outside of the cycle: low_beam always on (riding with lights), fuel slowly
// drains so the low-fuel warning trips, engine_temp rises with RPM, odometer
// integrates speed, clock minute ticks ~30x real-time so it visibly advances.

// --- Per-tick computations -------------------------------------------------
// Each helper takes the current cycle position and returns one slice of the
// resulting vehicle_data_t. The task loop just stitches them together.

static void phase_outputs(float cycle_t,
                          float *speed, float *rpm, gear_t *gear,
                          bool *hazard, bool *left_signal, bool *high_beam)
{
    *speed = 0;
    *rpm = 900;
    *gear = GEAR_NEUTRAL;
    *hazard = *left_signal = *high_beam = false;

    if (cycle_t < 3.0f) {
        *hazard = true;
    } else if (cycle_t < 5.0f) {
        // quiet idle — already initialised above
    } else if (cycle_t < 16.0f) {
        // Acceleration ramp 0 -> ~125 km/h over 11 s.
        float a = (cycle_t - 5.0f) / 11.0f;
        *speed = 125.0f * a;
        *gear  = gear_for_speed(*speed, rpm);
    } else if (cycle_t < 21.0f) {
        // WOT pull near redline: hold RPM in the shift-light band so the
        // bezel ring flashes for the full 5 s window. Speed climbs slightly
        // from end-of-accel toward cruise.
        *speed = 125.0f + 3.0f * (cycle_t - 16.0f) / 5.0f;  // 125 -> 128
        *rpm   = 9500;
        *gear  = GEAR_6;
    } else if (cycle_t < 25.0f) {
        *speed = 128.0f;
        *rpm   = 6200;
        *gear  = GEAR_6;
        *left_signal = true;
        *high_beam   = true;
    } else if (cycle_t < 31.0f) {
        float a = (cycle_t - 25.0f) / 6.0f;
        *speed = 128.0f * (1.0f - a);
        *gear  = gear_for_speed(*speed, rpm);
    }
}

// Real tachs are never still. Combine a slow swell (mechanical inertia /
// throttle micro-corrections), a faster chatter (combustion irregularity),
// and a tiny LCG-derived noise. Amplitude scales gently with RPM so idle
// ripples while top-end gets lively.
static float apply_engine_breathing(float rpm, float t, uint32_t *lcg)
{
    *lcg = (*lcg) * 1664525u + 1013904223u;
    float swell   = sinf(t * 1.7f)        * (30.0f + rpm * 0.012f);
    float chatter = sinf(t * 9.2f + 0.4f) * (15.0f + rpm * 0.006f);
    float noise   = (((int32_t)((*lcg) >> 16) & 0xFFFF) - 32768)
                  * (12.0f + rpm * 0.0025f) / 32768.0f;
    rpm += swell + chatter + noise;
    if (rpm < 650.0f)  rpm = 650.0f;
    if (rpm > 9950.0f) rpm = 9950.0f;
    return rpm;
}

// Pre-start lamp test in the first idle phase, plus mid-cycle pulses to
// exercise each indicator.
static void apply_warning_lamps(vehicle_data_t *d, float cycle_t)
{
    d->oil_pressure_warning = (cycle_t < 1.5f);
    d->battery_warning      = (cycle_t < 1.5f);
    d->immobiliser_warning  = (cycle_t < 3.0f);
    d->abs_warning          = (cycle_t >= 9.0f  && cycle_t < 11.0f);
    d->check_engine         = (cycle_t >= 13.0f && cycle_t < 15.0f);
}

// Engine temp: idles at 92, climbs with RPM, overshoots into the high-temp
// warning band during the WOT pull.
static int8_t compute_engine_temp(float rpm, float cycle_t)
{
    float rpm_factor = (rpm - 900.0f) / 9100.0f;
    if (rpm_factor < 0.0f) rpm_factor = 0.0f;
    float temp_f = 92.0f + 25.0f * rpm_factor;
    if (cycle_t >= 16.0f && cycle_t < 21.0f) temp_f += 8.0f;  // WOT-pull overshoot
    return (int8_t)(temp_f + 0.5f);
}

static void sim_task(void *arg)
{
    (void)arg;
    float    t              = 0.0f;
    float    odo_m_accum    = 12847000.0f;       // float so fractional metres carry over ticks
    float    trip1_m_accum  = 0.0f;              // resets at boot — typical "trip A" behaviour
    float    trip2_m_accum  = 47800.0f;          // pre-loaded so trip B looks lived-in
    float    fuel_progress  = 0.0f;
    float    clock_seconds  = 8 * 3600 + 24 * 60; // start at 08:24
    uint32_t breathing_lcg  = 0xDEADBEEFu;
    vehicle_data_t data = {
        .engine_temp_c = 92,
        .fuel_level    = 4,
        .odometer_m    = (uint32_t)odo_m_accum,
    };

    while (1) {
        t += TICK_S;
        if (t >= CYCLE_LEN_S) t -= CYCLE_LEN_S;
        float cycle_t = t;

        // 1) Phase dispatch: base speed/rpm/gear and one-shot flags.
        float  speed, rpm;
        gear_t gear;
        bool   hazard, left_signal, high_beam;
        phase_outputs(cycle_t, &speed, &rpm, &gear, &hazard, &left_signal, &high_beam);

        // 2) Engine breathing wobbles the steady RPM target.
        rpm = apply_engine_breathing(rpm, t, &breathing_lcg);

        // 3) Stitch into the snapshot.
        bool blink_on = (((int)(cycle_t * 2.0f)) % 2 == 0);   // ~1 Hz
        data.speed_kmh  = (uint16_t)(speed + 0.5f);
        data.rpm        = (uint16_t)(rpm + 0.5f);
        data.gear       = gear;
        data.turn_left  = (hazard || left_signal) && blink_on;
        data.turn_right = hazard && blink_on;
        data.low_beam   = true;            // always on while running
        data.high_beam  = high_beam;

        apply_warning_lamps(&data, cycle_t);
        data.engine_temp_c = compute_engine_temp(rpm, cycle_t);

        // 4) Integrators: trip 1 starts at zero each boot so we can watch it
        //    climb; trip 2 keeps a larger pre-set value so the two are
        //    distinguishable in the demo.
        float dm = integrate_distance_m(speed, TICK_S);
        odo_m_accum   += dm;
        trip1_m_accum += dm;
        trip2_m_accum += dm;
        data.odometer_m = (uint32_t)odo_m_accum;
        data.trip1_m    = (uint32_t)trip1_m_accum;
        data.trip2_m    = (uint32_t)trip2_m_accum;

        // 5) Fuel cycles ~10 s per segment so the low-fuel icon clearly trips.
        fuel_tick(&fuel_progress, &data.fuel_level, TICK_S, 10.0f, 6);

        // 6) Mock clock advances faster than wall-clock so the minute digit
        //    visibly ticks during the demo.
        clock_seconds = clock_advance(clock_seconds, TICK_S * CLOCK_SIM_SPEEDUP, SECONDS_PER_DAY);
        clock_seconds_to_hm(clock_seconds, &data.clock_hours, &data.clock_minutes);

        vehicle_data_set(&data);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void sim_engine_start(void)
{
    xTaskCreatePinnedToCore(sim_task, "sim", 4096, NULL, 8, NULL, 0);
}
