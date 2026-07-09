// odo_meter: odometer + dual-trip accumulation, reset, and set-odometer.

#include "odo_meter.h"
#include "unity.h"

static void test_add_advances_odometer_and_both_trips(void)
{
    odo_meter_t m = {0};
    odo_meter_add(&m, 40, 3);
    odo_meter_add(&m, 60, 2);
    TEST_ASSERT_EQUAL_UINT32(100, m.odometer_m);
    TEST_ASSERT_EQUAL_UINT32(100, m.trip_m[0]);
    TEST_ASSERT_EQUAL_UINT32(100, m.trip_m[1]);
    TEST_ASSERT_EQUAL_UINT32(5, m.trip_fuel[0]);
    TEST_ASSERT_EQUAL_UINT32(5, m.trip_fuel[1]);
    TEST_ASSERT_EQUAL_UINT32(5, m.fuel_total);
}

static void test_reset_trip_zeros_only_that_trip(void)
{
    odo_meter_t m = {0};
    odo_meter_add(&m, 100, 10);
    odo_meter_reset_trip(&m, 0);
    TEST_ASSERT_EQUAL_UINT32(0, m.trip_m[0]);
    TEST_ASSERT_EQUAL_UINT32(0, m.trip_fuel[0]);
    TEST_ASSERT_EQUAL_UINT32(100, m.trip_m[1]);  // other trip untouched
    TEST_ASSERT_EQUAL_UINT32(10, m.trip_fuel[1]);
    TEST_ASSERT_EQUAL_UINT32(100, m.odometer_m);  // odometer never reset
    TEST_ASSERT_EQUAL_UINT32(10, m.fuel_total);   // lifetime fuel never reset
    // Accumulation continues independently after a reset.
    odo_meter_add(&m, 50, 5);
    TEST_ASSERT_EQUAL_UINT32(50, m.trip_m[0]);
    TEST_ASSERT_EQUAL_UINT32(150, m.trip_m[1]);
}

static void test_reset_trip_out_of_range_is_ignored(void)
{
    odo_meter_t m = {0};
    odo_meter_add(&m, 100, 10);
    odo_meter_reset_trip(&m, -1);
    odo_meter_reset_trip(&m, 2);
    TEST_ASSERT_EQUAL_UINT32(100, m.trip_m[0]);
    TEST_ASSERT_EQUAL_UINT32(100, m.trip_m[1]);
}

static void test_set_odometer_leaves_trips(void)
{
    odo_meter_t m = {0};
    odo_meter_add(&m, 100, 10);
    odo_meter_set_odometer(&m, 50000000u);  // seed 50 000 km
    TEST_ASSERT_EQUAL_UINT32(50000000u, m.odometer_m);
    TEST_ASSERT_EQUAL_UINT32(100, m.trip_m[0]);  // trips unaffected
}

void RunTests(void)
{
    RUN_TEST(test_add_advances_odometer_and_both_trips);
    RUN_TEST(test_reset_trip_zeros_only_that_trip);
    RUN_TEST(test_reset_trip_out_of_range_is_ignored);
    RUN_TEST(test_set_odometer_leaves_trips);
}
