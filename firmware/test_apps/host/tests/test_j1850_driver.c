// J1850 producer glue: decoded frame -> vehicle_data. Exercises the full
// feed -> parse -> vehicle_data_set -> vehicle_data_get chain on host
// (vehicle_data.c is real here), so a recognised frame reaches the store
// and an unrecognised one leaves it alone.

#include "unity.h"
#include "j1850_driver.h"
#include "j1850_vpw.h"
#include "vehicle_data.h"
#include <string.h>

static j1850_frame_t frame(const uint8_t *payload, size_t n)  // payload WITHOUT CRC
{
    j1850_frame_t f;
    memset(&f, 0, sizeof(f));
    memcpy(f.data, payload, n);
    f.data[n] = j1850_crc(payload, n);
    f.len     = n + 1;
    f.crc_ok  = true;
    return f;
}

static void test_recognised_frame_reaches_vehicle_data(void)
{
    vehicle_data_init();
    j1850_driver_init();

    uint8_t       rpm[] = {0x28, 0x1B, 0x10, 0x02, 0x14, 0x00};  // 0x1400 / 4
    j1850_frame_t f     = frame(rpm, sizeof(rpm));
    j1850_driver_feed(&f);

    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT16(1280, vd.rpm);
}

static void test_unrecognised_frame_leaves_vehicle_data(void)
{
    vehicle_data_init();
    j1850_driver_init();

    uint8_t       rpm[] = {0x28, 0x1B, 0x10, 0x02, 0x14, 0x00};
    j1850_frame_t f     = frame(rpm, sizeof(rpm));
    j1850_driver_feed(&f);

    uint8_t       ka[] = {0x29, 0xFE, 0x40, 0x01};  // IM keep-alive
    j1850_frame_t k    = frame(ka, sizeof(ka));
    j1850_driver_feed(&k);

    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT16(1280, vd.rpm);  // unchanged
}

static void test_odometer_accumulates_from_bus(void)
{
    vehicle_data_init();
    j1850_driver_init();

    // A8 69 10 06 <ctr> — first reading seeds (no distance yet).
    uint8_t       seed[] = {0xA8, 0x69, 0x10, 0x06, 0x00, 0x64};  // ctr 100
    j1850_frame_t f      = frame(seed, sizeof(seed));
    j1850_driver_feed(&f);
    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT32(0, vd.odometer_m);

    // ctr 200 -> +100 ticks x 0.4 m = 40 m, mirrored into both trips.
    uint8_t       next[] = {0xA8, 0x69, 0x10, 0x06, 0x00, 0xC8};
    j1850_frame_t g      = frame(next, sizeof(next));
    j1850_driver_feed(&g);
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT32(40, vd.odometer_m);
    TEST_ASSERT_EQUAL_UINT32(40, vd.trip1_m);
    TEST_ASSERT_EQUAL_UINT32(40, vd.trip2_m);
}

static void test_fuel_ticks_accumulate_from_bus(void)
{
    vehicle_data_init();
    j1850_driver_init();

    uint8_t       seed[] = {0xA8, 0x83, 0x10, 0x0A, 0x01, 0xE0};  // ctr 480
    j1850_frame_t f      = frame(seed, sizeof(seed));
    j1850_driver_feed(&f);
    uint8_t       next[] = {0xA8, 0x83, 0x10, 0x0A, 0x01, 0xF4};  // ctr 500 (+20)
    j1850_frame_t g      = frame(next, sizeof(next));
    j1850_driver_feed(&g);

    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT32(20, vd.trip1_fuel_ticks);
    TEST_ASSERT_EQUAL_UINT32(20, vd.trip2_fuel_ticks);
}

