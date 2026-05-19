#pragma once
#include "lvgl.h"
#include "phone.h"
#include <stdbool.h>

// Which call button the user pressed. Only fires for CALL notifications —
// SMS / app banners have no buttons (dismissed via swipe).
typedef enum {
    CALL_ACTION_ACCEPT,   // incoming → active
    CALL_ACTION_REJECT,   // incoming → dismissed
    CALL_ACTION_END,      // active   → dismissed
} call_action_t;

typedef void (*notif_call_action_cb_t)(call_action_t action);

// Bottom-anchored overlay shown when a phone notification is active.
// Resizes between three modes: incoming-call (REJECT/ACCEPT buttons +
// "incoming" message), active-call (END CALL button + running duration
// timer), and informational (SMS/app — no buttons). Caller-supplied
// callback runs on the LVGL event thread under the display lock.
lv_obj_t *notification_banner_create(lv_obj_t *parent, notif_call_action_cb_t on_call_action);
void      notification_banner_update(lv_obj_t *cont, const notification_t *notif);
