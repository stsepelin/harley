#pragma once
#include <stdint.h>

// Lazy-create + load a full-screen modal showing the BLE pairing passkey
// (6 digits) plus ACCEPT / CANCEL buttons. Tap routes the rider's answer
// back into NimBLE via ble_peripheral_pair_respond and returns to the
// previous screen.
//
// Wired into ble_peripheral via ble_peripheral_pair_set_callback —
// app_main registers screen_pairing_show as the callback at boot.
//
// Caller must already hold the LVGL lock (NimBLE GAP callbacks come
// off the host task, so the screen_pairing entrypoint takes the lock
// itself).
void screen_pairing_show(uint32_t passkey);
