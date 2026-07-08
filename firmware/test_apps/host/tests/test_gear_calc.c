// gear_calc: infer gear from the RPM:speed ratio (no gear sensor on the
// VRSCF). Expected rpm/mph per gear ~ 1st 144 / 2nd 97 / 3rd 78 / 4th 67 /
// 5th 60; a gear at speed S is exercised with rpm = round(ratio * S).

#include "gear_calc.h"
#include "unity.h"

// rpm/mph centre of each gear's band (x1, not x100)
static const int CENTER[] = {0, 144, 97, 78, 67, 60};  // index by gear 1..5

static void test_each_gear_at_center(void)
{
    for (gear_t g = GEAR_1; g <= GEAR_5; g++) {
        uint16_t mph = 40;
        uint16_t rpm = (uint16_t)(CENTER[g] * mph);
        TEST_ASSERT_EQUAL(g, gear_calc(rpm, mph, GEAR_UNKNOWN));
    }
}

static void test_stopped_or_creeping_is_unknown(void)
{
    TEST_ASSERT_EQUAL(GEAR_UNKNOWN, gear_calc(0, 0, GEAR_UNKNOWN));  // parked
    TEST_ASSERT_EQUAL(GEAR_UNKNOWN, gear_calc(3000, 2, GEAR_3));     // < min mph
    TEST_ASSERT_EQUAL(GEAR_UNKNOWN, gear_calc(400, 30, GEAR_2));     // < min rpm
}

static void test_out_of_band_is_unknown(void)
{
    // Clutch slipping: high rpm, low speed -> ratio way above 1st.
    TEST_ASSERT_EQUAL(GEAR_UNKNOWN, gear_calc(9000, 20, GEAR_UNKNOWN));  // 450 rpm/mph
    // Impossibly tall: ratio below 5th.
    TEST_ASSERT_EQUAL(GEAR_UNKNOWN, gear_calc(2000, 50, GEAR_UNKNOWN));  // 40 rpm/mph
}

static void test_ride1_dominant_clusters(void)
{
    // From ride 1: the two cruised gears sit at ~152 and ~99 rpm/mph.
    TEST_ASSERT_EQUAL(GEAR_1, gear_calc(152 * 25, 25, GEAR_UNKNOWN));
    TEST_ASSERT_EQUAL(GEAR_2, gear_calc(99 * 35, 35, GEAR_UNKNOWN));
}

static void test_hysteresis_holds_across_boundary(void)
{
    // Ratio sitting right on the 4th/5th boundary (~63 rpm/mph).
    uint16_t mph = 50, rpm = (uint16_t)(63 * mph);
    // Coming from 4th, it should stick in 4th; from 5th, stick in 5th.
    TEST_ASSERT_EQUAL(GEAR_4, gear_calc(rpm, mph, GEAR_4));
    TEST_ASSERT_EQUAL(GEAR_5, gear_calc(rpm, mph, GEAR_5));
    // Far from any boundary, the previous gear does not override a clear band
    // (prev above the ratio, and prev below it - both hysteresis exit paths).
    TEST_ASSERT_EQUAL(GEAR_2, gear_calc(97 * 40, 40, GEAR_5));
    TEST_ASSERT_EQUAL(GEAR_4, gear_calc(67 * 40, 40, GEAR_1));
}

void RunTests(void)
{
    RUN_TEST(test_each_gear_at_center);
    RUN_TEST(test_stopped_or_creeping_is_unknown);
    RUN_TEST(test_out_of_band_is_unknown);
    RUN_TEST(test_ride1_dominant_clusters);
    RUN_TEST(test_hysteresis_holds_across_boundary);
}
