#include "ble_peripheral.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_visibility.h"
#include "phone.h"
#include "phone_data.h"
#include "phone_protocol.h"
#include "settings.h"
#include "settings_store.h"
#if CONFIG_VROD_J1850
#include "j1850_driver.h"  // apply calibrated speed divisor live
#endif

static const char *TAG = "ble_peripheral";

// State the UI thread reads via ble_peripheral_get_state(). Updated from
// the NimBLE host task (core 0 by default) and read from the UI thread
// (core 1). A short critical section is enough — the struct is small and
// the read site is once-per-frame.
static portMUX_TYPE              s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static ble_peripheral_state_t    s_state;

static void state_set_advertising(bool adv)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state.advertising = adv;
    portEXIT_CRITICAL(&s_state_mux);
}

static void state_set_connected(const uint8_t *addr_bytes)
{
    char buf[18] = {0};
    if (addr_bytes) {
        // NimBLE addresses are little-endian in memory; print MSB first so
        // the string matches what nRF Connect / Android Bluetooth UI shows.
        snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                 addr_bytes[5], addr_bytes[4], addr_bytes[3],
                 addr_bytes[2], addr_bytes[1], addr_bytes[0]);
    }
    portENTER_CRITICAL(&s_state_mux);
    s_state.connected   = (addr_bytes != NULL);
    s_state.advertising = false;
    memcpy(s_state.peer_addr_str, buf, sizeof(buf));
    portEXIT_CRITICAL(&s_state_mux);
}

static void state_set_powered(bool on)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state.powered = on;
    portEXIT_CRITICAL(&s_state_mux);
}

// Nordic UART Service-shaped UUIDs. Pulling these straight from the
// well-known NUS layout (rather than rolling our own) means nRF
// Connect / LightBlue / Web BLE all label our characteristics
// "RX/TX" for free during early validation. The wire payload is our
// own TLV — we just borrow the address space.
//
// NimBLE stores 128-bit UUIDs little-endian, so the bytes look reversed
// vs. the conventional 6E400001-... string form. Don't reorder.
#define NUS_UUID_BYTES(low_lsb, low_msb) \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
    0x93, 0xf3, 0xa3, 0xb5, low_lsb, low_msb, 0x40, 0x6e

static const ble_uuid128_t SVC_UUID = BLE_UUID128_INIT(NUS_UUID_BYTES(0x01, 0x00));
static const ble_uuid128_t RX_UUID  = BLE_UUID128_INIT(NUS_UUID_BYTES(0x02, 0x00));
static const ble_uuid128_t TX_UUID  = BLE_UUID128_INIT(NUS_UUID_BYTES(0x03, 0x00));

static uint8_t  s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_attr_handle;
static bool     s_tx_subscribed;       // set when the central writes CCCD

#if !CONFIG_VROD_BLE_INSECURE
// Pair-confirm UI hook. The GAP callback runs on the NimBLE host task,
// the LVGL UI on core 1 — the registered cb is called from the host
// task and is responsible for taking the LVGL lock if it touches
// widgets. s_pair_conn_handle pins which connection the rider's reply
// goes to, since a stale handle from a previous pairing would inject
// the response into the wrong session.
static ble_peripheral_pair_request_cb_t s_pair_cb;
static uint16_t                         s_pair_conn_handle;
#endif

