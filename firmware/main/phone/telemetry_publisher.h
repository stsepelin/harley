#pragma once

// Starts a background task that samples the latest vehicle_data and pushes a
// telemetry frame to the phone over the TX notify characteristic at a steady
// low rate. No bytes hit the air until a central subscribes -
// ble_peripheral_notify() gates on the CCCD - so this is safe to start
// unconditionally at boot. Encoding lives in telemetry_codec (host-tested);
// this is the untestable radio/task glue.
void telemetry_publisher_start(void);
