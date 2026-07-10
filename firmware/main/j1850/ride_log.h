#pragma once
#include "j1850_vpw.h"
#include <stdbool.h>
#include <stdint.h>

// On-board ride log: persists every decoded J1850 frame to a file on the
// microSD card so a speed/temp/gear capture can run with NO laptop attached.
// All file I/O happens in a low-priority flush task, off the sniffer/decode
// path and off Core 1 (LVGL) — the API here only enqueues bytes or sets a
// request flag. Every fault (no card, card full, write error) stops logging
// cleanly and is reported via the state below; the gauge is never affected.
//
// Retrieval: power off, pull the card, and run tools/j1850_report.py on
// /sdcard/ride_NNN.log (the file is capture.py-compatible).

typedef enum {
    RIDE_LOG_UNINIT = 0,  // before ride_log_init()
    RIDE_LOG_IDLE,        // card mounted, not recording
    RIDE_LOG_RECORDING,   // session file open, frames being written
    RIDE_LOG_NO_CARD,     // mount failed / no card inserted
    RIDE_LOG_ERROR,       // write or storage fault; logging stopped
} ride_log_state_t;

typedef struct {
    ride_log_state_t state;
    uint32_t         frames;    // frames written this session
    uint32_t         dropped;   // frames dropped (buffer full)
    uint32_t         mb_used;   // card space used, MB
    uint32_t         mb_total;  // card capacity, MB (0 until first stat)
} ride_log_stats_t;

// Mount the SD card and start the flush task. Safe to call once at boot; if
// no card is present the state becomes NO_CARD and start() will retry the
// mount. Never blocks the caller on I/O beyond the one-time mount.
void ride_log_init(void);

// Request start/stop of a recording session (bench-screen buttons). These
// only set a flag — the flush task does the actual open/close so no file I/O
// runs on the LVGL core. toggle() starts when idle, stops when recording.
void ride_log_start(void);
void ride_log_stop(void);
void ride_log_toggle(void);

// Enqueue one decoded frame for persistence. No-op unless RECORDING. Called
// from the sniffer task; formats + buffers only, never touches the card.
void ride_log_frame(const j1850_frame_t *f);

// Snapshot the current state + counters for the bench indicator.
void ride_log_get_stats(ride_log_stats_t *out);

// Delete every ride_NNN.log record from the card. Refuses while a recording is
// active (stop first) or if the card isn't mounted. Returns the number deleted,
// or -1 on refusal. Blocks briefly on directory I/O - call off the LVGL core.
int ride_log_purge(void);