static int gap_event_cb(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

// --- RX (phone → cluster) -------------------------------------------------

// Apply a config write-back: push the calibrated divisor to the live decoder
// and persist it. Runs on the NimBLE host task; settings_store_apply is only
// NVS I/O (no display/LVGL work), so it's safe here. Rare (a calibration), so
// the brief NVS write is fine.
static void apply_config(const vehicle_config_t *cfg)
{
#if CONFIG_VROD_J1850
    j1850_driver_set_speed_divisor(cfg->speed_divisor);
#endif
    settings_t s    = *settings_store_current();
    s.speed_divisor = cfg->speed_divisor;
    settings_store_apply(&s);  // validates + writes NVS
}

static int access_rx_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    // One TLV per write packet for v1. The cluster's max field
    // (NOTIF_MSG_MAX=128) plus headers fits well under a single 247-byte
    // LE-2M MTU, so reassembly isn't pulling its weight yet — add it
    // when a real payload pushes past one ATT_MTU.
    static uint8_t buf[256];
    uint16_t       len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_hs_mbuf_to_flat rc=%d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    size_t        consumed = 0;
    phone_event_t evt;
    phone_parse_result_t pr = phone_protocol_parse(buf, len, &consumed, &evt);
    if (pr == PHONE_PARSE_OK) {
        // One-line success log so monitor can confirm every TLV reaches
        // phone_data — the silent path makes "the cluster never showed
        // the call" indistinguishable from "the cluster never received
        // the bytes" without it. Cheap: a few notifs per minute at
        // most.
        if (evt.type == PHONE_EVT_CONFIG) {
            // Config write-back is cluster state, not a phone_data event:
            // apply the calibrated divisor live and persist it to NVS.
            ESP_LOGI(TAG, "rx CONFIG speed_divisor=%u", (unsigned)evt.config.speed_divisor);
            apply_config(&evt.config);
            return 0;
        }
        switch (evt.type) {
        case PHONE_EVT_NOTIF:
            ESP_LOGI(TAG, "rx NOTIF id=%08lx kind=%d sender='%s' msg='%.40s'",
                     (unsigned long)evt.notif.id, (int)evt.notif.kind, evt.notif.sender,
                     evt.notif.message);
            break;
        case PHONE_EVT_NOTIF_DISMISS:
            ESP_LOGI(TAG, "rx DISMISS id=%08lx", (unsigned long)evt.dismiss_id);
            break;
        case PHONE_EVT_MEDIA:
            ESP_LOGI(TAG, "rx MEDIA state=%d", (int)evt.media.state);
            break;
        case PHONE_EVT_CONFIG:
            break;  // handled above
        }
        phone_data_apply(&evt);
    } else {
        ESP_LOGW(TAG, "phone_protocol_parse pr=%d consumed=%u len=%u",
                 (int)pr, (unsigned)consumed, (unsigned)len);
    }
    return 0;
}

// NimBLE's ble_gatts_chr_is_sane() rejects entries with access_cb == NULL
// (table validation, not runtime gate), so even notify-only TX needs a
// non-NULL stub. The central can't actually read or write TX — the flag
// set has neither READ nor WRITE — so this is never invoked in practice;
// notifications go out via ble_gatts_notify_custom() instead.
static int access_tx_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)ctxt; (void)arg;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

// --- GATT service table ---------------------------------------------------

static const struct ble_gatt_chr_def chrs[] = {
    {
        .uuid      = &RX_UUID.u,
        .access_cb = access_rx_cb,
        // WRITE_AUTHEN requires an authenticated encrypted link — i.e.
        // an SC numeric-comparison pair has completed. Android's
        // BluetoothGatt sees the insufficient-authentication response
        // on a cold write and triggers pairing via the SM, then
        // retries. Without _AUTHEN, any unpaired central could write
        // here. CONFIG_VROD_BLE_INSECURE drops _AUTHEN back to the
        // unpaired flags for bench iteration.
#if CONFIG_VROD_BLE_INSECURE
        .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
#else
        .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP
                   | BLE_GATT_CHR_F_WRITE_AUTHEN,
#endif
    },
    {
        // TX (cluster → phone). Notify-only — the central reads via
        // BluetoothGattCallback.onCharacteristicChanged after enabling
        // notifications on the CCCD. Subscribing to TX doesn't carry a
        // privacy concern (the commands are public — play/pause, call
        // accept) so we don't gate the CCCD descriptor specifically;
        // RX being gated is enough to keep an unbonded central from
        // doing anything useful.
        .uuid       = &TX_UUID.u,
        .access_cb  = access_tx_cb,
        .flags      = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_attr_handle,
    },
    { 0 },
};

static const struct ble_gatt_svc_def svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &SVC_UUID.u,
        .characteristics = chrs,
    },
    { 0 },
};

// --- GAP / advertising ----------------------------------------------------

// First bonded peer's identity address, if any. The cluster supports at
// most one connection at a time, and the most-recently-paired phone is
// the one we should advertise toward. Returns true and fills *out on a
// hit; false on no bond stored or on a NimBLE lookup error.
static bool first_bonded_peer(ble_addr_t *out)
{
    ble_addr_t peers[1];
    int        num = 0;
    int rc = ble_store_util_bonded_peers(peers, &num, (int)(sizeof(peers) / sizeof(peers[0])));
    if (rc != 0 || num <= 0)
        return false;
    *out = peers[0];
    return true;
}

