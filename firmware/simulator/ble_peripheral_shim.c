// Desktop simulator stub for the BLE peripheral. The real one lives in
// main/ble/ble_peripheral.c and pulls in NimBLE + esp_hosted, neither
// of which has a host build. The sim doesn't have a radio; it just
// reports "advertising" so the settings/ride status surface renders.
//
// Flip s_sim_connected at runtime from the SDL key handler if you want
// to exercise the "connected" branch — keeping it as a regular static
// (not extern) for now since no caller needs to mutate it.
#include "ble_peripheral.h"
#include <string.h>

static const bool s_sim_connected = false;

void ble_peripheral_get_state(ble_peripheral_state_t *out)
{
    memset(out, 0, sizeof(*out));
    out->powered     = true;
    out->advertising = !s_sim_connected;
    out->connected   = s_sim_connected;
    if (s_sim_connected) {
        const char demo[] = "aa:bb:cc:dd:ee:ff";
        memcpy(out->peer_addr_str, demo, sizeof(demo));
    }
}

void ble_peripheral_disconnect_active(void)
{
}

// The sim doesn't drive a real radio so pairing can't actually happen
// here — the stubs just satisfy the linker and let the settings PHONE
// row's long-press handler compile against the same header the
// cluster uses.
void ble_peripheral_pair_set_callback(ble_peripheral_pair_request_cb_t cb)
{
    (void)cb;
}

void ble_peripheral_pair_respond(bool accept)
{
    (void)accept;
}

void ble_peripheral_forget_all_bonds(void)
{
}
