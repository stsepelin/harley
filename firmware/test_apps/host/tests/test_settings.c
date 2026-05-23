#include "unity.h"
#include "settings.h"

static void test_defaults_known_values(void)
{
    settings_t s;
    settings_default(&s);
    TEST_ASSERT_EQUAL_UINT8(UNITS_KPH, s.units);
    TEST_ASSERT_EQUAL_UINT8(60,        s.brightness);
    TEST_ASSERT_TRUE       (           s.sound_enabled);
    TEST_ASSERT_EQUAL_UINT8(70,        s.volume);
    // Default is "invisible to strangers once bonded" — directed
    // advertising is the safer steady state. The override is opt-in.
    TEST_ASSERT_FALSE(s.ble_visible_override);
}

static void test_validate_passes_valid_values(void)
{
    settings_t s = { .units = UNITS_MPH, .brightness = 50, .sound_enabled = false, .volume = 25 };
    settings_validate(&s);
    TEST_ASSERT_EQUAL_UINT8(UNITS_MPH, s.units);
    TEST_ASSERT_EQUAL_UINT8(50,        s.brightness);
    TEST_ASSERT_FALSE      (           s.sound_enabled);
    TEST_ASSERT_EQUAL_UINT8(25,        s.volume);
}

static void test_validate_clamps_volume_over_max(void)
{
    settings_t s = { .units = UNITS_KPH, .brightness = 60, .volume = 200 };
    settings_validate(&s);
    TEST_ASSERT_EQUAL_UINT8(100, s.volume);
}

static void test_validate_clamps_brightness_over_max(void)
{
    settings_t s = { .units = UNITS_KPH, .brightness = 200 };
    settings_validate(&s);
    TEST_ASSERT_EQUAL_UINT8(100, s.brightness);
}

// Below the visible-PWM floor the LCD goes fully black, so we clamp up
// to SETTINGS_BRIGHTNESS_MIN rather than honour whatever the user (or a
// rogue NVS read) tried to set.
static void test_validate_clamps_brightness_below_min(void)
{
    settings_t s = { .units = UNITS_KPH, .brightness = 0 };
    settings_validate(&s);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BRIGHTNESS_MIN, s.brightness);

    s.brightness = SETTINGS_BRIGHTNESS_MIN - 1;
    settings_validate(&s);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BRIGHTNESS_MIN, s.brightness);
}

// Reading garbage from NVS (stale data, future-firmware values, etc.)
// must not produce out-of-range enums that propagate into the widget
// switch-cases.
static void test_validate_repairs_bad_units_enum(void)
{
    settings_t s = { .units = (display_units_t)7, .brightness = 50 };
    settings_validate(&s);
    TEST_ASSERT_EQUAL_UINT8(UNITS_KPH, s.units);
}

static void test_validate_is_idempotent(void)
{
    settings_t s;
    settings_default(&s);
    settings_validate(&s);
    settings_validate(&s);
    TEST_ASSERT_EQUAL_UINT8(UNITS_KPH, s.units);
    TEST_ASSERT_EQUAL_UINT8(60,        s.brightness);
}

void RunTests(void)
{
    RUN_TEST(test_defaults_known_values);
    RUN_TEST(test_validate_passes_valid_values);
    RUN_TEST(test_validate_clamps_brightness_over_max);
    RUN_TEST(test_validate_clamps_brightness_below_min);
    RUN_TEST(test_validate_clamps_volume_over_max);
    RUN_TEST(test_validate_repairs_bad_units_enum);
    RUN_TEST(test_validate_is_idempotent);
}