static void start_advertising(void)
{
    ble_addr_t     peer        = {0};
    bool           has_bond    = first_bonded_peer(&peer);
    bool           override_on = settings_store_current()->ble_visible_override;
    ble_adv_mode_t mode        = ble_visibility_decide(has_bond, override_on);

    if (mode == BLE_ADV_MODE_HIDDEN) {
        // Hidden: undirected + CONNECTABLE, but non-discoverable and nameless.
        // The bonded phone reconnects by address via autoConnect (accept list),
        // which is reliable - unlike true directed advertising, which Android's
        // autoConnect catches only intermittently (parked, it misses the
        // low-duty directed windows, so Reconnect never fires). Strangers doing
        // a general/limited scan ignore a non-discoverable advert, and there's
        // no name or service UUID to identify it; the RX write is auth-gated
        // regardless. `peer` is unused in this mode.
        (void)peer;
        struct ble_hs_adv_fields fields = {0};
        fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;  // no DISC flag => non-discoverable
        int rc       = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gap_adv_set_fields(hidden) rc=%d", rc);
            return;
        }
        struct ble_gap_adv_params params = {0};
        params.conn_mode                 = BLE_GAP_CONN_MODE_UND;
        params.disc_mode                 = BLE_GAP_DISC_MODE_NON;
        rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gap_adv_start(hidden) rc=%d", rc);
            return;
        }
        state_set_advertising(true);
        ESP_LOGI(TAG, "advertising (hidden connectable) for bonded peer");
        return;
    }

    // Undirected (general discoverable) — used when no bond exists or
    // when the rider has toggled BT VISIBILITY on to add another phone.
    //
    // Keep the 128-bit service UUID in the scan response, not the main
    // advert. flags (3) + complete name + complete 128-bit UUID (18) can
    // exceed the 31-byte legacy advert limit; when it does NimBLE returns
    // BLE_HS_EMSGSIZE and ble_gap_adv_start() never runs — the symptom
    // Android-side is a scan that stays in SCANNING forever. Android's
    // SCAN_MODE_LOW_LATENCY is an active scan, so the scan response is
    // fetched and ScanFilter.setServiceUuid() still matches.
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name             = (uint8_t *)ble_svc_gap_device_name();
    fields.name_len         = (uint8_t)strlen((const char *)fields.name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gap_adv_set_fields rc=%d", rc); return; }

    struct ble_hs_adv_fields rsp = { 0 };
    rsp.uuids128             = (ble_uuid128_t *)&SVC_UUID;
    rsp.num_uuids128         = 1;
    rsp.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields rc=%d", rc); return; }

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                           gap_event_cb, NULL);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc); return; }

    state_set_advertising(true);
    ESP_LOGI(TAG, "advertising as 'Zeppl' (undirected, %s)", has_bond ? "override-on" : "no bond");
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            // s_conn_handle is read from the UI thread (ble_peripheral_notify,
            // ble_peripheral_disconnect_active) so the write needs to go
            // through the same critical section as the rest of s_state.
            portENTER_CRITICAL(&s_state_mux);
            s_conn_handle = event->connect.conn_handle;
            portEXIT_CRITICAL(&s_state_mux);
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                state_set_connected(desc.peer_id_addr.val);
            } else {
                state_set_connected((const uint8_t[6]){0});
            }
            ESP_LOGI(TAG, "central connected; handle=%u", event->connect.conn_handle);
            // Ask for a generous supervision timeout so a brief esp_hosted / RF
            // stall doesn't drop the link (we were seeing status=8/19 churn with
            // whatever short timeout the phone negotiated). 30-50 ms interval,
            // no slave latency, 6 s timeout (units: interval 1.25 ms, timeout 10 ms).
            struct ble_gap_upd_params upd = {
                .itvl_min            = 24,
                .itvl_max            = 40,
                .latency             = 0,
                .supervision_timeout = 600,
            };
            int urc = ble_gap_update_params(event->connect.conn_handle, &upd);
            if (urc != 0)
                ESP_LOGW(TAG, "ble_gap_update_params rc=%d", urc);
        } else {
            ESP_LOGW(TAG, "connect failed; status=%d", event->connect.status);
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        portENTER_CRITICAL(&s_state_mux);
        s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
        s_tx_subscribed = false;
        portEXIT_CRITICAL(&s_state_mux);
        state_set_connected(NULL);
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        // Track the TX-characteristic subscribe state — without it
        // ble_peripheral_notify() would queue mbufs the host then drops
        // (and we'd see ble_gatts_notify_custom rc=14, NOTIFY_DISABLED).
        ESP_LOGI(TAG, "subscribe attr=%u notify=%d indicate=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify, event->subscribe.cur_indicate);
        if (event->subscribe.attr_handle == s_tx_attr_handle) {
            portENTER_CRITICAL(&s_state_mux);
            s_tx_subscribed = event->subscribe.cur_notify;
            portEXIT_CRITICAL(&s_state_mux);
        }
        return 0;
#if !CONFIG_VROD_BLE_INSECURE
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        // SM wants the rider's input. With sm_io_cap = DISPLAY_ONLY and
        // sm_sc = 1 + sm_mitm = 1, the only action we should ever see is
        // NUMCMP (numeric comparison): both sides show a 6-digit number,
        // user confirms they match. Save the conn_handle so the
        // ble_peripheral_pair_respond() call from the UI thread targets
        // the right connection, then hand the passkey to the registered
        // UI callback. If no callback is set we have to reject (no way
        // to ask the user) — silently inject reject so the SM doesn't
        // hang.
        ESP_LOGI(TAG, "passkey action=%d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            s_pair_conn_handle = event->passkey.conn_handle;
            if (s_pair_cb) {
                s_pair_cb(event->passkey.params.numcmp);
            } else {
                ESP_LOGW(TAG, "no pair UI callback; rejecting");
                ble_peripheral_pair_respond(false);
            }
        } else {
            // Anything else (passkey display, OOB) is a configuration
            // mismatch — log loudly and reject.
            ESP_LOGW(TAG, "unexpected passkey action %d; rejecting",
                     event->passkey.params.action);
            struct ble_sm_io io = { .action = event->passkey.params.action };
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption changed; status=%d", event->enc_change.status);
        if (event->enc_change.status == 0) {
            // Successful encryption upgrade => a bond is now stored (or
            // a stored bond was used). Auto-revert the visibility override
            // so the cluster goes back to invisible-to-strangers after
            // the "add another phone" workflow completes. Idempotent for
            // the bonded-reconnect case where the override was already
            // off.
            const settings_t *cur = settings_store_current();
            if (cur->ble_visible_override) {
                settings_t next           = *cur;
                next.ble_visible_override = ble_visibility_after_new_bond(true);
                settings_store_apply(&next);
                ESP_LOGI(TAG, "BT visibility auto-reverted after pairing");
            }
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        // Directed advertising completed without a connection — the
        // controller stopped after its window. Re-evaluate (bond may
        // still be there, override flag may have flipped) and restart.
        ESP_LOGI(TAG, "adv complete; reason=%d", event->adv_complete.reason);
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // Peer initiated pairing on a connection that already has a
        // bond. Delete the stale bond and let the new pairing through;
        // NimBLE expects BLE_GAP_REPEAT_PAIRING_RETRY to proceed.
        ESP_LOGI(TAG, "repeat pairing — dropping stale bond");
        {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
#endif  /* !CONFIG_VROD_BLE_INSECURE */
    default:
        return 0;
    }
}

// --- Host lifecycle -------------------------------------------------------

static void on_host_reset(int reason)
{
    ESP_LOGW(TAG, "host reset; reason=%d", reason);
}

static void on_host_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) { ESP_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc); return; }
    start_advertising();
}

