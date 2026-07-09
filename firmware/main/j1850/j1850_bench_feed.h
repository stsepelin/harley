#pragma once

// Bench-only synthetic frame feeder (CONFIG_VROD_J1850_BENCH_SPEED). Feeds the
// producer a fixed SPEED + RPM frame on a timer so the gauge and the phone
// telemetry show non-zero data with no J1850 bus attached. Never on a bike.
void j1850_bench_feed_start(void);
