#pragma once
#include <stdbool.h>
#include <stdint.h>

// Shared data shape between the BLE bridge (companion app today, ANCS/AMS
// later if we add an iOS path) and the cluster UI. Everything in this
// header is producer-agnostic: phone_protocol.c parses wire bytes into
// these structs; phone_data.c stores the latest values for the UI.

#define NOTIF_SENDER_MAX   48   // ~longest contact name we'll show in the banner
#define NOTIF_MSG_MAX      128
#define MEDIA_FIELD_MAX    48

typedef enum {
    NOTIF_KIND_CALL = 0,
    NOTIF_KIND_SMS  = 1,
    NOTIF_KIND_APP  = 2,
    NOTIF_KIND_LAST,
} notif_kind_t;

typedef enum {
    MEDIA_STATE_STOPPED = 0,
    MEDIA_STATE_PAUSED  = 1,
    MEDIA_STATE_PLAYING = 2,
    MEDIA_STATE_LAST,
} media_state_t;

typedef struct {
    bool         active;                   // false when no current notification
    uint32_t     id;                       // companion-app-assigned, used for dismiss
    notif_kind_t kind;
    char         sender [NOTIF_SENDER_MAX];
    char         message[NOTIF_MSG_MAX];
    // CALL kind only. `call_in_progress` is set by phone_data_call_accept;
    // the banner then switches from incoming-call UI (reject/accept
    // buttons) to in-call UI (running duration + end-call button).
    // `call_start_ms` is the lv_tick value captured at accept time so
    // the widget can render elapsed call duration.
    bool         call_in_progress;
    uint32_t     call_start_ms;
    // Hash of the source app package; keys the cluster's icon cache. 0 = none.
    uint32_t icon_id;
} notification_t;

typedef struct {
    media_state_t state;
    char          artist[MEDIA_FIELD_MAX];
    char          title [MEDIA_FIELD_MAX];
} now_playing_t;

// A parsed wire message. The protocol layer fills exactly one of these
// per call to phone_protocol_parse(); phone_data.c applies the event to
// its internal state under the lock.
// Cluster configuration pushed from the phone. Field-keyed: the CONFIG payload
// is a sequence of {u8 field_id, u8 len, value[len]} sub-fields, so each setting
// is written independently (changing the layout never clobbers the calibrated
// divisor, and vice versa). Unknown field ids are skipped by length. The has_*
// flags say which fields this message actually carried.
#define CONFIG_FIELD_SPEED_DIVISOR 0x01u  // u16 LE
#define CONFIG_FIELD_LAYOUT        0x02u  // u8 (0 = classic gauge, 1 = map)

typedef struct {
    bool     has_speed_divisor;
    uint16_t speed_divisor;  // raw ECM count -> mph
    bool     has_layout;
    uint8_t  layout;  // layout_t: 0 = classic, 1 = map
} vehicle_config_t;

// One chunk of an app-icon image (48x48 RGB565, sent opaque). Chunks for the
// same icon_id are reassembled by offset into a cluster-side cache; the NOTIF
// then references the icon by icon_id. `data` points into the parse buffer and
// is only valid during the apply call (copied out immediately).
typedef struct {
    uint32_t       icon_id;
    uint16_t       total_len;  // full image byte count (48*48*2 = 4608)
    uint16_t       offset;     // byte offset of this chunk within the image
    const uint8_t *data;
    uint16_t       len;  // bytes in this chunk
} icon_chunk_t;

// Rider position streamed from the phone's GPS (the cluster has no GPS of its
// own). Fixed-point to keep the wire payload small; the map view converts to
// degrees. heading_cd is course-over-ground in centidegrees, 0xFFFF = unknown.
typedef struct {
    int32_t  lat_e7;      // latitude  * 1e7
    int32_t  lon_e7;      // longitude * 1e7
    uint16_t heading_cd;  // 0..35999, or 0xFFFF when stationary/unknown
} location_t;

typedef enum {
    PHONE_EVT_NOTIF         = 0x01,
    PHONE_EVT_NOTIF_DISMISS = 0x02,
    PHONE_EVT_MEDIA         = 0x03,
    PHONE_EVT_CONFIG        = 0x04,
    PHONE_EVT_ICON          = 0x05,
    // Phone-side call transitions (the rider answered/ended on the phone, not
    // via the cluster buttons). Payload-less.
    PHONE_EVT_CALL_ACTIVE = 0x06,
    PHONE_EVT_CALL_END    = 0x07,
    PHONE_EVT_LOCATION    = 0x08,  // GPS position for the map view
} phone_event_type_t;

typedef struct {
    phone_event_type_t type;
    union {
        notification_t   notif;       // PHONE_EVT_NOTIF
        uint32_t         dismiss_id;  // PHONE_EVT_NOTIF_DISMISS
        now_playing_t    media;       // PHONE_EVT_MEDIA
        vehicle_config_t config;      // PHONE_EVT_CONFIG
        icon_chunk_t     icon;        // PHONE_EVT_ICON
        location_t       location;    // PHONE_EVT_LOCATION
    };
} phone_event_t;

// Latest GPS position snapshot, with freshness so the map can fall back when the
// feed goes stale (phone out of range, GPS lost). Returned by
// phone_data_get_location(); `valid` is false until the first fix arrives.
typedef struct {
    bool     valid;
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint16_t heading_cd;
    uint32_t age_ms;  // since the last fix was applied
} phone_location_t;

// Combined snapshot, returned by phone_data_get() to consumers (UI).
typedef struct {
    notification_t notif;
    now_playing_t  media;
    // User-toggled overlay for the media banner. Set via a swipe-up
    // gesture when no notification is active; cleared on swipe-down,
    // on notif arrival, or when media stops playing.
    bool           media_banner_shown;
} phone_state_t;