static void nimble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();           // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

void ble_peripheral_init(void)
{
    // NVS is already initialised by settings_store_init() in app_main —
    // NimBLE just needs it available before nimble_port_init runs, not a
    // dedicated init here. Caller is responsible for the ordering.
    int rc = nimble_port_init();
    if (rc != 0) { ESP_LOGE(TAG, "nimble_port_init rc=%d", rc); return; }

    ble_hs_cfg.reset_cb = on_host_reset;
    ble_hs_cfg.sync_cb  = on_host_sync;

#if !CONFIG_VROD_BLE_INSECURE
    // LE Secure Connections + numeric comparison. Picking the right IO
    // cap is the whole game here:
    //   - DISPLAY_ONLY × KEYBOARD_DISPLAY (Android) under LE SC selects
    //     Passkey Entry (cluster displays, phone types). Our gap event
    //     handler only services BLE_SM_IOACT_NUMCMP, so that path used
    //     to reject and tear down the link.
    //   - DISPLAY_YES_NO × KEYBOARD_DISPLAY under LE SC selects Numeric
    //     Comparison: both sides show the same 6-digit number, each
    //     user confirms "yes, they match". This is what screen_pairing
    //     is built for, and it matches the cluster's hardware (we
    //     have a touchscreen for the yes/no, no keypad for entry).
    ble_hs_cfg.sm_io_cap          = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding         = 1;
    ble_hs_cfg.sm_mitm            = 1;
    ble_hs_cfg.sm_sc              = 1;
    ble_hs_cfg.sm_our_key_dist    = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    // store_status_cb defaults to ble_store_util_status_rr — if NVS
    // fills up, oldest bond gets evicted. Persisting bonds in NVS comes
    // from CONFIG_BT_NIMBLE_NVS_PERSIST=y in sdkconfig.
    ble_hs_cfg.store_status_cb    = ble_store_util_status_rr;
#endif

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_count_cfg rc=%d", rc); return; }
    rc = ble_gatts_add_svcs(svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_add_svcs rc=%d", rc); return; }

    ble_svc_gap_device_name_set("Zeppl");

    nimble_port_freertos_init(nimble_host_task);
    state_set_powered(true);
}

void ble_peripheral_get_state(ble_peripheral_state_t *out)
{
    portENTER_CRITICAL(&s_state_mux);
    *out = s_state;
    portEXIT_CRITICAL(&s_state_mux);
}

bool ble_peripheral_notify(const uint8_t *buf, uint16_t len)
{
    if (!buf || len == 0) return false;
    // Snapshot handle + subscribe state under the mutex — the NimBLE
    // host task updates them from GAP events on a different core, and
    // an uint16_t read without a barrier can see stale bytes on RISC-V.
    uint16_t handle;
    bool     subscribed;
    portENTER_CRITICAL(&s_state_mux);
    handle     = s_conn_handle;
    subscribed = s_tx_subscribed;
    portEXIT_CRITICAL(&s_state_mux);
    if (handle == BLE_HS_CONN_HANDLE_NONE) return false;
    if (!subscribed) return false;
    // ble_hs_mbuf_from_flat allocates an mbuf chain and copies; NimBLE
    // takes ownership when ble_gatts_notify_custom succeeds, frees the
    // chain when it fails — we don't need to release on either path.
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (!om) {
        ESP_LOGW(TAG, "notify: mbuf alloc failed (len=%u)", len);
        return false;
    }
    int rc = ble_gatts_notify_custom(handle, s_tx_attr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gatts_notify_custom rc=%d", rc);
        return false;
    }
    return true;
}

void ble_peripheral_disconnect_active(void)
{
    // Snapshot under the same critical section the GAP callback writes
    // under. ble_gap_terminate is itself tolerant of a stale handle —
    // returns BLE_HS_ENOTCONN if the disconnect raced us.
    uint16_t handle;
    portENTER_CRITICAL(&s_state_mux);
    handle = s_conn_handle;
    portEXIT_CRITICAL(&s_state_mux);
    if (handle == BLE_HS_CONN_HANDLE_NONE) return;
    int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0 && rc != BLE_HS_ENOTCONN) {
        ESP_LOGW(TAG, "ble_gap_terminate rc=%d", rc);
    }
}

