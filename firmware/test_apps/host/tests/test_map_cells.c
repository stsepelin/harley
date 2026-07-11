// Pure cell-paging logic: which lat/lon cell a position is in, the working-set
// window, and the heading-ahead prefetch cell. No LVGL/SD, so it's in the 100%
// branch gate.

#include "unity.h"
#include "map_cells.h"

#define DEG256 256  // 1-degree cells (cell_size in 1/256 deg)

static void test_cell_of_positive(void)
{
    // Tallinn 24.745 E / 59.437 N -> cell (24, 59) at 1 deg.
    map_cell_t c = map_cell_of(247450000, 594370000, DEG256);
    TEST_ASSERT_EQUAL_INT32(59, c.lat);
    TEST_ASSERT_EQUAL_INT32(24, c.lon);
}

static void test_cell_of_exact_boundary(void)
{
    // Exactly 59.0 N / 24.0 E -> the cell whose SW corner it is (no floor down).
    map_cell_t c = map_cell_of(240000000, 590000000, DEG256);
    TEST_ASSERT_EQUAL_INT32(59, c.lat);
    TEST_ASSERT_EQUAL_INT32(24, c.lon);
}

static void test_cell_of_negative_floors_down(void)
{
    // 33.9 S / 0.5 W -> floor to (-34, -1), not truncate to (-33, 0).
    map_cell_t c = map_cell_of(-5000000, -339000000, DEG256);
    TEST_ASSERT_EQUAL_INT32(-34, c.lat);
    TEST_ASSERT_EQUAL_INT32(-1, c.lon);
}

static void test_cell_of_negative_exact(void)
{
    // Exactly -34.0 N -> -34 (a % b == 0 with a < 0: no extra floor).
    map_cell_t c = map_cell_of(0, -340000000, DEG256);
    TEST_ASSERT_EQUAL_INT32(-34, c.lat);
    TEST_ASSERT_EQUAL_INT32(0, c.lon);
}

static void test_cell_of_half_degree(void)
{
    // 0.5 deg cells (cell_size 128): 59.437 -> floor(118.87) = 118.
    map_cell_t c = map_cell_of(247450000, 594370000, 128);
    TEST_ASSERT_EQUAL_INT32(118, c.lat);
    TEST_ASSERT_EQUAL_INT32(49, c.lon);  // 24.745 / 0.5 = 49.49 -> 49
}

static void test_cell_eq(void)
{
    map_cell_t a = {59, 24};
    TEST_ASSERT_TRUE(map_cell_eq(a, (map_cell_t){59, 24}));
    TEST_ASSERT_FALSE(map_cell_eq(a, (map_cell_t){60, 24}));  // lat differs
    TEST_ASSERT_FALSE(map_cell_eq(a, (map_cell_t){59, 25}));  // lon differs
}

static void test_in_window(void)
{
    map_cell_t ctr = {59, 24};
    TEST_ASSERT_FALSE(map_cell_in_window((map_cell_t){59, 24}, ctr, -1));  // bad radius
    TEST_ASSERT_TRUE(map_cell_in_window((map_cell_t){59, 24}, ctr, 1));    // centre
    TEST_ASSERT_TRUE(map_cell_in_window((map_cell_t){58, 23}, ctr, 1));    // neg corner (abs)
    TEST_ASSERT_TRUE(map_cell_in_window((map_cell_t){60, 25}, ctr, 1));    // pos corner
    TEST_ASSERT_FALSE(map_cell_in_window((map_cell_t){61, 24}, ctr, 1));   // lat out
    TEST_ASSERT_FALSE(map_cell_in_window((map_cell_t){59, 26}, ctr, 1));   // lon out
}

static void test_window(void)
{
    map_cell_t ctr = {59, 24};
    map_cell_t out[9];
    TEST_ASSERT_EQUAL_INT(0, map_cell_window(ctr, -1, out, 9));  // bad radius
    TEST_ASSERT_EQUAL_INT(0, map_cell_window(ctr, 1, out, 8));   // cap too small
    int n = map_cell_window(ctr, 1, out, 9);
    TEST_ASSERT_EQUAL_INT(9, n);
    // Every returned cell is inside the window, and the centre is present.
    bool has_ctr = false;
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(map_cell_in_window(out[i], ctr, 1));
        if (map_cell_eq(out[i], ctr))
            has_ctr = true;
    }
    TEST_ASSERT_TRUE(has_ctr);
    // radius 0 -> just the centre.
    TEST_ASSERT_EQUAL_INT(1, map_cell_window(ctr, 0, out, 9));
    TEST_ASSERT_TRUE(map_cell_eq(out[0], ctr));
}

static void test_ahead(void)
{
    map_cell_t ctr = {59, 24};
    TEST_ASSERT_TRUE(map_cell_eq(map_cell_ahead(ctr, -1.0), ctr));                    // unknown
    TEST_ASSERT_TRUE(map_cell_eq(map_cell_ahead(ctr, 0.0), (map_cell_t){60, 24}));    // N
    TEST_ASSERT_TRUE(map_cell_eq(map_cell_ahead(ctr, 90.0), (map_cell_t){59, 25}));   // E
    TEST_ASSERT_TRUE(map_cell_eq(map_cell_ahead(ctr, 180.0), (map_cell_t){58, 24}));  // S
    TEST_ASSERT_TRUE(map_cell_eq(map_cell_ahead(ctr, 270.0), (map_cell_t){59, 23}));  // W
    TEST_ASSERT_TRUE(map_cell_eq(map_cell_ahead(ctr, 45.0), (map_cell_t){60, 25}));   // NE
}

void RunTests(void)
{
    RUN_TEST(test_cell_of_positive);
    RUN_TEST(test_cell_of_exact_boundary);
    RUN_TEST(test_cell_of_negative_floors_down);
    RUN_TEST(test_cell_of_negative_exact);
    RUN_TEST(test_cell_of_half_degree);
    RUN_TEST(test_cell_eq);
    RUN_TEST(test_in_window);
    RUN_TEST(test_window);
    RUN_TEST(test_ahead);
}
