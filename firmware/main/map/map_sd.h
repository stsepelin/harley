#pragma once

// Real (non-demo) moving map: mounts the microSD, loads a packed ZMTA archive
// (CONFIG_VROD_MAP_SD_PATH) into PSRAM, builds the compact map screen, and
// scrolls it from live phone GPS + vehicle_data. Unlike map_demo there is no
// synthetic route: position/heading come from the phone fix, the strip from the
// bus. No-op (logs a warning) if the card or archive is missing. Gated by
// CONFIG_VROD_MAP_SD. Idempotent + lazy: ui_manager calls it the first time the
// map layout is shown (always after ble_peripheral_init, so nimble RAM ordering
// holds), so a classic setting never mounts the card or loads the archive.
void map_sd_load(void);
