// Regression tests for the skip-if-unchanged caches added to every label-
// based widget. The contract under test is: if the *displayed* value is
// the same as last frame, the widget must make ZERO LVGL setter calls.
//
// Catches the failure mode where a future refactor silently removes a
// cache (no visual difference; the gauge just gets choppier under load).

#include "unity.h"
#include "lvgl_stub.h"
#include "vehicle_data.h"

#include "speed_display.h"
#include "gear_indicator.h"
#include "temp_display.h"
#include "turn_signals.h"
#include "fuel_bar.h"
#include "clock_display.h"
#include "odometer_display.h"
#include "trip_display.h"
#include "warning_lights.h"

#include <string.h>

// Number of repeated identical updates we run after priming the cache. If
// any setter fires inside this loop, the cache is broken.
#define REPEAT  100

// --- speed_display ---------------------------------------------------------

static void test_speed_cache_skips_unchanged(void)
{
    lv_obj_t *w = speed_display_create(NULL);
    speed_display_set_value(w, 50, UNITS_KPH);          // prime
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) speed_display_set_value(w, 50, UNITS_KPH);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_lv_label_set_text_calls,
        "identical speed must not re-render");
}

static void test_speed_cache_fires_on_change(void)
{
    lv_obj_t *w = speed_display_create(NULL);
    speed_display_set_value(w, 50, UNITS_KPH);
    lv_stub_reset();
    speed_display_set_value(w, 51, UNITS_KPH);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// Switching the displayed unit must repaint both the value digits and
// the "km/h" / "mph" subtitle even when the underlying km/h value is
// identical — same input, different output.
static void test_speed_cache_fires_on_units_change(void)
{
    lv_obj_t *w = speed_display_create(NULL);
    speed_display_set_value(w, 50, UNITS_KPH);
    lv_stub_reset();
    speed_display_set_value(w, 50, UNITS_MPH);
    TEST_ASSERT_EQUAL_INT(2, g_lv_label_set_text_calls);  // value + unit
}

// --- gear_indicator --------------------------------------------------------

static void test_gear_cache_skips_unchanged(void)
{
    lv_obj_t *w = gear_indicator_create(NULL);
    gear_indicator_set(w, GEAR_3);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) gear_indicator_set(w, GEAR_3);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_gear_cache_fires_on_change(void)
{
    lv_obj_t *w = gear_indicator_create(NULL);
    gear_indicator_set(w, GEAR_3);
    lv_stub_reset();
    gear_indicator_set(w, GEAR_4);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// --- temp_display ----------------------------------------------------------
// temp updates both the value label (snprintf + set_text) and recolours
// both icon + value (two text_color calls).

static void test_temp_cache_skips_unchanged(void)
{
    lv_obj_t *w = temp_display_create(NULL);
    temp_display_set_value(w, 92);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) temp_display_set_value(w, 92);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_temp_cache_fires_on_change(void)
{
    lv_obj_t *w = temp_display_create(NULL);
    temp_display_set_value(w, 92);
    lv_stub_reset();
    temp_display_set_value(w, 93);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(2, g_lv_obj_set_style_text_color_calls);  // value + icon
}

// --- turn_signals ----------------------------------------------------------
// Only recolours the side(s) that actually flipped.

static void test_turn_signals_cache_skips_unchanged(void)
{
    lv_obj_t *w = turn_signals_create(NULL);
    turn_signals_set(w, false, false);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) turn_signals_set(w, false, false);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_turn_signals_only_changed_side_repaints(void)
{
    lv_obj_t *w = turn_signals_create(NULL);
    turn_signals_set(w, false, false);
    lv_stub_reset();
    turn_signals_set(w, true, false);   // only left flipped
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);
}

// --- fuel_bar --------------------------------------------------------------
// Per-level update redraws all FUEL_SEGMENTS (6) bg colours + 1 icon
// text colour. Skip should drop all 7.

static void test_fuel_cache_skips_unchanged(void)
{
    lv_obj_t *w = fuel_bar_create(NULL);
    fuel_bar_set_level(w, 4);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) fuel_bar_set_level(w, 4);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_bg_color_calls);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_fuel_cache_fires_on_change(void)
{
    lv_obj_t *w = fuel_bar_create(NULL);
    fuel_bar_set_level(w, 4);
    lv_stub_reset();
    fuel_bar_set_level(w, 3);
    TEST_ASSERT_EQUAL_INT(6, g_lv_obj_set_style_bg_color_calls);
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);
}

// --- clock_display ---------------------------------------------------------

