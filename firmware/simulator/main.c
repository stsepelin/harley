// Desktop V-Rod cluster simulator.
//
//   - Real production widget code (no mocks) drives the ride screen.
//   - The synthetic driving cycle from main/simulator/sim_engine.c runs on
//     a real pthread (via the FreeRTOS shim) and feeds vehicle_data.
//   - LVGL renders into an SDL2 window at native 800×800.
//
// Quit by closing the window or hitting Ctrl-C.

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/libs/lodepng/lodepng.h"

#include "vehicle_data.h"
#include "sim_engine.h"
#include "screen_ride.h"
#include "settings_store.h"
#include "ui_manager.h"
#include "gesture.h"
#include "phone_data.h"
#include "test_bridge.h"
#include "emoji_font.h"
#include "map_tile.h"
#include "screen_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#define DISPLAY_W  800
#define DISPLAY_H  800
#define UI_TICK_MS 15  // ~66 FPS, matches firmware's LV_DEF_REFR_PERIOD

static void inv_log_cb(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    fprintf(stderr, "[inv] %dx%d @ (%d,%d)\n", (int)(a->x2 - a->x1 + 1), (int)(a->y2 - a->y1 + 1),
            (int)a->x1, (int)a->y1);
}

// --- Perf harness (VROD_PERF=<frames>) ------------------------------------
// Times each render and sums the per-frame invalidated area, then prints a
// distribution and exits. The SDL backend's absolute render time is NOT the
// P4's, but the *dirty-rect area* and *relative* render cost map directly to
// the device's partial-refresh budget, which is what gates the 30 FPS target.
#define PERF_MAX_FRAMES 6000
#define PERF_WARMUP     30     // skip boot / screen-load transient
#define FRAME_BUDGET_US 33333  // 30 FPS

static uint64_t s_frame_inv_area = 0;
static int      s_frame_inv_cnt  = 0;
static uint32_t s_render_us[PERF_MAX_FRAMES];
static uint32_t s_area_px[PERF_MAX_FRAMES];
static int      s_cnt_px[PERF_MAX_FRAMES];

static void perf_inv_cb(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    s_frame_inv_area += (uint64_t)(a->x2 - a->x1 + 1) * (a->y2 - a->y1 + 1);
    s_frame_inv_cnt++;
}

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

// Returns target frame count if perf mode is on, else 0.
static int perf_target(void)
{
    const char *p = getenv("VROD_PERF");
    if (!p)
        return 0;
    int n = atoi(p);
    if (n <= 0)
        n = 600;
    if (n > PERF_MAX_FRAMES)
        n = PERF_MAX_FRAMES;
    return n;
}

static void perf_summary(int n)
{
    // Drop warmup frames, then report the steady-state distribution.
    int base = (n > PERF_WARMUP) ? PERF_WARMUP : 0;
    int m    = n - base;
    if (m <= 0)
        return;

    uint32_t *us     = malloc(sizeof(uint32_t) * m);
    uint64_t  sum_us = 0, sum_area = 0, sum_cnt = 0;
    uint32_t  max_area = 0;
    int       over     = 0;
    for (int i = 0; i < m; i++) {
        us[i] = s_render_us[base + i];
        sum_us += us[i];
        sum_area += s_area_px[base + i];
        sum_cnt += s_cnt_px[base + i];
        if (s_area_px[base + i] > max_area)
            max_area = s_area_px[base + i];
        if (us[i] > FRAME_BUDGET_US)
            over++;
    }
    qsort(us, m, sizeof(uint32_t), cmp_u32);
    uint32_t p50 = us[m / 2], p90 = us[(m * 90) / 100], p99 = us[(m * 99) / 100], mx = us[m - 1];
    double   mean_us = (double)sum_us / m;

    fprintf(stderr, "\n==== VROD perf: %d frames (warmup %d dropped) ====\n", m, base);
    fprintf(stderr, "render us   mean=%.0f  p50=%u  p90=%u  p99=%u  max=%u\n", mean_us, p50, p90,
            p99, mx);
    fprintf(stderr, "render-bound FPS  mean=%.1f  p99=%.1f  worst=%.1f\n", 1e6 / mean_us,
            1e6 / (p99 ? p99 : 1), 1e6 / (mx ? mx : 1));
    fprintf(stderr, "frames over 33.3ms budget: %d/%d (%.1f%%)\n", over, m, 100.0 * over / m);
    fprintf(stderr, "dirty px/frame  mean=%llu  max=%u   rects/frame mean=%.1f\n",
            (unsigned long long)(sum_area / m), max_area, (double)sum_cnt / m);
    free(us);
}

