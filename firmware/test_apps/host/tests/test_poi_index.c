// Coverage for poi_db's binary-format wrapping + query + sort. Builds
// a small synthetic DB whose POI positions are known by inspection,
// then exercises the bearing/heading filters and the closest-first
// ordering of the hits array.

#include "unity.h"
#include "poi_db.h"
#include <string.h>

#define TALLINN_LAT  594370000
#define TALLINN_LON  247536000

// Cameras placed at four cardinal compass points relative to Tallinn,
// each at exactly ~1 km from the centre. Cardinal positions mean the
// bearing-from-centre is unambiguous (0 / 90 / 180 / 270).
//
// One degree of latitude ≈ 111 km, so 1 km = ~0.009° = 90000 in
// 1e-7 units. Longitude at this latitude is roughly half as wide
// (cos(59.4°) ≈ 0.508), so 1 km east-west needs ~177000 1e-7 units.
//
// kind / limit / heading don't matter much for the index tests; we
// just need stable values to verify the sort.
static const poi_record_t cardinal_cams[4] = {
    { TALLINN_LAT + 90000,   TALLINN_LON,            POI_KIND_SPEED, 50, 0xFFFF },  // north
    { TALLINN_LAT,           TALLINN_LON + 177000,   POI_KIND_SPEED, 50, 0xFFFF },  // east
    { TALLINN_LAT - 90000,   TALLINN_LON,            POI_KIND_SPEED, 50, 0xFFFF },  // south
    { TALLINN_LAT,           TALLINN_LON - 177000,   POI_KIND_SPEED, 50, 0xFFFF },  // west
};

static void test_open_rejects_misaligned_buffer(void)
{
    poi_db_t db;
    // sizeof(poi_record_t) = 12; 7 bytes isn't a multiple → reject.
    bool ok = poi_db_open(&db, (const uint8_t *)cardinal_cams, 7);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NULL(db.records);
    TEST_ASSERT_EQUAL_size_t(0, db.count);
}

static void test_open_accepts_aligned_buffer(void)
{
    poi_db_t db;
    bool ok = poi_db_open(&db, (const uint8_t *)cardinal_cams,
                             sizeof(cardinal_cams));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(4, db.count);
    TEST_ASSERT_EQUAL_PTR(cardinal_cams, db.records);
}

static void test_query_heading_north_finds_only_north_camera(void)
{
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)cardinal_cams, sizeof(cardinal_cams));

    poi_hit_t hits[4];
    size_t n = poi_db_query(&db, TALLINN_LAT, TALLINN_LON,
                               /*heading=*/0, /*radius=*/1500,
                               /*tol=*/45, hits, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_PTR(&cardinal_cams[0], hits[0].cam);
    TEST_ASSERT_UINT32_WITHIN(50, 1000, hits[0].distance_m);
}

static void test_query_radius_excludes_far_cameras(void)
{
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)cardinal_cams, sizeof(cardinal_cams));

    // Radius 500 m — every camera is ~1 km away.
    poi_hit_t hits[4];
    size_t n = poi_db_query(&db, TALLINN_LAT, TALLINN_LON,
                               0, 500, 45, hits, 4);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

static void test_query_sorted_by_distance_ascending(void)
{
    // Put one camera close (200 m north) and three far (1 km). With
    // heading north + a wide tolerance, only the near + the far-north
    // match. The near one should land first.
    poi_record_t cams[5] = {
        { TALLINN_LAT + 90000,  TALLINN_LON,         POI_KIND_SPEED, 50, 0xFFFF }, // far north 1000m
        { TALLINN_LAT + 18000,  TALLINN_LON,         POI_KIND_SPEED, 50, 0xFFFF }, // near north 200m
        { TALLINN_LAT - 90000,  TALLINN_LON,         POI_KIND_SPEED, 50, 0xFFFF }, // south (outside 45° cone)
        { TALLINN_LAT,          TALLINN_LON + 177000, POI_KIND_SPEED, 50, 0xFFFF }, // east (outside cone)
        { TALLINN_LAT,          TALLINN_LON - 177000, POI_KIND_SPEED, 50, 0xFFFF }, // west (outside cone)
    };
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)cams, sizeof(cams));

    poi_hit_t hits[5];
    size_t n = poi_db_query(&db, TALLINN_LAT, TALLINN_LON,
                               0, 1500, 45, hits, 5);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_PTR(&cams[1], hits[0].cam);          // near first
    TEST_ASSERT_EQUAL_PTR(&cams[0], hits[1].cam);          // far second
    TEST_ASSERT_TRUE(hits[0].distance_m < hits[1].distance_m);
}

