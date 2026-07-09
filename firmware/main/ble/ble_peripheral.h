#pragma once
#include <stdbool.h>
#include <stdint.h>

// Bring up the BLE peripheral stack (NimBLE host, esp_hosted HCI to the
// on-board ESP32-C6) and start advertising as "Zeppl". Writes
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

// Push bytes out on the TX notify characteristic. No-op if no central
// is connected, or if the central hasn't subscribed (the CCCD descriptor
// isn't written yet). Caller owns `buf` — we copy into a NimBLE mbuf
// before returning. Returns true if the notify was queued, false otherwise.
bool ble_peripheral_notify(const uint8_t *buf, uint16_t len);

// --- Pairing (LE Secure Connections, numeric comparison) -------------------
// CONFIG_VROD_BLE_INSECURE disables the whole pairing flow; the rest of
// this header still compiles so call sites don't need ifdef guards.

// Callback the UI registers to render the 6-digit passkey when the
// security manager asks the user to confirm a numeric-comparison match.
// Fires from the NimBLE host task — the UI implementation must take the
// LVGL lock before touching widgets. After rendering, the UI calls
// ble_peripheral_pair_respond() with the rider's answer.
typedef void (*ble_peripheral_pair_request_cb_t)(uint32_t passkey);
void ble_peripheral_pair_set_callback(ble_peripheral_pair_request_cb_t cb);

// Inject the rider's accept / reject response into the SM flow. Must be
// called exactly once per pair_request_cb invocation.
void ble_peripheral_pair_respond(bool accept);

// Clear every stored bond from NVS. The next connect from any phone
// re-triggers the pairing flow. Safe to call from any task.
void ble_peripheral_forget_all_bonds(void);

// --- Visibility (stage 8) -------------------------------------------------

// Re-apply the (bond, visible_override) → advertising-mode decision and
// restart advertising. Called by the settings screen after toggling the
// BT_VISIBILITY row, so a flag-change takes effect immediately without
// waiting for the next disconnect. The override flag itself is read
// from settings_store_current(); this function only triggers the
// re-evaluation. Safe to call from any task; no-op while a central is
// connected (the new mode picks up at the next disconnect).
void ble_peripheral_refresh_visibility(void);
