#include "ble_visibility.h"

ble_adv_mode_t ble_visibility_decide(bool has_bond, bool visible_override)
{
    if (!has_bond || visible_override) {
        return BLE_ADV_MODE_UNDIRECTED;
    }
    return BLE_ADV_MODE_DIRECTED;
}

bool ble_visibility_after_new_bond(bool visible_override_was)
{
    (void)visible_override_was;
    return false;
}
