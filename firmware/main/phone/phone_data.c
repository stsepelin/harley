#include "phone_data.h"
#include "phone_protocol.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"   /* lv_tick_get for call duration timer */

// Cluster→phone notify path. Declared weak so the desktop simulator can
// link the same phone_data.c without pulling in NimBLE — the sim and
// host tests build a no-op stub via the regular symbol resolution path
// (firmware/simulator/ble_peripheral_shim.c, firmware/test_apps/host
// stubs). The weak attribute lets phone_data.c stay producer-agnostic
// in its sources list without a build-system fork.
__attribute__((weak)) bool ble_peripheral_notify(const uint8_t *buf, uint16_t len)
{
    (void)buf; (void)len;
    return false;
}

static void send_cmd(phone_cmd_t cmd)
{
    uint8_t buf[3];
    size_t  n = phone_protocol_encode_cmd(cmd, buf, sizeof(buf));
    (void)ble_peripheral_notify(buf, (uint16_t)n);
}

static void send_dismiss(uint32_t id)
{
    uint8_t buf[7];
    size_t  n = phone_protocol_encode_dismiss(id, buf, sizeof(buf));
    (void)ble_peripheral_notify(buf, (uint16_t)n);
}

// Pending notifications wait behind whatever is currently in s_state.notif.
// FIFO of fixed depth so a flood of pushes can't grow unboundedly. When
// the active one is dismissed (END CALL, REJECT, swipe-to-dismiss, or a
// matching DISMISS event), the front of the queue is promoted.
#define NOTIF_QUEUE_MAX  4

// Non-call notifications clear themselves from the cluster after this long, so
// a message doesn't sit on the gauge for the whole ride. Cluster-only: we do
// NOT send a dismiss to the phone, so it stays in the phone's shade. Calls are
// never auto-dismissed (the rider has to accept/reject/end them).
#define NOTIF_AUTODISMISS_MS 8000u

static phone_state_t      s_state;
static notification_t     s_queue[NOTIF_QUEUE_MAX];
static int                s_queue_count;
static SemaphoreHandle_t  s_mutex;
// lv_tick when s_state.notif last became the active one; drives auto-dismiss.
static uint32_t s_shown_ms;

void phone_data_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_queue, 0, sizeof(s_queue));
    s_queue_count = 0;
    s_mutex       = xSemaphoreCreateMutex();
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
        s_shown_ms    = lv_tick_get();  // restart the auto-dismiss clock
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
        if (s_state.notif.active) {
            if (s_state.notif.id == evt->notif.id) {
                // Same notification re-posted (a call going incoming->ongoing, or
                // an app editing its own notification): update in place, keeping
                // the call state, instead of queuing a stale duplicate.
                bool     cip                   = s_state.notif.call_in_progress;
                uint32_t cs                    = s_state.notif.call_start_ms;
                s_state.notif                  = evt->notif;
                s_state.notif.call_in_progress = cip;
                s_state.notif.call_start_ms    = cs;
            } else {
                // A different notification while one is showing — queue it; it
                // surfaces when the current one is dismissed.
                enqueue_or_drop_locked(&evt->notif);
            }
        } else {
            s_state.notif = evt->notif;
            s_shown_ms    = lv_tick_get();  // start the auto-dismiss clock
        }
        // A new notification (front or queued) hides the media banner so
        // it doesn't cover the overlay when promoted.
        s_state.media_banner_shown = false;
        break;
    case PHONE_EVT_CALL_ACTIVE:
        // The rider answered on the phone: switch the active call banner to
        // in-progress (END CALL + running timer), mirroring the cluster button.
        if (s_state.notif.active && s_state.notif.kind == NOTIF_KIND_CALL &&
            !s_state.notif.call_in_progress) {
            s_state.notif.call_in_progress = true;
            s_state.notif.call_start_ms    = lv_tick_get();
        }
        break;
    case PHONE_EVT_CALL_END:
        // The call ended on the phone (answered elsewhere, hung up, rejected):
        // clear the active call banner and surface anything queued behind it.
        if (s_state.notif.active && s_state.notif.kind == NOTIF_KIND_CALL) {
            promote_next_locked();
        }
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
    case PHONE_EVT_CONFIG:
    case PHONE_EVT_ICON:
        break;  // handled in ble_peripheral (config -> NVS; icon -> cache)
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

void phone_data_tick(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return;
    // Auto-clear a lingering non-call notification. Calls (incoming or in
    // progress) are never timed out. Cluster-only: promote_next_locked doesn't
    // send a dismiss to the phone, so the entry stays in the phone's shade.
    if (s_state.notif.active && s_state.notif.kind != NOTIF_KIND_CALL &&
        (uint32_t)(lv_tick_get() - s_shown_ms) >= NOTIF_AUTODISMISS_MS) {
        promote_next_locked();
    }
    xSemaphoreGive(s_mutex);
}

// --- User actions --------------------------------------------------------

static void dismiss_active_locked(void)
{
    promote_next_locked();
}

void phone_data_handle_swipe(phone_swipe_dir_t dir)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    uint32_t dismiss_id = 0;
    bool     want_dismiss = false;
    if (s_state.notif.active) {
        // CALL requires an explicit button press — swipes are ignored.
        // SMS / app pushes dismiss on any swipe direction. Capture the
        // id under the lock; send the wire-side DISMISS after we
        // release it so we never hold the cluster mutex across a
        // potentially-blocking NimBLE notify call.
        if (s_state.notif.kind != NOTIF_KIND_CALL) {
            dismiss_id   = s_state.notif.id;
            want_dismiss = true;
            dismiss_active_locked();
        }
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

    if (want_dismiss) send_dismiss(dismiss_id);
}

void phone_data_call_accept(void)
{
    bool send = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (s_state.notif.active
     && s_state.notif.kind == NOTIF_KIND_CALL
     && !s_state.notif.call_in_progress) {
        s_state.notif.call_in_progress = true;
        s_state.notif.call_start_ms    = lv_tick_get();
        send = true;
    }
    xSemaphoreGive(s_mutex);
    if (send) send_cmd(PHONE_CMD_CALL_ACCEPT);
}

void phone_data_call_reject(void)
{
    bool send = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (s_state.notif.active && s_state.notif.kind == NOTIF_KIND_CALL) {
        dismiss_active_locked();
        send = true;
    }
    xSemaphoreGive(s_mutex);
    if (send) send_cmd(PHONE_CMD_CALL_REJECT);
}

void phone_data_call_end(void)
{
    bool send = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (s_state.notif.active
     && s_state.notif.kind == NOTIF_KIND_CALL
     && s_state.notif.call_in_progress) {
        dismiss_active_locked();
        send = true;
    }
    xSemaphoreGive(s_mutex);
    if (send) send_cmd(PHONE_CMD_CALL_END);
}

void phone_data_media_action(phone_media_action_t action)
{
    // Companion app is the source of truth for playback state, so we
    // don't synthesise anything locally — just forward the intent.
    switch (action) {
    case PHONE_MEDIA_ACTION_PREV:       send_cmd(PHONE_CMD_MEDIA_PREV);       break;
    case PHONE_MEDIA_ACTION_PLAY_PAUSE: send_cmd(PHONE_CMD_MEDIA_PLAY_PAUSE); break;
    case PHONE_MEDIA_ACTION_NEXT:       send_cmd(PHONE_CMD_MEDIA_NEXT);       break;
    }
}
