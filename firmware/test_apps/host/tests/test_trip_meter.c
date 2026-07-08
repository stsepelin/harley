// trip_meter: rolling 16-bit bus counter -> per-frame delta, wrap-safe.

#include "trip_meter.h"
#include "unity.h"

#define MAXJ 20000u

static void test_first_reading_seeds_and_returns_zero(void)
{
    trip_meter_t m = {0};
    TEST_ASSERT_EQUAL_UINT16(0, trip_meter_delta(&m, 500, MAXJ));
}

static void test_normal_delta(void)
{
    trip_meter_t m = {0};
    trip_meter_delta(&m, 500, MAXJ);  // seed
    TEST_ASSERT_EQUAL_UINT16(100, trip_meter_delta(&m, 600, MAXJ));
    TEST_ASSERT_EQUAL_UINT16(50, trip_meter_delta(&m, 650, MAXJ));
}

static void test_wraparound_is_a_small_delta(void)
{
    trip_meter_t m = {0};
    trip_meter_delta(&m, 65500, MAXJ);  // seed near the top of the range
    // 65500 -> 100 wraps: (100 - 65500) mod 65536 = 136.
    TEST_ASSERT_EQUAL_UINT16(136, trip_meter_delta(&m, 100, MAXJ));
}

static void test_reset_or_gap_is_discarded_then_resumes(void)
{
    trip_meter_t m = {0};
    trip_meter_delta(&m, 8000, MAXJ);  // seed
    // Counter reset to 0: (0 - 8000) mod 65536 = 57536 > MAXJ -> discarded.
    TEST_ASSERT_EQUAL_UINT16(0, trip_meter_delta(&m, 0, MAXJ));
    // It reseeded to 0, so normal counting resumes from there.
    TEST_ASSERT_EQUAL_UINT16(20, trip_meter_delta(&m, 20, MAXJ));
}

void RunTests(void)
{
    RUN_TEST(test_first_reading_seeds_and_returns_zero);
    RUN_TEST(test_normal_delta);
    RUN_TEST(test_wraparound_is_a_small_delta);
    RUN_TEST(test_reset_or_gap_is_discarded_then_resumes);
}
