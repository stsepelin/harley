#include "unity.h"
#include "format.h"
#include <string.h>

// --- format_km_grouped --------------------------------------------------

static void test_grouped_zero(void)
{
    char buf[20];
    format_km_grouped(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0", buf);
}

static void test_grouped_under_thousand(void)
{
    char buf[20];
    format_km_grouped(123, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("123", buf);
}

static void test_grouped_exactly_thousand(void)
{
    char buf[20];
    format_km_grouped(1000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1,000", buf);
}

static void test_grouped_five_digits(void)
{
    char buf[20];
    format_km_grouped(12847, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("12,847", buf);
}

static void test_grouped_seven_digits(void)
{
    char buf[20];
    format_km_grouped(1234567, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1,234,567", buf);
}

// Tight buffer: the formatter must never overrun. We allow truncation here,
// but the result must be NUL-terminated and fit.
static void test_grouped_truncates_safely(void)
{
    char buf[4];
    format_km_grouped(12847, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[sizeof(buf) - 1]);
    TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

// Tighter buffer: the comma can't fit even though the digit before it
// could. Exercises the inner "gi + 1 < out_size" guard on the comma write.
static void test_grouped_skips_comma_when_no_room(void)
{
    char buf[3];
    format_km_grouped(12847, buf, sizeof(buf));
    // "12" fills the buffer; no room for "12," before NUL.
    TEST_ASSERT_EQUAL_STRING("12", buf);
}

void RunTests(void)
{
    RUN_TEST(test_grouped_zero);
    RUN_TEST(test_grouped_under_thousand);
    RUN_TEST(test_grouped_exactly_thousand);
    RUN_TEST(test_grouped_five_digits);
    RUN_TEST(test_grouped_seven_digits);
    RUN_TEST(test_grouped_truncates_safely);
    RUN_TEST(test_grouped_skips_comma_when_no_room);
}
