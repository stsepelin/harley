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
} notification_t;

typedef struct {
    media_state_t state;
    char          artist[MEDIA_FIELD_MAX];
    char          title [MEDIA_FIELD_MAX];
} now_playing_t;

// A parsed wire message. The protocol layer fills exactly one of these
// per call to phone_protocol_parse(); phone_data.c applies the event to
// its internal state under the lock.
typedef enum {
    PHONE_EVT_NOTIF         = 0x01,
    PHONE_EVT_NOTIF_DISMISS = 0x02,
    PHONE_EVT_MEDIA         = 0x03,
} phone_event_type_t;

typedef struct {
    phone_event_type_t type;
    union {
        notification_t notif;          // PHONE_EVT_NOTIF
        uint32_t       dismiss_id;     // PHONE_EVT_NOTIF_DISMISS
        now_playing_t  media;          // PHONE_EVT_MEDIA
    };
} phone_event_t;

// Combined snapshot, returned by phone_data_get() to consumers (UI).
typedef struct {
    notification_t notif;
    now_playing_t  media;
    // User-toggled overlay for the media banner. Set via a swipe-up
    // gesture when no notification is active; cleared on swipe-down,
    // on notif arrival, or when media stops playing.
    bool           media_banner_shown;
} phone_state_t;
