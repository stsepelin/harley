// Geodesic math used by the POI alert engine. Targets the
// equirectangular distance + bearing + heading-delta in poi_math.
//
// All test points are picked so the answers are computable by hand
// (or with a basic spherical-trig calculator) — regressions in any of
// these functions surface as numeric off-by-N rather than "looks
// roughly right".

#include "unity.h"
#include "poi_math.h"

// Tallinn city centre — origin for most cases. Lat ≈ 59.437° N.
#define TALLINN_LAT  594370000
#define TALLINN_LON  247536000

static void test_distance_zero_for_identical_points(void)
{
    uint32_t d = poi_math_distance_m(TALLINN_LAT, TALLINN_LON,
                                        TALLINN_LAT, TALLINN_LON);
    TEST_ASSERT_EQUAL_UINT32(0, d);
}

static void test_distance_one_degree_latitude_is_111_km(void)
{
    // 1° of latitude is always ≈ 111.111 km regardless of where on the
    // globe you are. Allow a 100 m tolerance for the equirectangular
    // approximation at this scale.
    uint32_t d = poi_math_distance_m(TALLINN_LAT, TALLINN_LON,
                                        TALLINN_LAT + 10000000, TALLINN_LON);
    TEST_ASSERT_UINT32_WITHIN(100, 111111, d);
}

static void test_distance_one_degree_longitude_at_tallinn(void)
{
    // 1° of longitude at 59.437° N = 111.111 km × cos(59.437°)
    // ≈ 111.111 × 0.5083 = 56.477 km.
    uint32_t d = poi_math_distance_m(TALLINN_LAT, TALLINN_LON,
                                        TALLINN_LAT, TALLINN_LON + 10000000);
    TEST_ASSERT_UINT32_WITHIN(200, 56477, d);
}

static void test_distance_short_hop_under_one_metre(void)
{
    // 0.00001° ≈ 1.1 m of latitude — make sure we don't round to zero.
    uint32_t d = poi_math_distance_m(TALLINN_LAT, TALLINN_LON,
                                        TALLINN_LAT + 100, TALLINN_LON);
    TEST_ASSERT_UINT32_WITHIN(1, 1, d);
}

static void test_bearing_north_due_north(void)
{
    // Target is north (positive Δlat, no Δlon) → bearing 0°.
    uint16_t b = poi_math_bearing_deg(TALLINN_LAT, TALLINN_LON,
                                         TALLINN_LAT + 1000000, TALLINN_LON);
    TEST_ASSERT_UINT16_WITHIN(1, 0, b);
}

static void test_bearing_east_due_east(void)
{
    uint16_t b = poi_math_bearing_deg(TALLINN_LAT, TALLINN_LON,
                                         TALLINN_LAT, TALLINN_LON + 1000000);
    TEST_ASSERT_UINT16_WITHIN(1, 90, b);
}

static void test_bearing_south_due_south(void)
{
    uint16_t b = poi_math_bearing_deg(TALLINN_LAT, TALLINN_LON,
                                         TALLINN_LAT - 1000000, TALLINN_LON);
    TEST_ASSERT_UINT16_WITHIN(1, 180, b);
}

static void test_bearing_west_due_west(void)
{
    uint16_t b = poi_math_bearing_deg(TALLINN_LAT, TALLINN_LON,
                                         TALLINN_LAT, TALLINN_LON - 1000000);
    TEST_ASSERT_UINT16_WITHIN(1, 270, b);
}

static void test_bearing_returns_zero_for_coincident_points(void)
{
    // Documented contract: bearing(p, p) returns 0 rather than NaN'ing.
    uint16_t b = poi_math_bearing_deg(TALLINN_LAT, TALLINN_LON,
                                         TALLINN_LAT, TALLINN_LON);
    TEST_ASSERT_EQUAL_UINT16(0, b);
}

static void test_heading_delta_basic(void)
{
    TEST_ASSERT_EQUAL_INT16(  0, poi_math_heading_delta(  0,   0));
    TEST_ASSERT_EQUAL_INT16( 10, poi_math_heading_delta(  0,  10));
    TEST_ASSERT_EQUAL_INT16(-10, poi_math_heading_delta( 10,   0));
    TEST_ASSERT_EQUAL_INT16( 90, poi_math_heading_delta(  0,  90));
    TEST_ASSERT_EQUAL_INT16(-90, poi_math_heading_delta( 90,   0));
}

static void test_heading_delta_wraps_around_north(void)
{
    // Going from heading 10 to heading 350 is a 20° turn left, not
    // 340° right. The signed shortest-path convention matters for the
    // alert engine's "is the POI in my front cone" check.
    TEST_ASSERT_EQUAL_INT16(-20, poi_math_heading_delta( 10, 350));
    TEST_ASSERT_EQUAL_INT16( 20, poi_math_heading_delta(350,  10));
    // Exactly antipodal → +180 by convention (not -180).
    TEST_ASSERT_EQUAL_INT16(180, poi_math_heading_delta(  0, 180));
    TEST_ASSERT_EQUAL_INT16(180, poi_math_heading_delta(180, 360));   /* 360 alias of 0 */
}

void RunTests(void)
{
    RUN_TEST(test_distance_zero_for_identical_points);
    RUN_TEST(test_distance_one_degree_latitude_is_111_km);
    RUN_TEST(test_distance_one_degree_longitude_at_tallinn);
    RUN_TEST(test_distance_short_hop_under_one_metre);
    RUN_TEST(test_bearing_north_due_north);
    RUN_TEST(test_bearing_east_due_east);
    RUN_TEST(test_bearing_south_due_south);
    RUN_TEST(test_bearing_west_due_west);
    RUN_TEST(test_bearing_returns_zero_for_coincident_points);
    RUN_TEST(test_heading_delta_basic);
    RUN_TEST(test_heading_delta_wraps_around_north);
}
