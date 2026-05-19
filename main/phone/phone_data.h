#pragma once
#include "phone.h"
#include <stdbool.h>

// Mutex-guarded latest-value store for phone-bridge state. The producer
// (BLE bridge later; a sim-side mock for now) calls phone_data_apply()
// with parsed events; the UI calls phone_data_get() to grab a snapshot.
// Mirrors the vehicle_data pattern.

void phone_data_init(void);

// Apply one parsed event. NOTIF replaces the current notification slot;
// NOTIF_DISMISS clears it iff `dismiss_id` matches the current `notif.id`
// (so a stale dismiss for an already-replaced notification is ignored);
// MEDIA replaces the current media slot.
void phone_data_apply(const phone_event_t *evt);

void phone_data_get(phone_state_t *out);

// User-facing actions. These dispatch to whatever phone-bridge backend
// is linked (BLE on device, mock on the simulator) and also clear the
// local state so the UI hides the banner immediately.

typedef enum {
    PHONE_SWIPE_LEFT,
    PHONE_SWIPE_RIGHT,
    PHONE_SWIPE_UP,
    PHONE_SWIPE_DOWN,
} phone_swipe_dir_t;

// State-machine entrypoint for swipe gestures. Both event_watcher
// (firmware) and the sim's main loop call this — keeping the routing
// rules in one place. Current behavior:
//   - SMS / app notif active: any swipe dismisses it.
//   - CALL notif active: swipes are ignored (must accept/reject).
//   - No notif, media playing, SWIPE_UP: show media banner.
//   - Media banner shown, SWIPE_DOWN: hide it.
void phone_data_handle_swipe(phone_swipe_dir_t dir);

// Explicit call actions. No-op if the active notification isn't a CALL.
//   accept: incoming → active; banner stays visible, switches to the
//           in-call layout (running duration + END CALL button).
//   reject: incoming → dismissed.
//   end:    active   → dismissed.
void phone_data_call_accept(void);
void phone_data_call_reject(void);
void phone_data_call_end(void);

typedef enum {
    PHONE_MEDIA_ACTION_PREV,
    PHONE_MEDIA_ACTION_PLAY_PAUSE,
    PHONE_MEDIA_ACTION_NEXT,
} phone_media_action_t;

// Media transport actions, invoked from the media-banner buttons. For
// now these only log + (eventually) forward over BLE; the local state
// is not synthesised because the phone is the source of truth.
void phone_data_media_action(phone_media_action_t action);