static void test_query_respects_out_cap(void)
{
    // out_cap = 1 — only the closest matching hit should land.
    poi_record_t cams[2] = {
        { TALLINN_LAT + 90000,  TALLINN_LON, POI_KIND_SPEED, 50, 0xFFFF }, // far
        { TALLINN_LAT + 18000,  TALLINN_LON, POI_KIND_SPEED, 50, 0xFFFF }, // near
    };
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)cams, sizeof(cams));

    poi_hit_t hit;
    size_t n = poi_db_query(&db, TALLINN_LAT, TALLINN_LON,
                               0, 1500, 45, &hit, 1);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_PTR(&cams[1], hit.cam);
}

static void test_query_filters_by_camera_facing_direction(void)
{
    // One camera 200 m north of Tallinn, facing east — it catches
    // eastbound riders, not northbound ones. A rider at Tallinn
    // heading north has the camera ahead of them, but the camera
    // doesn't catch riders going this way → no hit.
    poi_record_t cam_facing_east = {
        TALLINN_LAT + 18000, TALLINN_LON, POI_KIND_SPEED, 50, /*heading=*/90
    };
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)&cam_facing_east, sizeof(cam_facing_east));

    poi_hit_t hit;
    size_t n = poi_db_query(&db, TALLINN_LAT, TALLINN_LON,
                               /*heading=*/0, 1500, 45, &hit, 1);
    TEST_ASSERT_EQUAL_size_t(0, n);

    // Now place the rider 200 m WEST of an east-facing camera and have
    // them heading east. Camera is ahead of rider AND the camera's
    // facing direction matches rider heading → hit expected.
    poi_record_t cam_east_of_rider = {
        TALLINN_LAT, TALLINN_LON + 35000, POI_KIND_SPEED, 50, /*heading=*/90
    };
    poi_db_open(&db, (const uint8_t *)&cam_east_of_rider, sizeof(cam_east_of_rider));
    n = poi_db_query(&db, TALLINN_LAT, TALLINN_LON,
                        /*heading=*/90, 1500, 45, &hit, 1);
    TEST_ASSERT_EQUAL_size_t(1, n);
}

static void test_query_omnidirectional_camera_matches_any_heading(void)
{
    // heading_deg == 0xFFFF means "catches both directions" (red-light
    // boxes, fixed enforcement points).
    poi_record_t cam = {
        TALLINN_LAT + 18000, TALLINN_LON, POI_KIND_RED_LIGHT, 0, 0xFFFF
    };
    poi_db_t db;
    poi_db_open(&db, (const uint8_t *)&cam, sizeof(cam));

    poi_hit_t hit;
    // Rider heading north hits the camera (camera is in front).
    size_t n = poi_db_query(&db, TALLINN_LAT, TALLINN_LON,
                               0, 1500, 45, &hit, 1);
    TEST_ASSERT_EQUAL_size_t(1, n);
}

void RunTests(void)
{
    RUN_TEST(test_open_rejects_misaligned_buffer);
    RUN_TEST(test_open_accepts_aligned_buffer);
    RUN_TEST(test_query_heading_north_finds_only_north_camera);
    RUN_TEST(test_query_radius_excludes_far_cameras);
    RUN_TEST(test_query_sorted_by_distance_ascending);
    RUN_TEST(test_query_respects_out_cap);
    RUN_TEST(test_query_filters_by_camera_facing_direction);
    RUN_TEST(test_query_omnidirectional_camera_matches_any_heading);
}
