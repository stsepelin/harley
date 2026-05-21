#pragma once
#include <stdbool.h>
#include <stdint.h>

// Bring up the BLE peripheral stack (NimBLE host, esp_hosted HCI to the
// on-board ESP32-C6) and start advertising as "V-Rod Cluster". Writes
// to the RX characteristic are parsed via phone_protocol_parse and
// applied via phone_data_apply — same data path the simulator uses,
// just driven by real bytes off the radio.
//
// Call after phone_data_init(): the lock the parse path takes lives in
// that module. NVS init happens here on first call (NimBLE wants its
// bond storage backend before the host task starts).
void ble_peripheral_init(void);

// Snapshot of the radio's current connection state for the UI thread.
// Three orthogonal bits: powered (stack came up at all), advertising
// (currently looking for a central), connected (a central is on the
// other end). peer_addr_str is "aa:bb:cc:dd:ee:ff" (lower case) when
// connected, empty otherwise — kept as a string so the UI doesn't
// need to know NimBLE address-type semantics.
typedef struct {
    bool powered;
    bool advertising;
    bool connected;
    char peer_addr_str[18];
} ble_peripheral_state_t;

void ble_peripheral_get_state(ble_peripheral_state_t *out);

// Gracefully drop the active central. No-op if nothing's connected.
// Safe to call from any task.
void ble_peripheral_disconnect_active(void);
