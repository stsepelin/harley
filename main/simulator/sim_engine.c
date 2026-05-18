#include "sim_engine.h"
#include "vehicle_data.h"
#include "gear_table.h"
#include "sim_math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define CYCLE_LEN_S  32.0f
#define TICK_S       0.05f

// 32-second synthetic driving cycle that exercises every gear, the redline,
// neutral, turn signals, hazard flashers, and every warning lamp.
//
// Phases:
//   0..3 s    idle (N, ~900 rpm, 0 km/h), HAZARD blinking, immobiliser lit,
//             oil + battery warnings (pre-start lamp test)
//   3..5 s    idle, neutral, no signals — lamps clear
//   5..20 s   accelerating through gears 1..6, briefly hits redline. ABS
//             warning pulses around 9..11 s, check-engine 13..15 s
//  20..25 s   cruise at 130 km/h, gear 6, left turn signal, HIGH BEAM on
//  25..31 s   deceleration, downshifting
//  31..32 s   idle stop, neutral
//
// Outside of the cycle: low_beam always on (riding with lights), fuel slowly
// drains so the low-fuel warning trips, engine_temp rises with RPM, odometer
// integrates speed, clock minute ticks ~2x real-time so it visibly advances.

static void sim_task(void *arg)
{
    float t = 0.0f;
    float odo_m_accum   = 12847000.0f;  // float so fractional metres carry over ticks
    float trip1_m_accum = 0.0f;         // resets at boot — typical "trip A" behaviour
    float trip2_m_accum = 47800.0f;     // pre-loaded so trip B looks lived-in
    float fuel_progress = 0.0f;         // 0..(FUEL_CYCLE_S) — drives fuel cycling
    float clock_seconds = 8 * 3600 + 24 * 60;  // start at 08:24
    vehicle_data_t data = {
        .engine_temp_c = 92,
        .fuel_level = 4,
        .odometer_m = (uint32_t)odo_m_accum,
    };

    while (1) {
        t += TICK_S;
        if (t >= CYCLE_LEN_S) t -= CYCLE_LEN_S;
        float cycle_t = t;
        float rpm = 900;
        float speed = 0;
        gear_t gear = GEAR_NEUTRAL;
        bool blink_on = (((int)(cycle_t * 2.0f)) % 2 == 0);  // ~1 Hz
        bool hazard = false;
        bool left_signal = false;
        bool high_beam = false;

        if (cycle_t < 3.0f) {
            speed = 0; rpm = 900; gear = GEAR_NEUTRAL;
            hazard = true;
        } else if (cycle_t < 5.0f) {
            speed = 0; rpm = 900; gear = GEAR_NEUTRAL;
        } else if (cycle_t < 20.0f) {
            float a = (cycle_t - 5.0f) / 15.0f;
            speed = 130.0f * a;
            gear = gear_for_speed(speed, &rpm);
        } else if (cycle_t < 25.0f) {
            speed = 128.0f;
            rpm = 6200;
            gear = GEAR_6;
            left_signal = true;
            high_beam = true;
        } else if (cycle_t < 31.0f) {
            float a = (cycle_t - 25.0f) / 6.0f;
            speed = 130.0f * (1.0f - a);
            gear = gear_for_speed(speed, &rpm);
        } else {
            speed = 0; rpm = 900; gear = GEAR_NEUTRAL;
        }

        // Engine breathing: real tachs are never still. Combine a slow swell
        // (mechanical inertia / throttle micro-corrections), a faster chatter
        // (combustion irregularity), and a tiny LCG-derived noise. Amplitude
        // scales gently with RPM so idle ripples while top-end gets lively.
        static uint32_t lcg = 0xDEADBEEFu;
        lcg = lcg * 1664525u + 1013904223u;
        float swell   = sinf(t * 1.7f)        * (30.0f + rpm * 0.012f);
        float chatter = sinf(t * 9.2f + 0.4f) * (15.0f + rpm * 0.006f);
        float noise   = (((int32_t)(lcg >> 16) & 0xFFFF) - 32768)
                      * (12.0f + rpm * 0.0025f) / 32768.0f;
        rpm += swell + chatter + noise;
        if (rpm < 650.0f)  rpm = 650.0f;
        if (rpm > 9950.0f) rpm = 9950.0f;

        data.speed_kmh = (uint16_t)(speed + 0.5f);
        data.rpm = (uint16_t)(rpm + 0.5f);
        data.gear = gear;

        data.turn_left  = (hazard || left_signal) && blink_on;
        data.turn_right = hazard && blink_on;

        // Lights: low beam always on while running; high beam during cruise.
        data.low_beam  = true;
        data.high_beam = high_beam;

        // Warning lamps. Pre-start lamp test in the first idle phase, plus
        // mid-cycle pulses to exercise each indicator.
        data.oil_pressure_warning = (cycle_t < 1.5f);
        data.battery_warning      = (cycle_t < 1.5f);
        data.immobiliser_warning  = (cycle_t < 3.0f);
        data.abs_warning          = (cycle_t >= 9.0f  && cycle_t < 11.0f);
        data.check_engine         = (cycle_t >= 13.0f && cycle_t < 15.0f);

        // Engine temp: idles at 92, climbs with RPM, briefly overshoots into
        // the high-temp warning band near the redline burst.
        float rpm_factor = (rpm - 900.0f) / 9100.0f;
        if (rpm_factor < 0.0f) rpm_factor = 0.0f;
        float temp_f = 92.0f + 25.0f * rpm_factor;
        if (cycle_t >= 18.0f && cycle_t < 20.5f) temp_f += 8.0f;  // redline overshoot
        data.engine_temp_c = (int8_t)(temp_f + 0.5f);

        // Odometer + trips: integrate speed each tick. Trip 1 starts at zero
        // each boot so we can watch it climb; trip 2 keeps a larger pre-set
        // value so the two are distinguishable in the demo.
        float dm = integrate_distance_m(speed, TICK_S);
        odo_m_accum   += dm;
        trip1_m_accum += dm;
        trip2_m_accum += dm;
        data.odometer_m = (uint32_t)odo_m_accum;
        data.trip1_m    = (uint32_t)trip1_m_accum;
        data.trip2_m    = (uint32_t)trip2_m_accum;

        // Fuel: drain 1 segment every ~10 s of sim time, refill from 0 → 6.
        // Picks up the icon-red threshold (≤1) clearly during the demo.
        fuel_tick(&fuel_progress, &data.fuel_level, TICK_S, 10.0f, 6);

        // Mock clock: advance ~30x real-time so the minute digit visibly
        // ticks every two seconds.
        clock_seconds = clock_advance(clock_seconds, TICK_S * 30.0f, 24.0f * 3600.0f);
        clock_seconds_to_hm(clock_seconds, &data.clock_hours, &data.clock_minutes);

        vehicle_data_set(&data);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void sim_engine_start(void)
{
    xTaskCreatePinnedToCore(sim_task, "sim", 4096, NULL, 8, NULL, 0);
}
