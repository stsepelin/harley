// telemetry_codec: cluster -> phone vehicle_data frame encoder. The exact
// byte layout asserted here is the cross-check fixture for the companion's
// TelemetryCodec.kt - keep the two in lock-step.

#include "telemetry_codec.h"
#include "unity.h"
#include <string.h>

static vehicle_data_t sample(void)
{
    vehicle_data_t vd;
    memset(&vd, 0, sizeof(vd));
    vd.speed_raw        = 0x3000;  // 12288 raw ECM counts
    vd.speed_mph        = 63;
    vd.rpm              = 3200;
    vd.gear             = GEAR_3;
    vd.engine_temp_c    = -7;
    vd.fuel_level       = 5;
    vd.low_beam         = true;     // lamp bit 2
    vd.neutral          = true;     // lamp bit 4  -> lamps = 0x0014
    vd.odometer_m       = 1000000;  // 0x000F4240
    vd.trip1_m          = 12345;    // 0x00003039
    vd.trip2_m          = 54321;    // 0x0000D431
    vd.trip1_fuel_ticks = 1111;     // 0x00000457
    vd.trip2_fuel_ticks = 2222;     // 0x000008AE
    vd.clock_hours      = 8;
    vd.clock_minutes    = 24;
    return vd;
}

static void test_encode_exact_frame(void)
{
    static const uint8_t expected[TELEMETRY_FRAME_LEN] = {
        0x40, 0x21, 0x00,        // type, payload_len = 33
        0x00, 0x30,              // speed_raw
        0x3F, 0x00,              // speed_mph
        0x80, 0x0C,              // rpm
        0x03,                    // gear
        0xF9,                    // engine_temp_c (-7)
        0x05,                    // fuel_level
        0x14, 0x00,              // lamps (low_beam | neutral)
        0x40, 0x42, 0x0F, 0x00,  // odometer_m
        0x39, 0x30, 0x00, 0x00,  // trip1_m
        0x31, 0xD4, 0x00, 0x00,  // trip2_m
        0x57, 0x04, 0x00, 0x00,  // trip1_fuel_ticks
        0xAE, 0x08, 0x00, 0x00,  // trip2_fuel_ticks
        0x08, 0x18,              // clock h:m
    };
    vehicle_data_t vd = sample();
    uint8_t        out[TELEMETRY_FRAME_LEN];
    size_t         n = telemetry_encode(&vd, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(TELEMETRY_FRAME_LEN, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, TELEMETRY_FRAME_LEN);
}

static void test_all_lamps_pack(void)
{
    vehicle_data_t vd;
    memset(&vd, 0, sizeof(vd));
    vd.turn_left = vd.turn_right = vd.low_beam = vd.high_beam = vd.neutral = true;
    vd.oil_pressure_warning = vd.check_engine = vd.abs_warning = true;
    vd.battery_warning = vd.immobiliser_warning = true;

    uint8_t out[TELEMETRY_FRAME_LEN];
    TEST_ASSERT_EQUAL_UINT(TELEMETRY_FRAME_LEN, telemetry_encode(&vd, out, sizeof(out)));
    uint16_t lamps = (uint16_t)(out[12] | (out[13] << 8));
    TEST_ASSERT_EQUAL_HEX16(0x03FF, lamps);  // bits 0..9 all set
}

static void test_buffer_too_small_returns_zero(void)
{
    vehicle_data_t vd = sample();
    uint8_t        out[TELEMETRY_FRAME_LEN - 1];
    TEST_ASSERT_EQUAL_UINT(0, telemetry_encode(&vd, out, sizeof(out)));
}

void RunTests(void)
{
    RUN_TEST(test_encode_exact_frame);
    RUN_TEST(test_all_lamps_pack);
    RUN_TEST(test_buffer_too_small_returns_zero);
}
