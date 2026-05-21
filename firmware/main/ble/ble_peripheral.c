#include "ble_peripheral.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "phone.h"
#include "phone_data.h"
#include "phone_protocol.h"

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

static uint8_t  s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static int gap_event_cb(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

// --- RX (phone → cluster) -------------------------------------------------

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
        phone_data_apply(&evt);
    } else {
        ESP_LOGW(TAG, "phone_protocol_parse pr=%d consumed=%u len=%u",
                 (int)pr, (unsigned)consumed, (unsigned)len);
    }
    return 0;
}

// --- GATT service table ---------------------------------------------------

static const struct ble_gatt_chr_def chrs[] = {
    {
        .uuid      = &RX_UUID.u,
        .access_cb = access_rx_cb,
        .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
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

static void start_advertising(void)
{
    // Split the advert. flags (3) + complete name "V-Rod Cluster" (15) +
    // complete 128-bit service UUID (18) = 36 bytes, which overflows the
    // 31-byte legacy advert limit. NimBLE returns BLE_HS_EMSGSIZE and
    // ble_gap_adv_start() never runs — the symptom Android-side is a
    // scan that stays in SCANNING forever. Move the UUID to the scan
    // response; Android's SCAN_MODE_LOW_LATENCY is an active scan, so
    // the scan response is fetched and ScanFilter.setServiceUuid() still
    // matches.
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
    ESP_LOGI(TAG, "advertising as 'V-Rod Cluster'");
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                state_set_connected(desc.peer_id_addr.val);
            } else {
                state_set_connected((const uint8_t[6]){0});
            }
            ESP_LOGI(TAG, "central connected; handle=%u", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "connect failed; status=%d", event->connect.status);
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        state_set_connected(NULL);
        start_advertising();
        return 0;
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
    // NimBLE expects NVS available for bond / IRK storage before init.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    int rc = nimble_port_init();
    if (rc != 0) { ESP_LOGE(TAG, "nimble_port_init rc=%d", rc); return; }

    ble_hs_cfg.reset_cb = on_host_reset;
    ble_hs_cfg.sync_cb  = on_host_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_count_cfg rc=%d", rc); return; }
    rc = ble_gatts_add_svcs(svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_add_svcs rc=%d", rc); return; }

    ble_svc_gap_device_name_set("V-Rod Cluster");

    nimble_port_freertos_init(nimble_host_task);
    state_set_powered(true);
}

void ble_peripheral_get_state(ble_peripheral_state_t *out)
{
    portENTER_CRITICAL(&s_state_mux);
    *out = s_state;
    portEXIT_CRITICAL(&s_state_mux);
}

void ble_peripheral_disconnect_active(void)
{
    // Tolerant of a concurrent disconnect — ble_gap_terminate returns
    // BLE_HS_ENOTCONN if the handle dies under us, which is the same
    // outcome we wanted anyway.
    uint16_t handle = s_conn_handle;
    if (handle == BLE_HS_CONN_HANDLE_NONE) return;
    int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0 && rc != BLE_HS_ENOTCONN) {
        ESP_LOGW(TAG, "ble_gap_terminate rc=%d", rc);
    }
}
