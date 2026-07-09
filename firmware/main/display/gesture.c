#include "gesture.h"
#include <stdlib.h>   /* abs() */

#define DEFAULT_LONG_PRESS_MS   600
#define DEFAULT_SWIPE_DIST_MIN  60
#define DEFAULT_SWIPE_PERP_MAX  60
#define DEFAULT_DOUBLE_TAP_MS   300

void gesture_init(gesture_state_t *g)
{
    g->long_press_ms    = DEFAULT_LONG_PRESS_MS;
    g->swipe_dist_min   = DEFAULT_SWIPE_DIST_MIN;
    g->swipe_perp_max   = DEFAULT_SWIPE_PERP_MAX;
    g->double_tap_ms    = DEFAULT_DOUBLE_TAP_MS;
    g->pressing         = false;
    g->long_fired       = false;
    g->press_start_tick = 0;
    g->press_start_x = g->press_start_y = 0;
    g->last_x        = g->last_y        = 0;
    g->pending_tap                      = false;
    g->pending_tap_tick                 = 0;
    g->pending_tap_x = g->pending_tap_y = 0;
}

static gesture_event_t classify_swipe(const gesture_state_t *g)
{
    int dx = g->last_x - g->press_start_x;
    int dy = g->last_y - g->press_start_y;
    if (abs(dx) >= g->swipe_dist_min && abs(dy) <= g->swipe_perp_max) {
        return (dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
    }
    if (abs(dy) >= g->swipe_dist_min && abs(dx) <= g->swipe_perp_max) {
        return (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
    }
    return GESTURE_NONE;
}

gesture_event_t gesture_update(gesture_state_t *g,
                               bool pressed, int x, int y, uint32_t now_ms)
{
    if (pressed) {
        if (!g->pressing) {
            // Rising edge — start of a gesture.
            g->pressing         = true;
            g->long_fired       = false;
            g->press_start_x    = x;
            g->press_start_y    = y;
            g->press_start_tick = now_ms;
        }
        g->last_x = x;
        g->last_y = y;

        // Long-press fires only if the finger hasn't already wandered far
        // enough to be a swipe. Once fired, swipe classification on release
        // is suppressed (long_fired stays set until the next press).
        if (!g->long_fired
         && abs(x - g->press_start_x) < g->swipe_dist_min
         && abs(y - g->press_start_y) < g->swipe_dist_min
         && (uint32_t)(now_ms - g->press_start_tick) >= g->long_press_ms) {
            g->long_fired = true;
            return GESTURE_LONG_PRESS;
        }
        return GESTURE_NONE;
    }

    // Falling edge — classify and reset.
    gesture_event_t out = GESTURE_NONE;
    if (g->pressing && !g->long_fired) {
        out = classify_swipe(g);
        // Not a swipe and the finger stayed put (below the swipe distance on
        // both axes) -> a tap. A large but non-clean drag (e.g. diagonal) is
        // neither swipe nor tap, so it stays NONE.
        if (out == GESTURE_NONE && abs(g->last_x - g->press_start_x) < g->swipe_dist_min &&
            abs(g->last_y - g->press_start_y) < g->swipe_dist_min) {
            out = GESTURE_TAP;
        }
    }
    g->pressing   = false;
    g->long_fired = false;

    if (out == GESTURE_TAP) {
        // A second tap close in time and space to the previous one upgrades to
        // a double-tap. The single TAP still fires immediately (keeps taps
        // responsive); consumers that care about double-tap act on the second.
        if (g->pending_tap && (uint32_t)(now_ms - g->pending_tap_tick) < g->double_tap_ms &&
            abs(g->last_x - g->pending_tap_x) <= g->swipe_dist_min &&
            abs(g->last_y - g->pending_tap_y) <= g->swipe_dist_min) {
            g->pending_tap = false;  // consumed; the next tap starts a fresh pair
            return GESTURE_DOUBLE_TAP;
        }
        g->pending_tap      = true;
        g->pending_tap_tick = now_ms;
        g->pending_tap_x    = g->last_x;
        g->pending_tap_y    = g->last_y;
    }
    return out;
}