// --- Pairing -------------------------------------------------------------

void ble_peripheral_pair_set_callback(ble_peripheral_pair_request_cb_t cb)
{
#if CONFIG_VROD_BLE_INSECURE
    (void)cb;
#else
    s_pair_cb = cb;
#endif
}

void ble_peripheral_pair_respond(bool accept)
{
#if CONFIG_VROD_BLE_INSECURE
    (void)accept;
#else
    if (s_pair_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "pair_respond with no pending request");
        return;
    }
    struct ble_sm_io io = {
        .action        = BLE_SM_IOACT_NUMCMP,
        .numcmp_accept = accept ? 1 : 0,
    };
    int rc = ble_sm_inject_io(s_pair_conn_handle, &io);
    if (rc != 0) ESP_LOGW(TAG, "ble_sm_inject_io rc=%d", rc);
    // One-shot: clear so a UI bug double-tapping after the SM has
    // already concluded can't accidentally inject into a future
    // pairing session.
    s_pair_conn_handle = BLE_HS_CONN_HANDLE_NONE;
#endif
}

void ble_peripheral_forget_all_bonds(void)
{
#if !CONFIG_VROD_BLE_INSECURE
    int rc = ble_store_clear();
    if (rc != 0) ESP_LOGW(TAG, "ble_store_clear rc=%d", rc);
    else         ESP_LOGI(TAG, "all bonds cleared");
    // Bond just disappeared → directed adv has no peer to target.
    // Restart so undirected adv takes over and the cluster is
    // pair-able again from any phone without a power-cycle.
    ble_peripheral_refresh_visibility();
#endif
}

void ble_peripheral_refresh_visibility(void)
{
    // Don't disrupt an active connection. The new mode applies at the
    // next disconnect, where the existing BLE_GAP_EVENT_DISCONNECT path
    // already calls start_advertising() and the fresh override flag is
    // read from settings then.
    portENTER_CRITICAL(&s_state_mux);
    bool connected = (s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
    portEXIT_CRITICAL(&s_state_mux);
    if (connected) {
        ESP_LOGI(TAG, "refresh_visibility: deferred until disconnect");
        return;
    }
    (void)ble_gap_adv_stop();  // OK if not currently advertising
    start_advertising();
}