static void test_clock_cache_skips_unchanged(void)
{
    lv_obj_t *w = clock_display_create(NULL);
    clock_display_set(w, 8, 24);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) clock_display_set(w, 8, 24);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_clock_cache_fires_on_minute_change(void)
{
    lv_obj_t *w = clock_display_create(NULL);
    clock_display_set(w, 8, 24);
    lv_stub_reset();
    clock_display_set(w, 8, 25);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// --- odometer_display ------------------------------------------------------
// Cache key is km (meters / 1000) — sub-km changes must not re-render.

static void test_odometer_cache_skips_sub_km(void)
{
    lv_obj_t *w = odometer_display_create(NULL);
    odometer_display_set(w, 12847000, UNITS_KPH);   // 12,847 km
    lv_stub_reset();
    // Same km value reached via several sub-km updates — must stay quiet.
    for (uint32_t m = 12847001; m < 12847000 + 1000; m += 7) {
        odometer_display_set(w, m, UNITS_KPH);
    }
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_odometer_cache_fires_on_km_change(void)
{
    lv_obj_t *w = odometer_display_create(NULL);
    odometer_display_set(w, 12847000, UNITS_KPH);
    lv_stub_reset();
    odometer_display_set(w, 12848000, UNITS_KPH);   // next km
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

static void test_odometer_cache_fires_on_units_change(void)
{
    lv_obj_t *w = odometer_display_create(NULL);
    odometer_display_set(w, 12847000, UNITS_KPH);
    lv_stub_reset();
    odometer_display_set(w, 12847000, UNITS_MPH);   // same metres, different unit
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// --- trip_display ----------------------------------------------------------
// Cache key is tenth-of-km. Changes within 100 m must stay quiet.

static void test_trip_cache_skips_sub_tenth_km(void)
{
    lv_obj_t *w = trip_display_create(NULL, "TRIP1");
    trip_display_set(w, 1234, UNITS_KPH);            // 1.2 km
    lv_stub_reset();
    for (uint32_t m = 1235; m < 1300; m++) trip_display_set(w, m, UNITS_KPH);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_trip_cache_fires_on_tenth_change(void)
{
    lv_obj_t *w = trip_display_create(NULL, "TRIP1");
    trip_display_set(w, 1234, UNITS_KPH);            // 1.2 km
    lv_stub_reset();
    trip_display_set(w, 1300, UNITS_KPH);            // 1.3 km
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

static void test_trip_cache_fires_on_units_change(void)
{
    lv_obj_t *w = trip_display_create(NULL, "TRIP1");
    trip_display_set(w, 1234, UNITS_KPH);
    lv_stub_reset();
    trip_display_set(w, 1234, UNITS_MPH);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// --- warning_lights --------------------------------------------------------
// Per-lamp icon+colour cache. Idle (all off) → no work. Flipping one lamp
// fires set_text + set_style_text_color exactly once (one lamp painted).

static lv_obj_t *make_warn_chevron(void)
{
    static const lamp_id_t ids[] = { LAMP_OIL, LAMP_ENGINE, LAMP_BATTERY };
    return warning_lights_create(NULL, ids, 3, WARN_LAYOUT_CHEVRON);
}

static void test_warning_lights_cache_idle(void)
{
    lv_obj_t *w = make_warn_chevron();
    vehicle_data_t d = {0};
    warning_lights_update(w, &d);        // prime
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) warning_lights_update(w, &d);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_warning_lights_only_changed_lamp_repaints(void)
{
    lv_obj_t *w = make_warn_chevron();
    vehicle_data_t d = {0};
    warning_lights_update(w, &d);
    lv_stub_reset();

    d.oil_pressure_warning = true;
    warning_lights_update(w, &d);
    // lamp_apply_appearance writes icon AND colour, but only for the
    // changed lamp.
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);
}

void RunTests(void)
{
    RUN_TEST(test_speed_cache_skips_unchanged);
    RUN_TEST(test_speed_cache_fires_on_change);
    RUN_TEST(test_speed_cache_fires_on_units_change);
    RUN_TEST(test_gear_cache_skips_unchanged);
    RUN_TEST(test_gear_cache_fires_on_change);
    RUN_TEST(test_temp_cache_skips_unchanged);
    RUN_TEST(test_temp_cache_fires_on_change);
    RUN_TEST(test_turn_signals_cache_skips_unchanged);
    RUN_TEST(test_turn_signals_only_changed_side_repaints);
    RUN_TEST(test_fuel_cache_skips_unchanged);
    RUN_TEST(test_fuel_cache_fires_on_change);
    RUN_TEST(test_clock_cache_skips_unchanged);
    RUN_TEST(test_clock_cache_fires_on_minute_change);
    RUN_TEST(test_odometer_cache_skips_sub_km);
    RUN_TEST(test_odometer_cache_fires_on_km_change);
    RUN_TEST(test_odometer_cache_fires_on_units_change);
    RUN_TEST(test_trip_cache_skips_sub_tenth_km);
    RUN_TEST(test_trip_cache_fires_on_tenth_change);
    RUN_TEST(test_trip_cache_fires_on_units_change);
    RUN_TEST(test_warning_lights_cache_idle);
    RUN_TEST(test_warning_lights_only_changed_lamp_repaints);
}
