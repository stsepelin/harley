#include "unity.h"
#include "vehicle_data.h"
#include "freertos_stub.h"
#include <string.h>

// Distinctive non-zero sentinel struct — every field different so a
// memcpy regression in vehicle_data_set/get would surface visibly.
static vehicle_data_t sample_payload(void)
{
    vehicle_data_t d = {
        .speed_mph            = 128,
        .rpm                  = 6200,
        .gear                 = GEAR_5,
        .engine_temp_c        = 92,
        .fuel_level           = 4,
        .turn_left            = true,
        .turn_right           = false,
        .low_beam             = true,
        .high_beam            = true,
        .neutral              = false,
        .oil_pressure_warning = false,
        .check_engine         = true,
        .abs_warning          = false,
        .battery_warning      = false,
        .immobiliser_warning  = false,
        .odometer_m           = 12847000u,
        .trip1_m              = 1234u,
        .trip2_m              = 47800u,
        .clock_hours          = 8,
        .clock_minutes        = 24,
    };
    return d;
}

// --- init ---------------------------------------------------------------

static void test_init_zeroes_and_neutral(void)
{
    freertos_stub_reset();
    vehicle_data_init();

    vehicle_data_t got;
    memset(&got, 0xAA, sizeof(got));   // poison so reads must overwrite
    vehicle_data_get(&got);

    TEST_ASSERT_EQUAL_UINT16(0, got.speed_mph);
    TEST_ASSERT_EQUAL_UINT16(0, got.rpm);
    TEST_ASSERT_EQUAL_INT(GEAR_NEUTRAL, got.gear);
    TEST_ASSERT_EQUAL_UINT8(0, got.fuel_level);
    TEST_ASSERT_FALSE(got.turn_left);
    TEST_ASSERT_FALSE(got.high_beam);
    TEST_ASSERT_EQUAL_UINT32(0, got.odometer_m);
    TEST_ASSERT_EQUAL_UINT32(0, got.trip1_m);
    TEST_ASSERT_EQUAL_UINT32(0, got.trip2_m);
}

// --- happy-path roundtrip ----------------------------------------------

static void test_set_then_get_roundtrip(void)
{
    freertos_stub_reset();
    vehicle_data_init();

    vehicle_data_t in = sample_payload();
    vehicle_data_set(&in);

    vehicle_data_t out;
    memset(&out, 0xCC, sizeof(out));
    vehicle_data_get(&out);

    TEST_ASSERT_EQUAL_MEMORY(&in, &out, sizeof(in));
}

// Multiple writes: each get sees the most recent set.
static void test_latest_set_wins(void)
{
    freertos_stub_reset();
    vehicle_data_init();

    vehicle_data_t a = sample_payload();
    a.speed_mph      = 50;
    vehicle_data_set(&a);

    vehicle_data_t b = sample_payload();
    b.speed_mph      = 99;
    vehicle_data_set(&b);

    vehicle_data_t got;
    vehicle_data_get(&got);
    TEST_ASSERT_EQUAL_UINT16(99, got.speed_mph);
}

// --- timeout / contention paths ----------------------------------------

static void test_set_is_dropped_when_take_fails(void)
{
    freertos_stub_reset();
    vehicle_data_init();

    // First write succeeds → known good state.
    vehicle_data_t baseline = sample_payload();
    vehicle_data_set(&baseline);

    // Second write hits a timeout — must NOT mutate the stored data.
    g_stub_take_succeeds = 0;
    vehicle_data_t doomed = sample_payload();
    doomed.speed_mph      = 999;
    vehicle_data_set(&doomed);

    g_stub_take_succeeds = 1;
    vehicle_data_t got;
    vehicle_data_get(&got);
    TEST_ASSERT_EQUAL_UINT16(baseline.speed_mph, got.speed_mph);
}

static void test_get_leaves_out_untouched_on_take_fail(void)
{
    freertos_stub_reset();
    vehicle_data_init();

    vehicle_data_t in = sample_payload();
    vehicle_data_set(&in);

    // Reader hits a timeout: the caller's stack buffer must keep whatever
    // was there before. We poison it and confirm bytes are unchanged.
    g_stub_take_succeeds = 0;
    vehicle_data_t out;
    memset(&out, 0xEE, sizeof(out));
    vehicle_data_get(&out);

    uint8_t expected[sizeof(out)];
    memset(expected, 0xEE, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(expected, &out, sizeof(out));
}

// Every successful take must be paired with exactly one give — otherwise
// on real hardware the mutex would slowly leak counts and deadlock.
static void test_take_give_balance(void)
{
    freertos_stub_reset();
    vehicle_data_init();

    vehicle_data_t d = sample_payload();
    for (int i = 0; i < 50; i++) {
        vehicle_data_set(&d);
        vehicle_data_t out;
        vehicle_data_get(&out);
    }
    // 50 sets + 50 gets = 100 successful takes, each must give exactly once.
    TEST_ASSERT_EQUAL_INT(100, g_stub_take_calls);
    TEST_ASSERT_EQUAL_INT(100, g_stub_give_calls);
}

// A failing take must NOT trigger a give — otherwise the mutex would be
// released while still locked by someone else.
static void test_failed_take_does_not_give(void)
{
    freertos_stub_reset();
    vehicle_data_init();

    g_stub_take_succeeds = 0;
    vehicle_data_t d = sample_payload();
    vehicle_data_set(&d);
    vehicle_data_t out;
    vehicle_data_get(&out);

    TEST_ASSERT_EQUAL_INT(2, g_stub_take_calls);
    TEST_ASSERT_EQUAL_INT(0, g_stub_give_calls);
}

void RunTests(void)
{
    RUN_TEST(test_init_zeroes_and_neutral);
    RUN_TEST(test_set_then_get_roundtrip);
    RUN_TEST(test_latest_set_wins);
    RUN_TEST(test_set_is_dropped_when_take_fails);
    RUN_TEST(test_get_leaves_out_untouched_on_take_fail);
    RUN_TEST(test_take_give_balance);
    RUN_TEST(test_failed_take_does_not_give);
}