// One-shot screenshot. Set VROD_SHOT=<path.png> to dump the active screen
// after VROD_SHOT_FRAMES (default 60) main-loop iterations, then exit — lets
// us review the layout headlessly without a physical panel. lv_snapshot
// re-renders the widget tree to ARGB8888; lodepng writes a 32-bit PNG.
// Snapshot the active screen to a 32-bit PNG. Returns 0 on success. Used both
// by the one-shot VROD_SHOT path and the route animation's per-frame dump.
static int write_screenshot(const char *path)
{
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!snap) {
        fprintf(stderr, "[shot] snapshot failed\n");
        return -1;
    }

    uint32_t w = snap->header.w, h = snap->header.h, stride = snap->header.stride;
    uint8_t *rgba = malloc((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = snap->data + (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            // ARGB8888 is B,G,R,A in memory; lodepng wants R,G,B,A.
            uint8_t *o = rgba + ((size_t)y * w + x) * 4;
            o[0]       = row[x * 4 + 2];
            o[1]       = row[x * 4 + 1];
            o[2]       = row[x * 4 + 0];
            o[3]       = row[x * 4 + 3];
        }
    }
    // Encode to memory then write the file ourselves — LVGL builds lodepng
    // without disk I/O, so lodepng_*_file() can't open the output.
    uint8_t *png    = NULL;
    size_t   png_sz = 0;
    unsigned err    = lodepng_encode32(&png, &png_sz, rgba, w, h);
    free(rgba);
    lv_draw_buf_destroy(snap);
    if (err) {
        fprintf(stderr, "[shot] encode error %u: %s\n", err, lodepng_error_text(err));
        return -1;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[shot] cannot open %s\n", path);
        free(png);
        return -1;
    }
    fwrite(png, 1, png_sz, fp);
    fclose(fp);
    free(png);
    return 0;
}

static void maybe_screenshot(void)
{
    const char *path = getenv("VROD_SHOT");
    if (!path)
        return;

    static int  frame = 0;
    int         want  = 60;
    const char *f     = getenv("VROD_SHOT_FRAMES");
    if (f && atoi(f) > 0)
        want = atoi(f);
    if (++frame < want)
        return;

    if (write_screenshot(path) == 0)
        fprintf(stderr, "[shot] wrote %s\n", path);
    exit(0);
}

// Synthesise a vehicle_data snapshot from speed alone, so the compact map's
// gauge widgets show something plausible in VROD_MAP mode (which has no sim
// engine feeding vehicle_data). Gear bands + within-band revs, fixed temp/fuel.
// Compass bearing from point 1 to point 2 (degrees, 0 = north, CW).
static double sim_bearing(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * M_PI / 180.0, p2 = lat2 * M_PI / 180.0;
    double dl = (lon2 - lon1) * M_PI / 180.0;
    double b  = atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl));
    b         = b * 180.0 / M_PI;
    return b < 0 ? b + 360.0 : b;
}

static vehicle_data_t sim_map_vd(int mph)
{
    static const int hi[6] = {8, 18, 30, 45, 62, 90};
    vehicle_data_t   vd;
    memset(&vd, 0, sizeof(vd));
    vd.speed_mph     = (uint16_t)mph;
    vd.engine_temp_c = 85;
    vd.fuel_level    = 4;
    if (mph <= 0) {
        vd.gear = GEAR_NEUTRAL;
        return vd;
    }
    int g = 0, lo = 0;
    for (g = 0; g < 6; g++) {
        if (mph < hi[g])
            break;
        lo = hi[g];
    }
    if (g > 5)
        g = 5;
    int span = hi[g] - lo;
    vd.gear  = (gear_t)(g + 1);
    vd.rpm   = (uint16_t)(1500 + (span > 0 ? (mph - lo) * 6500 / span : 0));
    return vd;
}

