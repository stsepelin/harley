#pragma once
#include <stdbool.h>
#include <stdint.h>

// Touch-gesture state machine. Pure logic, no LVGL — callers feed it the
// per-tick (pressed, x, y, now_ms) triple they read from the indev and
// it returns at most one classified event per call.
//
// The FSM runs identically on firmware (event_watcher_task) and on the
// desktop sim, which is the entire reason it lives here as its own
// module: prior to extraction the two callers had a copy of this state
// machine that had already started to drift.

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_LONG_PRESS,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
} gesture_event_t;

typedef struct {
    // Tunables. Initialised by gesture_init() to the canonical defaults
    // (600 ms / 60 px / 60 px); set them yourself afterwards if a caller
    // needs different thresholds.
    uint32_t long_press_ms;
    int      swipe_dist_min;   // px traveled along the dominant axis
    int      swipe_perp_max;   // px allowed on the other axis

    // Internal state. Treat as opaque.
    bool     pressing;
    bool     long_fired;
    uint32_t press_start_tick;
    int      press_start_x, press_start_y;
    int      last_x, last_y;
} gesture_state_t;

void gesture_init(gesture_state_t *g);

// Returns the gesture (if any) that closed on this tick. Long-press
// fires on hold; swipes fire on release. NONE is the common case.
gesture_event_t gesture_update(gesture_state_t *g,
                               bool pressed, int x, int y, uint32_t now_ms);
