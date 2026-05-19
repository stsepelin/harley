#include "phone_data.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"   /* lv_tick_get for call duration timer */

// Pending notifications wait behind whatever is currently in s_state.notif.
// FIFO of fixed depth so a flood of pushes can't grow unboundedly. When
// the active one is dismissed (END CALL, REJECT, swipe-to-dismiss, or a
// matching DISMISS event), the front of the queue is promoted.
#define NOTIF_QUEUE_MAX  4

static phone_state_t      s_state;
static notification_t     s_queue[NOTIF_QUEUE_MAX];
static int                s_queue_count;
static SemaphoreHandle_t  s_mutex;

void phone_data_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_queue, 0, sizeof(s_queue));
    s_queue_count = 0;
    s_mutex = xSemaphoreCreateMutex();
}

static void queue_remove_at_locked(int idx)
{
    for (int j = idx; j + 1 < s_queue_count; j++) s_queue[j] = s_queue[j + 1];
    s_queue_count--;
}

static void promote_next_locked(void)
{
    if (s_queue_count > 0) {
        s_state.notif = s_queue[0];
        queue_remove_at_locked(0);
    } else {
        memset(&s_state.notif, 0, sizeof(s_state.notif));
    }
}

static void enqueue_or_drop_locked(const notification_t *n)
{
    // Full → drop the oldest non-CALL to make room for the new entry.
    // Calls outrank app/SMS pushes; we never bump a queued call to make
    // room for an SMS, only the reverse.
    if (s_queue_count >= NOTIF_QUEUE_MAX) {
        int drop = -1;
        for (int i = 0; i < s_queue_count; i++) {
            if (s_queue[i].kind != NOTIF_KIND_CALL) { drop = i; break; }
        }
        if (drop < 0) return;   // queue is all calls; drop the incoming
        queue_remove_at_locked(drop);
    }
    s_queue[s_queue_count++] = *n;
}

void phone_data_apply(const phone_event_t *evt)
{
    if (!evt) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    switch (evt->type) {
    case PHONE_EVT_NOTIF:
        // Don't overwrite an active notification — especially a ringing
        // or in-progress call. Queue it; it'll surface when the user
        // dismisses the current one.
        if (s_state.notif.active) {
            enqueue_or_drop_locked(&evt->notif);
        } else {
            s_state.notif = evt->notif;
        }
        // A new notification (front or queued) hides the media banner so
        // it doesn't cover the overlay when promoted.
        s_state.media_banner_shown = false;
        break;
    case PHONE_EVT_NOTIF_DISMISS:
        if (s_state.notif.active && s_state.notif.id == evt->dismiss_id) {
            promote_next_locked();
        } else {
            // Dismiss may target an entry still waiting in the queue
            // (e.g. user already swiped its SMS away on the phone).
            for (int i = 0; i < s_queue_count; i++) {
                if (s_queue[i].id == evt->dismiss_id) {
                    queue_remove_at_locked(i);
                    break;
                }
            }
        }
        break;
    case PHONE_EVT_MEDIA:
        s_state.media = evt->media;
        // If media stops, the banner has nothing useful left to show.
        if (s_state.media.state != MEDIA_STATE_PLAYING
         && s_state.media.state != MEDIA_STATE_PAUSED) {
            s_state.media_banner_shown = false;
        }
        break;
    }

    xSemaphoreGive(s_mutex);
}

void phone_data_get(phone_state_t *out)
{
    if (!out) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(out, &s_state, sizeof(s_state));
        xSemaphoreGive(s_mutex);
    }
}

// --- User actions --------------------------------------------------------
//
// For v1 these only update local state; eventually they should also send a
// command back to the companion app over BLE (ACCEPT_CALL, REJECT_CALL,
// MEDIA_NEXT etc.). Wiring that goes through a separate phone_bridge
// module once the on-device BLE path exists.

static void dismiss_active_locked(void)
{
    promote_next_locked();
}

void phone_data_handle_swipe(phone_swipe_dir_t dir)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    if (s_state.notif.active) {
        // CALL requires an explicit button press — swipes are ignored.
        // SMS / app pushes dismiss on any swipe direction.
        if (s_state.notif.kind != NOTIF_KIND_CALL) dismiss_active_locked();
    } else if (dir == PHONE_SWIPE_UP) {
        // Pull up the media banner if there's a track loaded.
        if (s_state.media.state == MEDIA_STATE_PLAYING
         || s_state.media.state == MEDIA_STATE_PAUSED) {
            s_state.media_banner_shown = true;
        }
    } else if (dir == PHONE_SWIPE_DOWN) {
        s_state.media_banner_shown = false;
    }

    xSemaphoreGive(s_mutex);
}

void phone_data_call_accept(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (s_state.notif.active
     && s_state.notif.kind == NOTIF_KIND_CALL
     && !s_state.notif.call_in_progress) {
        // TODO: phone_bridge_send(PHONE_CMD_CALL_ACCEPT) once BLE is wired.
        s_state.notif.call_in_progress = true;
        s_state.notif.call_start_ms    = lv_tick_get();
    }
    xSemaphoreGive(s_mutex);
}

void phone_data_call_reject(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (s_state.notif.active && s_state.notif.kind == NOTIF_KIND_CALL) {
        // TODO: phone_bridge_send(PHONE_CMD_CALL_REJECT) once BLE is wired.
        dismiss_active_locked();
    }
    xSemaphoreGive(s_mutex);
}

void phone_data_call_end(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (s_state.notif.active
     && s_state.notif.kind == NOTIF_KIND_CALL
     && s_state.notif.call_in_progress) {
        // TODO: phone_bridge_send(PHONE_CMD_CALL_END) once BLE is wired.
        dismiss_active_locked();
    }
    xSemaphoreGive(s_mutex);
}

void phone_data_media_action(phone_media_action_t action)
{
    // No local state to mutate — the companion app is the source of
    // truth for playback state. When BLE is wired this will forward the
    // command; for now it's a hook so the buttons have somewhere to go.
    (void)action;
}
