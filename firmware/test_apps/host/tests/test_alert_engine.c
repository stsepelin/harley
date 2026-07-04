// Behavioural tests for the poi_alert state machine. Feeds a
// sequence of gps_source samples through it and asserts the
// active-alert transitions: idle → present (when the rider enters
// the alert cone) → dismissed (after passing the POI by enough
// metres to cross the hysteresis band).

#include "unity.h"
#include "poi_alert.h"
#include "poi_db.h"
#include "gps_source.h"

#define TALLINN_LAT  594370000
#define TALLINN_LON  247536000

// One omnidirectional camera 200 m north of Tallinn. Tests below
// approach it from the south heading north, then continue past.
static const poi_record_t SINGLE_CAM[] = {
    { TALLINN_LAT + 18000, TALLINN_LON, POI_KIND_SPEED, 50, 0xFFFF },
};

static poi_db_t open_single_cam_db(void)
{
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)SINGLE_CAM, sizeof(SINGLE_CAM));
    return db;
}

static gps_source_t make_fix(int32_t lat, int32_t lon, uint16_t heading)
{
    gps_source_t g = {
        .lat_e7      = lat,
        .lon_e7      = lon,
        .speed_mph   = 50,
        .heading_deg = heading,
        .fix_ok      = true,
        .time_ms     = 0,
    };
    return g;
}

static void test_no_alert_without_fix(void)
{
    poi_db_t db = open_single_cam_db();
    poi_alert_init(&db);

    gps_source_t g = make_fix(TALLINN_LAT, TALLINN_LON, /*heading=*/0);
    g.fix_ok = false;
    poi_alert_tick(&g);

    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_FALSE(a.active);
}

static void test_no_alert_outside_radius(void)
{
    poi_db_t db = open_single_cam_db();
    poi_alert_init(&db);

    // 10 km south of the camera → way outside the 500 m radius.
    gps_source_t g = make_fix(TALLINN_LAT - 900000, TALLINN_LON, 0);
    poi_alert_tick(&g);

    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_FALSE(a.active);
}

static void test_no_alert_when_camera_behind_rider(void)
{
    poi_db_t db = open_single_cam_db();
    poi_alert_init(&db);

    // Camera is north of rider; rider heading south — bearing-to-camera
    // is 0° but rider heading is 180°, so the bearing delta is 180°,
    // well outside the ±45° cone.
    gps_source_t g = make_fix(TALLINN_LAT, TALLINN_LON, /*heading=*/180);
    poi_alert_tick(&g);

    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_FALSE(a.active);
}

static void test_alert_fires_on_approach(void)
{
    poi_db_t db = open_single_cam_db();
    poi_alert_init(&db);

    // 100 m south of the camera, heading north. Camera is in cone, in
    // range — should be the active alert.
    gps_source_t g = make_fix(TALLINN_LAT + 9000, TALLINN_LON, /*heading=*/0);
    poi_alert_tick(&g);

    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_TRUE(a.active);
    TEST_ASSERT_EQUAL_PTR(&SINGLE_CAM[0], a.cam);
    // Distance should be ≈ 100 m.
    TEST_ASSERT_UINT32_WITHIN(20, 100, a.distance_m);
}

static void test_alert_dismisses_after_passing_with_hysteresis(void)
{
    poi_db_t db = open_single_cam_db();
    poi_alert_init(&db);

    // Approach (camera 100 m ahead, heading north) → alert active.
    gps_source_t approach = make_fix(TALLINN_LAT + 9000, TALLINN_LON, 0);
    poi_alert_tick(&approach);
    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_TRUE(a.active);

    // Now ~400 m past the camera (still heading north). Inside
    // radius but bearing-from-rider to camera is now behind us (180°),
    // outside the front cone. New queries miss → no replacement
    // alert, but the held alert sticks until we cross
    // radius + dismiss_m = 550 m past.
    gps_source_t passing = make_fix(TALLINN_LAT + 18000 + 36000, TALLINN_LON, 0);
    poi_alert_tick(&passing);
    poi_alert_get(&a);
    TEST_ASSERT_TRUE(a.active);

    // Now ~700 m past — beyond the hysteresis band.
    gps_source_t cleared = make_fix(TALLINN_LAT + 18000 + 63000, TALLINN_LON, 0);
    poi_alert_tick(&cleared);
    poi_alert_get(&a);
    TEST_ASSERT_FALSE(a.active);
    TEST_ASSERT_NULL(a.cam);
}

