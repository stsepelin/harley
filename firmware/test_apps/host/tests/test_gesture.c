#include "unity.h"
#include "gesture.h"

// Helpers: drive the FSM through a sequence of (pressed, x, y, tick)
// ticks. The FSM is pure, so we can sweep through any synthetic input.

static gesture_event_t tick(gesture_state_t *g, bool pressed, int x, int y, uint32_t t)
{
    return gesture_update(g, pressed, x, y, t);
}

// --- defaults --------------------------------------------------------------

static void test_init_sets_defaults_and_idle(void)
{
    gesture_state_t g;
    gesture_init(&g);
    TEST_ASSERT_EQUAL_UINT32(600, g.long_press_ms);
    TEST_ASSERT_EQUAL_INT(60, g.swipe_dist_min);
    TEST_ASSERT_EQUAL_INT(60, g.swipe_perp_max);
    TEST_ASSERT_FALSE(g.pressing);
    TEST_ASSERT_FALSE(g.long_fired);
}

// --- long-press ------------------------------------------------------------

static void test_long_press_fires_after_threshold(void)
{
    gesture_state_t g; gesture_init(&g);
    // Hold at the same point. No event until threshold; then GESTURE_LONG_PRESS.
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE,       tick(&g, true, 100, 100, 0));
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE,       tick(&g, true, 100, 100, 200));
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE,       tick(&g, true, 100, 100, 500));
    TEST_ASSERT_EQUAL_INT(GESTURE_LONG_PRESS, tick(&g, true, 100, 100, 600));
    // Subsequent ticks within the same press must not refire.
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE,       tick(&g, true, 100, 100, 800));
}

static void test_long_press_suppresses_swipe_on_release(void)
{
    gesture_state_t g; gesture_init(&g);
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE,       tick(&g, true,  100, 100, 0));
    TEST_ASSERT_EQUAL_INT(GESTURE_LONG_PRESS, tick(&g, true,  100, 100, 700));
    // Release after a long-press shouldn't classify as a swipe even if
    // the finger drifted slightly on the way up.
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE,       tick(&g, false, 100, 120, 750));
}

static void test_movement_blocks_long_press(void)
{
    gesture_state_t g; gesture_init(&g);
    // Move 80 px before the long-press deadline — no long-press should fire.
    tick(&g, true, 100, 100, 0);
    tick(&g, true, 180, 100, 200);   // 80 px > swipe_dist_min (60)
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE, tick(&g, true, 180, 100, 700));
    // Release classifies as a horizontal swipe.
    TEST_ASSERT_EQUAL_INT(GESTURE_SWIPE_RIGHT, tick(&g, false, 180, 100, 750));
}

// --- swipe classification --------------------------------------------------

static void test_swipe_right(void)
{
    gesture_state_t g; gesture_init(&g);
    tick(&g, true,  100, 100, 0);
    tick(&g, true,  200, 100, 50);   // dx=+100, dy=0
    TEST_ASSERT_EQUAL_INT(GESTURE_SWIPE_RIGHT, tick(&g, false, 200, 100, 100));
}

static void test_swipe_left(void)
{
    gesture_state_t g; gesture_init(&g);
    tick(&g, true,  200, 100, 0);
    tick(&g, true,  100, 100, 50);
    TEST_ASSERT_EQUAL_INT(GESTURE_SWIPE_LEFT, tick(&g, false, 100, 100, 100));
}

static void test_swipe_up(void)
{
    gesture_state_t g; gesture_init(&g);
    tick(&g, true,  100, 200, 0);
    tick(&g, true,  100, 100, 50);   // dy=-100
    TEST_ASSERT_EQUAL_INT(GESTURE_SWIPE_UP, tick(&g, false, 100, 100, 100));
}

static void test_swipe_down(void)
{
    gesture_state_t g; gesture_init(&g);
    tick(&g, true,  100, 100, 0);
    tick(&g, true,  100, 200, 50);   // dy=+100
    TEST_ASSERT_EQUAL_INT(GESTURE_SWIPE_DOWN, tick(&g, false, 100, 200, 100));
}

// --- swipe rejection -------------------------------------------------------

static void test_too_short_motion_is_a_tap(void)
{
    gesture_state_t g; gesture_init(&g);
    tick(&g, true,  100, 100, 0);
    tick(&g, true, 120, 100, 50);  // dx=20 < 60 -> not a swipe, stayed put
    TEST_ASSERT_EQUAL_INT(GESTURE_TAP, tick(&g, false, 120, 100, 100));
}

static void test_quick_press_release_is_a_tap(void)
{
    gesture_state_t g;
    gesture_init(&g);
    tick(&g, true, 200, 300, 0);
    TEST_ASSERT_EQUAL_INT(GESTURE_TAP, tick(&g, false, 200, 300, 120));  // no move, < long-press
}

