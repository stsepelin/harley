#pragma once
#include "lvgl.h"
#include "phone.h"

typedef enum {
    MEDIA_ACTION_PREV,
    MEDIA_ACTION_PLAY_PAUSE,
    MEDIA_ACTION_NEXT,
} media_action_t;

typedef void (*media_action_cb_t)(media_action_t action);

// Bottom-anchored overlay showing the currently playing track + three
// transport buttons. Hidden by default; swipe up reveals it (handled in
// phone_data / event_watcher), swipe down hides it. Buttons fire the
// callback so screen_ride can dispatch to phone_data + eventually the
// BLE outgoing command path.
lv_obj_t *media_banner_create(lv_obj_t *parent, media_action_cb_t on_action);

// Pass the latest snapshot. `visible` is the user-toggled show/hide
// state; the widget only invalidates on transitions or content changes.
void media_banner_update(lv_obj_t *cont, const now_playing_t *np, bool visible);