int main(void)
{
    // 1) Vehicle state + bridge listener. Comes before SDL window creation
    //    so headless smoke tests (no display, e.g. CI) can still bind the
    //    bridge port and validate the parse/apply path even when window
    //    creation will fail seconds later.
    vehicle_data_init();
    phone_data_init();
    test_bridge_start();  // localhost:7700 listener for ad-hoc payloads

    // 2) LVGL core + color-emoji fallback chain. emoji_font_init only
    //    depends on lv_init, not on the SDL window, so it runs ahead of
    //    the window check — that way headless smoke tests still exercise
    //    the FreeType linkage (good CI canary).
    lv_init();
    emoji_font_init();

    // 3) SDL2 backend. No display → keep the process alive so the
    //    bridge thread (already on localhost:7700) can still service
    //    notify.py round-trips for protocol-level smoke testing.
    lv_display_t *display = lv_sdl_window_create(DISPLAY_W, DISPLAY_H);
    if (!display) {
        fprintf(stderr, "no SDL display — staying alive for bridge-only mode\n");
        pause();
        return 0;
    }
    lv_sdl_window_set_title(display, "V-Rod cluster simulator");
    lv_sdl_mouse_create();

    // Map spike: VROD_MAP=<tiles_dir> renders the moving-map screen instead of
    // the gauge. VROD_MAP_CENTER="lat,lon" (default: tileset centre),
    // VROD_MAP_PPT px/tile (default 256), VROD_MAP_SPEED mph. Honours VROD_SHOT.
    const char *mapdir = getenv("VROD_MAP");
    const char *mapzmta = getenv("VROD_MAP_ZMTA");
    if (mapdir || mapzmta) {
        // VROD_MAP_ZMTA exercises the packed-archive file loader (the on-device
        // SD path); VROD_MAP is the per-tile directory loader.
        map_tileset_t *ts = mapzmta ? map_tileset_load_file(mapzmta) : map_tileset_load_dir(mapdir);
        if (!ts) {
            fprintf(stderr, "map: failed to load %s\n", mapzmta ? mapzmta : mapdir);
            return 1;
        }
        double      ctx = 0, cty = 0;
        const char *ctr = getenv("VROD_MAP_CENTER");
        if (ctr) {
            double lat, lon;
            if (sscanf(ctr, "%lf,%lf", &lat, &lon) == 2)
                map_lonlat_to_tilef(lon, lat, ts->zoom, &ctx, &cty);
        } else {
            double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
            for (int i = 0; i < ts->ntiles; i++) {
                double x = ts->tiles[i].tx, y = ts->tiles[i].ty;
                if (x < minx)
                    minx = x;
                if (x > maxx)
                    maxx = x;
                if (y < miny)
                    miny = y;
                if (y > maxy)
                    maxy = y;
            }
            ctx = (minx + maxx + 1) / 2.0;
            cty = (miny + maxy + 1) / 2.0;
        }
        double ppt   = getenv("VROD_MAP_PPT") ? atof(getenv("VROD_MAP_PPT")) : 256.0;
        int    speed = getenv("VROD_MAP_SPEED") ? atoi(getenv("VROD_MAP_SPEED")) : 52;
        fprintf(stderr, "map: %d tiles z%d, center (%.3f,%.3f), ppt %.0f\n", ts->ntiles, ts->zoom,
                ctx, cty, ppt);
        lv_screen_load(screen_map_create(ts, DISPLAY_W, DISPLAY_H));

        // Route animation: VROD_TRACK=<file> (lines "lat lon speed_mph") drives
        // the map centre. With VROD_TRACK_OUT set, each frame is dumped to
        // <dir>/frame_%05d.png; otherwise it animates the route live in the
        // window (interpolated between points so the scroll glides).
        const char *track = getenv("VROD_TRACK");
        if (track) {
            const char *outdir = getenv("VROD_TRACK_OUT");
            FILE       *tf     = fopen(track, "r");
            if (!tf) {
                fprintf(stderr, "track: cannot open %s\n", track);
                return 1;
            }
#define TRK_MAX 4096
            static double lat_pt[TRK_MAX], lon_pt[TRK_MAX];
            static float  mph_pt[TRK_MAX];
            int           n = 0;
            {
                double la, lo;
                float  m;
                while (n < TRK_MAX && fscanf(tf, "%lf %lf %f", &la, &lo, &m) == 3) {
                    lat_pt[n] = la;
                    lon_pt[n] = lo;
                    mph_pt[n] = m;
                    n++;
                }
            }
            fclose(tf);
            if (n < 2) {
                fprintf(stderr, "track: too few points (%d)\n", n);
                return 1;
            }

            // Heading-up by default along a track; VROD_MAP_NORTH=1 forces
            // north-up so the two can be compared.
            bool head_up = !getenv("VROD_MAP_NORTH");

            if (outdir) {
                for (int i = 0; i < n; i++) {
                    double tx, ty;
                    map_lonlat_to_tilef(lon_pt[i], lat_pt[i], ts->zoom, &tx, &ty);
                    int    j = (i + 1) % n;
                    double heading =
                        head_up ? sim_bearing(lat_pt[i], lon_pt[i], lat_pt[j], lon_pt[j]) : -1.0;
                    vehicle_data_t vd = sim_map_vd((int)lrint(mph_pt[i]));
                    screen_map_render(tx, ty, ppt, heading);
                    screen_map_commit(&vd, settings_store_current());
                    lv_timer_handler();
                    char path[1024];
                    snprintf(path, sizeof(path), "%s/frame_%05d.png", outdir, i);
                    if (write_screenshot(path) != 0)
                        return 1;
                }
                fprintf(stderr, "track: wrote %d frames to %s\n", n, outdir);
                return 0;
            }

            // Interactive: glide along the route, interpolating between points.
            fprintf(stderr, "track: animating %d points live (%s)\n", n,
                    head_up ? "heading-up" : "north-up");
            double pos = 0.0;
            while (1) {
                int    i0 = (int)pos, i1 = (i0 + 1) % n;
                double f   = pos - i0;
                double lat = lat_pt[i0] + f * (lat_pt[i1] - lat_pt[i0]);
                double lon = lon_pt[i0] + f * (lon_pt[i1] - lon_pt[i0]);
                double tx, ty;
                map_lonlat_to_tilef(lon, lat, ts->zoom, &tx, &ty);
                double heading    = head_up ? sim_bearing(lat, lon, lat_pt[i1], lon_pt[i1]) : -1.0;
                vehicle_data_t vd = sim_map_vd((int)lrint(mph_pt[i0]));
                screen_map_render(tx, ty, ppt, heading);
                screen_map_commit(&vd, settings_store_current());
                screen_map_set_no_coverage(!map_tileset_covers(ts, (uint32_t)tx, (uint32_t)ty));
                lv_timer_handler();
                maybe_screenshot();
                usleep(UI_TICK_MS * 1000);
                pos += 0.08;  // fraction of a point per frame -> a gentle glide
                if (pos >= n)
                    pos -= n;
            }
        }

        while (1) {
            vehicle_data_t vd = sim_map_vd(speed);
            screen_map_render(ctx, cty, ppt, -1.0);
            screen_map_commit(&vd, settings_store_current());
            screen_map_set_no_coverage(!map_tileset_covers(ts, (uint32_t)ctx, (uint32_t)cty));
            lv_timer_handler();
            maybe_screenshot();
            usleep(UI_TICK_MS * 1000);
        }
    }

    // Diagnostic: log every invalidated area (VROD_INVLOG=1). Backend-
    // independent — reveals what the widget tree marks dirty per frame, which
    // is what drives render cost on the device's partial-refresh pipeline.
    if (getenv("VROD_INVLOG"))
        lv_display_add_event_cb(display, inv_log_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    int perf_n = perf_target();
    if (perf_n)
        lv_display_add_event_cb(display, perf_inv_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    // 4) Producer for the driving cycle. phone_data has no built-in
    //    producer here — push events from the test bridge (tools/notify.py)
    //    instead.
    sim_engine_start();

    // 5) Init settings (desktop shim — defaults only) and build the ride
    //    screen against the running sim. The ui_manager shim caches both
    //    screens so the settings → back path rejoins the original instead
    //    of rebuilding the ride each time.
    ui_manager_init();

    // 4) Main loop: pump vehicle data + settings into the UI, then let
    //    LVGL render. The sim updates s_data on its own thread;
    //    vehicle_data_get gives us a snapshot under the mutex so we never
    //    see a torn struct.
    //
    //    Long-press + swipe detection uses the same gesture FSM as the
    //    firmware (main/display/gesture.c). On desktop there's no
    //    FreeRTOS task to pin it to, so we run it inline.
    gesture_state_t gesture;
    gesture_init(&gesture);

    while (1) {
        vehicle_data_t snapshot;
        vehicle_data_get(&snapshot);
        screen_ride_update(&snapshot, settings_store_current());

        lv_indev_t *indev   = lv_indev_get_next(NULL);
        bool        pressed = false;
        lv_point_t  pt      = {0, 0};
        if (indev) {
            pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
            lv_indev_get_point(indev, &pt);
        }
        switch (gesture_update(&gesture, pressed, pt.x, pt.y, lv_tick_get())) {
        case GESTURE_LONG_PRESS:
            ui_manager_show_settings();
            break;
        case GESTURE_SWIPE_LEFT:
            phone_data_handle_swipe(PHONE_SWIPE_LEFT);
            break;
        case GESTURE_SWIPE_RIGHT:
            phone_data_handle_swipe(PHONE_SWIPE_RIGHT);
            break;
        case GESTURE_SWIPE_UP:
            phone_data_handle_swipe(PHONE_SWIPE_UP);
            break;
        case GESTURE_SWIPE_DOWN:
            phone_data_handle_swipe(PHONE_SWIPE_DOWN);
            break;
        case GESTURE_TAP:
            if (screen_ride_info_hit(gesture.last_x, gesture.last_y))
                screen_ride_cycle_info();
            break;
        case GESTURE_NONE:
        default:
            break;
        }

        if (perf_n) {
            static int pf    = 0;
            s_frame_inv_area = 0;
            s_frame_inv_cnt  = 0;
            uint64_t t0      = now_us();
            lv_timer_handler();
            uint32_t dt     = (uint32_t)(now_us() - t0);
            s_render_us[pf] = dt;
            s_area_px[pf]   = (uint32_t)s_frame_inv_area;
            s_cnt_px[pf]    = s_frame_inv_cnt;
            if (++pf >= perf_n) {
                perf_summary(perf_n);
                return 0;
            }
        } else {
            lv_timer_handler();
            maybe_screenshot();
        }
        usleep(UI_TICK_MS * 1000);
    }
}
