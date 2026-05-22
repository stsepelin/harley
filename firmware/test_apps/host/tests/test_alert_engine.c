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
        .speed_kmh   = 50,
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

void RunTests(void)
{
    RUN_TEST(test_no_alert_without_fix);
    RUN_TEST(test_no_alert_outside_radius);
    RUN_TEST(test_no_alert_when_camera_behind_rider);
    RUN_TEST(test_alert_fires_on_approach);
    RUN_TEST(test_alert_dismisses_after_passing_with_hysteresis);
    RUN_TEST(test_alert_switches_to_closer_camera_when_two_are_in_range);
}
