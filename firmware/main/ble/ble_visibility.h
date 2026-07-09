#pragma once
#include <stdbool.h>

// Pure-logic decision: given the current bond + override state, which
// kind of BLE advertising should the peripheral run?
//
// The cluster is invisible to strangers when bonded and the rider
// hasn't explicitly opted into "discoverable" mode in settings. If
// either condition flips (no bond yet, OR override is on), we fall
// back to general undirected advertising so any phone can find and
// pair with the cluster.

typedef enum {
    // Discoverable: connectable + named, anyone can scan and pair.
    BLE_ADV_MODE_UNDIRECTED = 0,
    // Hidden: connectable but non-discoverable and nameless. The bonded phone
    // reconnects by address (autoConnect / accept list, reliable); strangers see
    // only an anonymous device whose write characteristic is auth-gated. We use
    // this rather than true directed advertising because Android's autoConnect
    // catches directed adv unreliably.
    BLE_ADV_MODE_HIDDEN = 1,
} ble_adv_mode_t;

ble_adv_mode_t ble_visibility_decide(bool has_bond, bool visible_override);

// Policy: when a new bond is successfully stored, the visibility
// override auto-clears so the cluster doesn't stay broadcastable
// after the rider's "add a second phone" workflow completes.
// Returns the new override value the caller should persist —
// always false today, but factored as a function so the policy lives
// in one place and is testable.
bool ble_visibility_after_new_bond(bool visible_override_was);