static void test_diagonal_motion_rejected_when_perp_too_large(void)
{
    gesture_state_t g; gesture_init(&g);
    // dx=100, dy=100 — neither axis is "clean" (perp >= 60), reject.
    tick(&g, true,  100, 100, 0);
    tick(&g, true,  200, 200, 50);
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE, tick(&g, false, 200, 200, 100));
}

// Small dx but large dy, with a tightened perp so it's not a clean swipe:
// the drift exceeds the tap distance on one axis, so it's neither swipe nor
// tap -> NONE. (Guards the tap's per-axis distance check.)
static void test_large_drift_one_axis_is_not_a_tap(void)
{
    gesture_state_t g;
    gesture_init(&g);
    g.swipe_perp_max = 20;  // tighter than swipe_dist_min (60)
    tick(&g, true, 100, 100, 0);
    tick(&g, true, 130, 250, 50);  // dx=30 (>perp, <dist), dy=150 (>=dist)
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE, tick(&g, false, 130, 250, 100));
}

static void test_slight_drift_during_swipe_is_tolerated(void)
{
    gesture_state_t g; gesture_init(&g);
    // dx=100, dy=30 — perp under threshold, still a right swipe.
    tick(&g, true,  100, 100, 0);
    tick(&g, true,  200, 130, 50);
    TEST_ASSERT_EQUAL_INT(GESTURE_SWIPE_RIGHT, tick(&g, false, 200, 130, 100));
}

// --- state hygiene ---------------------------------------------------------

static void test_release_without_prior_press_emits_nothing(void)
{
    gesture_state_t g; gesture_init(&g);
    // First ever call is a release (cold start, finger not on the panel).
    TEST_ASSERT_EQUAL_INT(GESTURE_NONE, tick(&g, false, 100, 100, 0));
}

static void test_consecutive_gestures_are_independent(void)
{
    gesture_state_t g; gesture_init(&g);
    // First press: a right swipe.
    tick(&g, true,  100, 100, 0);
    tick(&g, true,  200, 100, 50);
    TEST_ASSERT_EQUAL_INT(GESTURE_SWIPE_RIGHT, tick(&g, false, 200, 100, 100));
    // Now a long-press at a fresh location.
    tick(&g, true,  400, 400, 200);
    TEST_ASSERT_EQUAL_INT(GESTURE_LONG_PRESS, tick(&g, true, 400, 400, 900));
}

// Pointer reads (x,y) while !pressed are noise; the FSM must not start a
// gesture from them. Validates that pressing has to be true to arm.
static void test_idle_pointer_motion_ignored(void)
{
    gesture_state_t g; gesture_init(&g);
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(GESTURE_NONE, tick(&g, false, 100 + i * 30, 100, i * 10));
    }
    // Confirm the next press starts fresh — long-press fires after ~600 ms.
    tick(&g, true, 100, 100, 100);
    TEST_ASSERT_EQUAL_INT(GESTURE_LONG_PRESS, tick(&g, true, 100, 100, 800));
}

// Tunables: custom thresholds are honoured. Useful for callers that want
// a hair-trigger swipe or a slower long-press.
static void test_custom_thresholds_honoured(void)
{
    gesture_state_t g; gesture_init(&g);
    g.long_press_ms  = 200;
    g.swipe_dist_min = 10;

    // 200 ms long-press now fires.
    tick(&g, true, 100, 100, 0);
    TEST_ASSERT_EQUAL_INT(GESTURE_LONG_PRESS, tick(&g, true, 100, 100, 200));

    // Reset by releasing, then a 15-px swipe should classify.
    tick(&g, false, 100, 100, 250);
    tick(&g, true,  100, 100, 300);
    tick(&g, true,  115, 100, 305);
    TEST_ASSERT_EQUAL_INT(GESTURE_SWIPE_RIGHT, tick(&g, false, 115, 100, 310));
}

void RunTests(void)
{
    RUN_TEST(test_init_sets_defaults_and_idle);
    RUN_TEST(test_long_press_fires_after_threshold);
    RUN_TEST(test_long_press_suppresses_swipe_on_release);
    RUN_TEST(test_movement_blocks_long_press);
    RUN_TEST(test_swipe_right);
    RUN_TEST(test_swipe_left);
    RUN_TEST(test_swipe_up);
    RUN_TEST(test_swipe_down);
    RUN_TEST(test_too_short_motion_is_a_tap);
    RUN_TEST(test_quick_press_release_is_a_tap);
    RUN_TEST(test_diagonal_motion_rejected_when_perp_too_large);
    RUN_TEST(test_large_drift_one_axis_is_not_a_tap);
    RUN_TEST(test_slight_drift_during_swipe_is_tolerated);
    RUN_TEST(test_release_without_prior_press_emits_nothing);
    RUN_TEST(test_consecutive_gestures_are_independent);
    RUN_TEST(test_idle_pointer_motion_ignored);
    RUN_TEST(test_custom_thresholds_honoured);
}
