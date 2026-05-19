#pragma once

// Desktop-only stand-in for the BLE phone bridge. Spawns a FreeRTOS task
// (which becomes a pthread under the freertos_shim) that emits a fixed
// timeline of notifications + media events into phone_data. The real
// firmware build will replace this with a BLE GATT server module.
void phone_mock_start(void);
