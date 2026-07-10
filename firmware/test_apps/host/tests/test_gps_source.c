#include "unity.h"
#include "gps_source.h"
#include "freertos_stub.h"
#include <string.h>

static gps_source_t sample(void)
{
    gps_source_t g = {
        .lat_e7      = 594370000,
        .lon_e7      = 247536000,
        .speed_mph   = 42,
        .heading_deg = 135,
        .fix_ok      = true,
        .time_ms     = 12345,
    };
    return g;
}

static void test_roundtrip(void)
{
    freertos_stub_reset();
    gps_source_init();
    gps_source_t in = sample();
    gps_source_set(&in);

    gps_source_t out;
    memset(&out, 0, sizeof(out));
    gps_source_get(&out);
    TEST_ASSERT_EQUAL_INT32(in.lat_e7, out.lat_e7);
    TEST_ASSERT_EQUAL_INT32(in.lon_e7, out.lon_e7);
    TEST_ASSERT_EQUAL_UINT16(in.speed_mph, out.speed_mph);
    TEST_ASSERT_EQUAL_UINT16(in.heading_deg, out.heading_deg);
    TEST_ASSERT_TRUE(out.fix_ok);
}

// NULL in either direction is a no-op (guards), and never mutates the store.
static void test_null_args_are_noops(void)
{
    freertos_stub_reset();
    gps_source_init();
    gps_source_set(NULL);
    gps_source_get(NULL);

    gps_source_t out;
    memset(&out, 0xEE, sizeof(out));
    gps_source_get(&out);
    TEST_ASSERT_FALSE(out.fix_ok);  // still the zeroed init state
}

// A set that loses the mutex must leave the stored fix untouched.
static void test_set_dropped_on_take_fail(void)
{
    freertos_stub_reset();
    gps_source_init();
    gps_source_t good = sample();
    gps_source_set(&good);

    g_stub_take_succeeds = 0;
    gps_source_t doomed  = sample();
    doomed.lat_e7        = 999;
    gps_source_set(&doomed);

    g_stub_take_succeeds = 1;
    gps_source_t out;
    gps_source_get(&out);
    TEST_ASSERT_EQUAL_INT32(good.lat_e7, out.lat_e7);
}

// A get that loses the mutex must leave the caller's buffer untouched.
static void test_get_untouched_on_take_fail(void)
{
    freertos_stub_reset();
    gps_source_init();
    gps_source_t in = sample();
    gps_source_set(&in);

    g_stub_take_succeeds = 0;
    gps_source_t out;
    memset(&out, 0xEE, sizeof(out));
    gps_source_get(&out);

    uint8_t expected[sizeof(out)];
    memset(expected, 0xEE, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(expected, &out, sizeof(out));
}

void RunTests(void)
{
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_null_args_are_noops);
    RUN_TEST(test_set_dropped_on_take_fail);
    RUN_TEST(test_get_untouched_on_take_fail);
}
