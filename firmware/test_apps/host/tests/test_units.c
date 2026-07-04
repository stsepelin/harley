#include "unity.h"
#include "units.h"

// --- speed ---------------------------------------------------------------

// mph is canonical now, so the MPH display path is a passthrough.
static void test_speed_mph_passthrough(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, units_speed_display(0, UNITS_MPH));
    TEST_ASSERT_EQUAL_UINT16(50, units_speed_display(50, UNITS_MPH));
    TEST_ASSERT_EQUAL_UINT16(127, units_speed_display(127, UNITS_MPH));
}

// Hand-checked against the exact conversion factor 1.609344.
static void test_speed_kph_round_to_nearest(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, units_speed_display(0, UNITS_KPH));      // 0.00
    TEST_ASSERT_EQUAL_UINT16(80, units_speed_display(50, UNITS_KPH));    // 80.47
    TEST_ASSERT_EQUAL_UINT16(161, units_speed_display(100, UNITS_KPH));  // 160.93 → 161
    TEST_ASSERT_EQUAL_UINT16(193, units_speed_display(120, UNITS_KPH));  // 193.12
    TEST_ASSERT_EQUAL_UINT16(322, units_speed_display(200, UNITS_KPH));  // 321.87 → 322
}

// Rounds to nearest in both directions.
static void test_speed_kph_round_boundary(void)
{
    // 45 mph = 72.42 km/h → 72 (rounds down)
    TEST_ASSERT_EQUAL_UINT16(72, units_speed_display(45, UNITS_KPH));
    // 78 mph = 125.53 km/h → 126 (rounds up)
    TEST_ASSERT_EQUAL_UINT16(126, units_speed_display(78, UNITS_KPH));
}

// --- whole distance (odometer) -------------------------------------------

static void test_distance_whole_km_truncated(void)
{
    TEST_ASSERT_EQUAL_UINT32(0,     units_distance_whole(0,        UNITS_KPH));
    TEST_ASSERT_EQUAL_UINT32(0,     units_distance_whole(999,      UNITS_KPH));
    TEST_ASSERT_EQUAL_UINT32(1,     units_distance_whole(1000,     UNITS_KPH));
    TEST_ASSERT_EQUAL_UINT32(12847, units_distance_whole(12847000, UNITS_KPH));
}

static void test_distance_whole_mi_truncated(void)
{
    // 1 mile = 1609.344 m, so just-below and just-above are 1609 / 1610 m.
    TEST_ASSERT_EQUAL_UINT32(0,    units_distance_whole(0,        UNITS_MPH));
    TEST_ASSERT_EQUAL_UINT32(0,    units_distance_whole(1609,     UNITS_MPH)); // 0.9998 mi
    TEST_ASSERT_EQUAL_UINT32(1,    units_distance_whole(1610,     UNITS_MPH)); // 1.0004 mi
    TEST_ASSERT_EQUAL_UINT32(1000, units_distance_whole(1609344,  UNITS_MPH)); // exactly 1000 mi
    TEST_ASSERT_EQUAL_UINT32(7982, units_distance_whole(12847000, UNITS_MPH)); // 7982.76 mi
}

// --- tenths distance (trip counter) --------------------------------------

static void test_distance_tenths_km(void)
{
    TEST_ASSERT_EQUAL_UINT32(0,  units_distance_tenths(0,    UNITS_KPH));
    TEST_ASSERT_EQUAL_UINT32(0,  units_distance_tenths(99,   UNITS_KPH));
    TEST_ASSERT_EQUAL_UINT32(1,  units_distance_tenths(100,  UNITS_KPH));
    TEST_ASSERT_EQUAL_UINT32(12, units_distance_tenths(1234, UNITS_KPH));
}

static void test_distance_tenths_mi(void)
{
    TEST_ASSERT_EQUAL_UINT32(0,  units_distance_tenths(0,     UNITS_MPH));
    TEST_ASSERT_EQUAL_UINT32(0,  units_distance_tenths(160,   UNITS_MPH));  // 0.099 mi
    TEST_ASSERT_EQUAL_UINT32(1,  units_distance_tenths(161,   UNITS_MPH));  // 0.100 mi
    TEST_ASSERT_EQUAL_UINT32(7,  units_distance_tenths(1234,  UNITS_MPH));  // 0.767
    TEST_ASSERT_EQUAL_UINT32(76, units_distance_tenths(12340, UNITS_MPH));  // 7.667
}

// --- labels --------------------------------------------------------------

static void test_speed_labels(void)
{
    TEST_ASSERT_EQUAL_STRING("km/h", units_speed_label(UNITS_KPH));
    TEST_ASSERT_EQUAL_STRING("mph",  units_speed_label(UNITS_MPH));
}

static void test_distance_labels(void)
{
    TEST_ASSERT_EQUAL_STRING("km", units_distance_label(UNITS_KPH));
    TEST_ASSERT_EQUAL_STRING("mi", units_distance_label(UNITS_MPH));
}

void RunTests(void)
{
    RUN_TEST(test_speed_mph_passthrough);
    RUN_TEST(test_speed_kph_round_to_nearest);
    RUN_TEST(test_speed_kph_round_boundary);
    RUN_TEST(test_distance_whole_km_truncated);
    RUN_TEST(test_distance_whole_mi_truncated);
    RUN_TEST(test_distance_tenths_km);
    RUN_TEST(test_distance_tenths_mi);
    RUN_TEST(test_speed_labels);
    RUN_TEST(test_distance_labels);
}
