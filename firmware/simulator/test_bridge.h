#pragma once

// Spawn a background pthread that listens on localhost:7700 for raw
// TLV bytes (same wire format the cluster's BLE RX characteristic
// parses) and feeds them through phone_protocol_parse + phone_data_apply.
// Pair with tools/notify.py to push notification / media / dismiss
// events into a running simulator without flashing the cluster.
//
// Also installs a strong override of ble_peripheral_notify (weakly
// defined in phone_data.c) so the cluster→phone command bytes that
// fire when the sim's banner buttons are tapped get decoded and
// printed to stderr — handy for verifying that ACCEPT / END CALL /
// MEDIA_NEXT really do produce the expected wire payloads.
//
// Call once at startup, after phone_data_init.
void test_bridge_start(void);