static void test_seed_restores_totals(void)
{
    vehicle_data_init();
    j1850_driver_init();

    odo_meter_t saved = {.odometer_m = 12345, .trip_m = {40, 60}, .trip_fuel = {2, 3}};
    j1850_driver_seed(&saved);

    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT32(12345, vd.odometer_m);
    TEST_ASSERT_EQUAL_UINT32(40, vd.trip1_m);
    TEST_ASSERT_EQUAL_UINT32(60, vd.trip2_m);
    TEST_ASSERT_EQUAL_UINT32(2, vd.trip1_fuel_ticks);

    // A snapshot round-trips the same totals for persistence.
    odo_meter_t snap;
    j1850_driver_snapshot(&snap);
    TEST_ASSERT_EQUAL_UINT32(12345, snap.odometer_m);
    TEST_ASSERT_EQUAL_UINT32(60, snap.trip_m[1]);
}

static void test_reset_trip_via_driver(void)
{
    vehicle_data_init();
    j1850_driver_init();
    odo_meter_t saved = {.odometer_m = 500, .trip_m = {500, 500}, .trip_fuel = {10, 10}};
    j1850_driver_seed(&saved);

    j1850_driver_reset_trip(0);

    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT32(0, vd.trip1_m);       // reset
    TEST_ASSERT_EQUAL_UINT32(500, vd.trip2_m);     // other trip kept
    TEST_ASSERT_EQUAL_UINT32(500, vd.odometer_m);  // odometer kept
}

static void test_set_odometer_via_driver(void)
{
    vehicle_data_init();
    j1850_driver_init();
    j1850_driver_set_odometer(80000000u);  // 80 000 km

    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT32(80000000u, vd.odometer_m);
}

static void test_speed_divisor_runtime(void)
{
    vehicle_data_init();
    j1850_driver_init();
    TEST_ASSERT_EQUAL_UINT16(188, j1850_driver_speed_divisor());  // gear-ratio default

    uint8_t       speed[] = {0x48, 0x29, 0x10, 0x02, 0x4C, 0x2C};  // raw 19500
    j1850_frame_t f       = frame(speed, sizeof(speed));
    j1850_driver_feed(&f);

    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT16(19500, vd.speed_raw);
    TEST_ASSERT_EQUAL_UINT16(103, vd.speed_mph);  // 19500 / 188

    // Calibrate: smaller divisor -> higher mph, applied to the last frame now.
    j1850_driver_set_speed_divisor(130);
    TEST_ASSERT_EQUAL_UINT16(130, j1850_driver_speed_divisor());
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT16(150, vd.speed_mph);  // 19500 / 130

    // 0 is rejected (no divide-by-zero, divisor unchanged).
    j1850_driver_set_speed_divisor(0);
    TEST_ASSERT_EQUAL_UINT16(130, j1850_driver_speed_divisor());
}

static void test_bad_crc_counter_is_ignored(void)
{
    vehicle_data_init();
    j1850_driver_init();

    uint8_t       seed[] = {0xA8, 0x69, 0x10, 0x06, 0x00, 0x64};
    j1850_frame_t f      = frame(seed, sizeof(seed));
    j1850_driver_feed(&f);  // seed

    uint8_t       next[] = {0xA8, 0x69, 0x10, 0x06, 0x00, 0xC8};
    j1850_frame_t b      = frame(next, sizeof(next));
    b.crc_ok             = false;  // corrupt in flight
    j1850_driver_feed(&b);

    vehicle_data_t vd;
    vehicle_data_get(&vd);
    TEST_ASSERT_EQUAL_UINT32(0, vd.odometer_m);  // bad CRC must not accumulate
}

void RunTests(void)
{
    RUN_TEST(test_recognised_frame_reaches_vehicle_data);
    RUN_TEST(test_unrecognised_frame_leaves_vehicle_data);
    RUN_TEST(test_odometer_accumulates_from_bus);
    RUN_TEST(test_fuel_ticks_accumulate_from_bus);
    RUN_TEST(test_seed_restores_totals);
    RUN_TEST(test_reset_trip_via_driver);
    RUN_TEST(test_set_odometer_via_driver);
    RUN_TEST(test_speed_divisor_runtime);
    RUN_TEST(test_bad_crc_counter_is_ignored);
}
