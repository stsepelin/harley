#include "phone_mock.h"
#include "phone_data.h"
#include "phone.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// Each step is a delay in ms followed by one event applied to phone_data.
// The schedule loops, so the sim shows every state at a predictable
// cadence when you leave it running.

typedef enum {
    STEP_NOTIF,
    STEP_DISMISS,
    STEP_MEDIA,
} step_kind_t;

typedef struct {
    uint32_t      delay_ms;
    step_kind_t   kind;
    union {
        notification_t notif;
        uint32_t       dismiss_id;
        now_playing_t  media;
    };
} mock_step_t;

// Fixed timeline. Keep it short so a sim observer sees the full cycle
// without leaving it running for ages.
static const mock_step_t s_steps[] = {
    // 5 s in: an incoming call. Banner pops up at the top.
    { 5000, STEP_NOTIF,
        .notif = { .active = true, .id = 100, .kind = NOTIF_KIND_CALL,
                   .sender = "Mom", .message = "incoming" } },

    // 4 s later: call answered → dismiss.
    { 4000, STEP_DISMISS, .dismiss_id = 100 },

    // 3 s later: SMS lands.
    { 3000, STEP_NOTIF,
        .notif = { .active = true, .id = 101, .kind = NOTIF_KIND_SMS,
                   .sender = "John", .message = "running 10 min late" } },

    // 6 s later: read → dismiss.
    { 6000, STEP_DISMISS, .dismiss_id = 101 },

    // 2 s later: media starts. Now-playing joins the info-slot rotation.
    { 2000, STEP_MEDIA,
        .media = { .state = MEDIA_STATE_PLAYING,
                   .artist = "Ramones", .title = "Blitzkrieg Bop" } },

    // 12 s later: track change.
    { 12000, STEP_MEDIA,
        .media = { .state = MEDIA_STATE_PLAYING,
                   .artist = "Black Sabbath", .title = "Iron Man" } },

    // 10 s later: app push (e.g. WhatsApp).
    { 10000, STEP_NOTIF,
        .notif = { .active = true, .id = 102, .kind = NOTIF_KIND_APP,
                   .sender = "WhatsApp", .message = "Alice: see you at 6" } },

    // 6 s later: dismiss it.
    { 6000, STEP_DISMISS, .dismiss_id = 102 },

    // 4 s later: media paused.
    { 4000, STEP_MEDIA,
        .media = { .state = MEDIA_STATE_PAUSED,
                   .artist = "Black Sabbath", .title = "Iron Man" } },

    // 3 s later: queue-exercise. Incoming call lands, then an SMS
    // arrives 2 s later — the SMS must NOT replace the call. Once the
    // call is dismissed (5 s after that), the queued SMS appears.
    { 3000, STEP_NOTIF,
        .notif = { .active = true, .id = 200, .kind = NOTIF_KIND_CALL,
                   .sender = "Boss", .message = "incoming" } },
    { 2000, STEP_NOTIF,
        .notif = { .active = true, .id = 201, .kind = NOTIF_KIND_SMS,
                   .sender = "Garage", .message = "tyres ready" } },
    { 5000, STEP_DISMISS, .dismiss_id = 200 },
    { 6000, STEP_DISMISS, .dismiss_id = 201 },

    // 5 s later: stopped (drops the info-slot rotation back to 4 entries).
    { 5000, STEP_MEDIA,
        .media = { .state = MEDIA_STATE_STOPPED, .artist = "", .title = "" } },
};

static void apply_step(const mock_step_t *s)
{
    phone_event_t evt;
    memset(&evt, 0, sizeof(evt));
    switch (s->kind) {
    case STEP_NOTIF:
        evt.type  = PHONE_EVT_NOTIF;
        evt.notif = s->notif;
        break;
    case STEP_DISMISS:
        // If the user has accepted the call this dismiss is for, skip it.
        // A real phone wouldn't phantom-hang-up mid-call; the scripted
        // dismiss models the caller hanging up after the *unanswered*
        // window. The user clicking END CALL clears the state directly.
        {
            phone_state_t st;
            phone_data_get(&st);
            if (st.notif.active
             && st.notif.id == s->dismiss_id
             && st.notif.kind == NOTIF_KIND_CALL
             && st.notif.call_in_progress) {
                return;
            }
        }
        evt.type       = PHONE_EVT_NOTIF_DISMISS;
        evt.dismiss_id = s->dismiss_id;
        break;
    case STEP_MEDIA:
        evt.type  = PHONE_EVT_MEDIA;
        evt.media = s->media;
        break;
    }
    phone_data_apply(&evt);
}

static void mock_task(void *arg)
{
    (void)arg;
    while (1) {
        for (size_t i = 0; i < sizeof(s_steps) / sizeof(s_steps[0]); i++) {
            vTaskDelay(pdMS_TO_TICKS(s_steps[i].delay_ms));
            apply_step(&s_steps[i]);
        }
    }
}

void phone_mock_start(void)
{
    xTaskCreatePinnedToCore(mock_task, "phone_mock", 8192, NULL, 5, NULL, 0);
}