static void test_alert_switches_to_closer_camera_when_two_are_in_range(void)
{
    // Two cameras directly north of the rider: one at 100 m, one at
    // 300 m. Both inside the 500 m radius and inside the heading cone
    // — the engine should latch onto the closer one.
    static const poi_record_t two_cams[] = {
        { TALLINN_LAT + 27000, TALLINN_LON, POI_KIND_SPEED, 50, 0xFFFF },  // far  ~300m
        { TALLINN_LAT + 9000,  TALLINN_LON, POI_KIND_SPEED, 50, 0xFFFF },  // near ~100m
    };
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)two_cams, sizeof(two_cams));
    poi_alert_init(&db);

    gps_source_t g = make_fix(TALLINN_LAT, TALLINN_LON, 0);
    poi_alert_tick(&g);

    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_TRUE(a.active);
    TEST_ASSERT_EQUAL_PTR(&two_cams[1], a.cam);   // the closer record
}

static void test_tick_ignores_null_db_and_null_gps(void)
{
    // Before init (or init'd against NULL) a tick must be a no-op.
    poi_alert_init(NULL);
    gps_source_t g = make_fix(TALLINN_LAT, TALLINN_LON, 0);
    poi_alert_tick(&g);

    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_FALSE(a.active);

    // NULL gps sample with a live db: also a no-op, state untouched.
    poi_db_t db = open_single_cam_db();
    poi_alert_init(&db);
    poi_alert_tick(NULL);
    poi_alert_get(&a);
    TEST_ASSERT_FALSE(a.active);
}

static void test_get_tolerates_null_out(void)
{
    poi_alert_get(NULL);  // must not crash
}

static void test_active_alert_distance_updates_on_approach(void)
{
    poi_db_t db = open_single_cam_db();
    poi_alert_init(&db);

    // 300 m out → alert latches.
    gps_source_t far_fix = make_fix(TALLINN_LAT - 9000, TALLINN_LON, 0);
    poi_alert_tick(&far_fix);
    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_TRUE(a.active);
    uint32_t d_far = a.distance_m;

    // Same camera still the closest hit next tick — the alert must stay
    // latched on it and only the distance may change.
    gps_source_t near_fix = make_fix(TALLINN_LAT + 9000, TALLINN_LON, 0);
    poi_alert_tick(&near_fix);
    poi_alert_get(&a);
    TEST_ASSERT_TRUE(a.active);
    TEST_ASSERT_EQUAL_PTR(&SINGLE_CAM[0], a.cam);
    TEST_ASSERT_TRUE(a.distance_m < d_far);
    TEST_ASSERT_UINT32_WITHIN(20, 100, a.distance_m);
}

static void test_active_alert_replaced_by_closer_different_camera(void)
{
    // Two cameras north of the start point: A at ~200 m, B at ~400 m.
    static const poi_record_t gauntlet[] = {
        {TALLINN_LAT + 18000, TALLINN_LON, POI_KIND_SPEED, 50, 0xFFFF},  // A
        {TALLINN_LAT + 36000, TALLINN_LON, POI_KIND_SPEED, 50, 0xFFFF},  // B
    };
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)gauntlet, sizeof(gauntlet));
    poi_alert_init(&db);

    // From the start, A is the closest in-cone hit.
    gps_source_t at_start = make_fix(TALLINN_LAT, TALLINN_LON, 0);
    poi_alert_tick(&at_start);
    poi_alert_t a;
    poi_alert_get(&a);
    TEST_ASSERT_TRUE(a.active);
    TEST_ASSERT_EQUAL_PTR(&gauntlet[0], a.cam);

    // Rider now just past A (~250 m in): A is behind (out of cone) but
    // within the hysteresis hold; B is dead ahead and in-cone. The
    // active alert must switch to B while A's hold is still live.
    gps_source_t past_a = make_fix(TALLINN_LAT + 22500, TALLINN_LON, 0);
    poi_alert_tick(&past_a);
    poi_alert_get(&a);
    TEST_ASSERT_TRUE(a.active);
    TEST_ASSERT_EQUAL_PTR(&gauntlet[1], a.cam);
}

void RunTests(void)
{
    RUN_TEST(test_no_alert_without_fix);
    RUN_TEST(test_no_alert_outside_radius);
    RUN_TEST(test_no_alert_when_camera_behind_rider);
    RUN_TEST(test_alert_fires_on_approach);
    RUN_TEST(test_alert_dismisses_after_passing_with_hysteresis);
    RUN_TEST(test_alert_switches_to_closer_camera_when_two_are_in_range);
    RUN_TEST(test_tick_ignores_null_db_and_null_gps);
    RUN_TEST(test_get_tolerates_null_out);
    RUN_TEST(test_active_alert_distance_updates_on_approach);
    RUN_TEST(test_active_alert_replaced_by_closer_different_camera);
}
