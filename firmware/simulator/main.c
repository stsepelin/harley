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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

#define DISPLAY_W   800
#define DISPLAY_H   800
#define UI_TICK_MS  15      // ~66 FPS, matches firmware's LV_DEF_REFR_PERIOD

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

    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!snap) {
        fprintf(stderr, "[shot] snapshot failed\n");
        exit(1);
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
        exit(1);
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[shot] cannot open %s\n", path);
        exit(1);
    }
    fwrite(png, 1, png_sz, fp);
    fclose(fp);
    free(png);
    fprintf(stderr, "[shot] wrote %s (%ux%u, %zu bytes)\n", path, w, h, png_sz);
    exit(0);
}

int main(void)
{
    // 1) Vehicle state + bridge listener. Comes before SDL window creation
    //    so headless smoke tests (no display, e.g. CI) can still bind the
    //    bridge port and validate the parse/apply path even when window
    //    creation will fail seconds later.
    vehicle_data_init();
    phone_data_init();
    test_bridge_start();       // localhost:7700 listener for ad-hoc payloads

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
        lv_point_t  pt      = { 0, 0 };
        if (indev) {
            pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
            lv_indev_get_point(indev, &pt);
        }
        switch (gesture_update(&gesture, pressed, pt.x, pt.y, lv_tick_get())) {
        case GESTURE_LONG_PRESS:  ui_manager_show_settings();              break;
        case GESTURE_SWIPE_LEFT:  phone_data_handle_swipe(PHONE_SWIPE_LEFT);  break;
        case GESTURE_SWIPE_RIGHT: phone_data_handle_swipe(PHONE_SWIPE_RIGHT); break;
        case GESTURE_SWIPE_UP:    phone_data_handle_swipe(PHONE_SWIPE_UP);    break;
        case GESTURE_SWIPE_DOWN:  phone_data_handle_swipe(PHONE_SWIPE_DOWN);  break;
        case GESTURE_TAP:
            if (screen_ride_info_hit(gesture.last_x, gesture.last_y))
                screen_ride_cycle_info();
            break;
        case GESTURE_NONE:        default:                                    break;
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
