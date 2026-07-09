#include "unity.h"
#include "phone_data.h"
#include "phone.h"
#include "freertos_stub.h"
#include "lvgl.h"       // lv_tick_stub_set
#include <string.h>

// --- helpers ----------------------------------------------------------------

static phone_event_t make_notif(uint32_t id, notif_kind_t kind, const char *sender)
{
    phone_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = PHONE_EVT_NOTIF;
    e.notif.active = true;
    e.notif.id     = id;
    e.notif.kind   = kind;
    strncpy(e.notif.sender, sender, sizeof(e.notif.sender) - 1);
    return e;
}

static phone_event_t make_dismiss(uint32_t id)
{
    phone_event_t e;
    memset(&e, 0, sizeof(e));
    e.type       = PHONE_EVT_NOTIF_DISMISS;
    e.dismiss_id = id;
    return e;
}

static phone_event_t make_media(media_state_t s, const char *artist, const char *title)
{
    phone_event_t e;
    memset(&e, 0, sizeof(e));
    e.type        = PHONE_EVT_MEDIA;
    e.media.state = s;
    strncpy(e.media.artist, artist, sizeof(e.media.artist) - 1);
    strncpy(e.media.title,  title,  sizeof(e.media.title)  - 1);
    return e;
}

static void fresh(void)
{
    freertos_stub_reset();
    lv_tick_stub_set(0);
    phone_data_init();
}

// --- happy path: front slot ------------------------------------------------

static void test_first_notif_becomes_active(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&e);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
    TEST_ASSERT_EQUAL_INT(NOTIF_KIND_SMS, s.notif.kind);
    TEST_ASSERT_EQUAL_STRING("Alice", s.notif.sender);
}

static void test_dismiss_clears_when_queue_empty(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&a);
    phone_event_t d = make_dismiss(1);
    phone_data_apply(&d);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

static void test_dismiss_with_wrong_id_does_nothing(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&a);
    phone_event_t d = make_dismiss(99);     // unknown id
    phone_data_apply(&d);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
}

// --- queue semantics -------------------------------------------------------

static void test_second_notif_queues_behind_active(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_CALL, "Boss");
    phone_data_apply(&a);
    phone_event_t b = make_notif(2, NOTIF_KIND_SMS, "Garage");
    phone_data_apply(&b);

    // Active stays Boss.
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
    TEST_ASSERT_EQUAL_INT(NOTIF_KIND_CALL, s.notif.kind);
}

static void test_dismiss_active_promotes_queue_front(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_CALL, "Boss");
    phone_data_apply(&a);
    phone_event_t b = make_notif(2, NOTIF_KIND_SMS, "Garage");
    phone_data_apply(&b);
    phone_event_t c = make_notif(3, NOTIF_KIND_APP, "WhatsApp");
    phone_data_apply(&c);

    phone_event_t d = make_dismiss(1);   // dismiss active Boss
    phone_data_apply(&d);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(2, s.notif.id);          // Garage promoted
    TEST_ASSERT_EQUAL_INT(NOTIF_KIND_SMS, s.notif.kind);

    // Drain: next dismiss should reveal WhatsApp.
    phone_event_t d2 = make_dismiss(2);
    phone_data_apply(&d2);
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(3, s.notif.id);

    // One more dismiss empties the queue completely.
    phone_event_t d3 = make_dismiss(3);
    phone_data_apply(&d3);
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

static void test_dismiss_queued_entry_removes_from_middle(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_CALL, "A");
    phone_event_t b = make_notif(2, NOTIF_KIND_SMS,  "B");
    phone_event_t c = make_notif(3, NOTIF_KIND_APP,  "C");
    phone_data_apply(&a);
    phone_data_apply(&b);
    phone_data_apply(&c);

    // Dismiss B (currently queued, not active) — late dismiss from the
    // phone side: user dealt with the SMS on the phone itself.
    phone_event_t db = make_dismiss(2);
    phone_data_apply(&db);

    // Now dismissing A should jump straight to C, not B.
    phone_event_t da = make_dismiss(1);
    phone_data_apply(&da);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(3, s.notif.id);
}

static void test_queue_priority_evicts_non_call_when_full(void)
{
    fresh();
    // 1 active + 4 queued = 5 entries; the queue cap is 4, so when the
    // 5th queued lands (a CALL), the oldest non-CALL should be evicted.
    phone_event_t a  = make_notif(1, NOTIF_KIND_CALL, "Active");
    phone_event_t q1 = make_notif(2, NOTIF_KIND_SMS,  "SMS1");
    phone_event_t q2 = make_notif(3, NOTIF_KIND_APP,  "APP1");
    phone_event_t q3 = make_notif(4, NOTIF_KIND_SMS,  "SMS2");
    phone_event_t q4 = make_notif(5, NOTIF_KIND_APP,  "APP2");
    phone_data_apply(&a);
    phone_data_apply(&q1);
    phone_data_apply(&q2);
    phone_data_apply(&q3);
    phone_data_apply(&q4);    // queue now full of non-CALL entries
    phone_event_t pri = make_notif(6, NOTIF_KIND_CALL, "Late call");
    phone_data_apply(&pri);   // should evict q1 (oldest non-CALL) and append

    // Drain and confirm order: Active, q2, q3, q4, pri (no q1).
    uint32_t expected[] = { 1, 3, 4, 5, 6 };
    for (int i = 0; i < 5; i++) {
        phone_state_t s;
        phone_data_get(&s);
        TEST_ASSERT_EQUAL_UINT32(expected[i], s.notif.id);
        phone_event_t dn = make_dismiss(expected[i]);
        phone_data_apply(&dn);
    }
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

static void test_queue_full_of_calls_drops_new_arrival(void)
{
    fresh();
    // Fill with one active + four queued CALLs.
    for (uint32_t id = 1; id <= 5; id++) {
        phone_event_t e = make_notif(id, NOTIF_KIND_CALL, "C");
        phone_data_apply(&e);
    }
    // A non-CALL arrival with the queue saturated must be dropped, not
    // bump a queued CALL.
    phone_event_t sms = make_notif(99, NOTIF_KIND_SMS, "Hi");
    phone_data_apply(&sms);

    // Drain — we should see ids 1..5 only.
    for (uint32_t id = 1; id <= 5; id++) {
        phone_state_t s;
        phone_data_get(&s);
        TEST_ASSERT_EQUAL_UINT32(id, s.notif.id);
        phone_event_t dn = make_dismiss(id);
        phone_data_apply(&dn);
    }
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);   // dropped sms never surfaced
}

// --- call accept / reject / end --------------------------------------------

static void test_call_accept_sets_in_progress_and_start_tick(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&e);
    lv_tick_stub_set(12345);
    phone_data_call_accept();

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.call_in_progress);
    TEST_ASSERT_EQUAL_UINT32(12345, s.notif.call_start_ms);
}

static void test_call_accept_noop_when_no_call(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&e);
    phone_data_call_accept();   // SMS is not a call — must not set flag

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.call_in_progress);
}

static void test_call_accept_is_idempotent(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&e);
    lv_tick_stub_set(1000);
    phone_data_call_accept();
    lv_tick_stub_set(5000);
    phone_data_call_accept();   // second accept must not reset start_ms

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(1000, s.notif.call_start_ms);
}

static void test_call_reject_dismisses_and_promotes(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_event_t b = make_notif(2, NOTIF_KIND_SMS,  "Garage");
    phone_data_apply(&a);
    phone_data_apply(&b);
    phone_data_call_reject();

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(2, s.notif.id);
}

static void test_call_end_only_after_accept(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&a);

    // Call end without accept first is a no-op (no in_progress to end).
    phone_data_call_end();
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);

    // After accept, end dismisses.
    phone_data_call_accept();
    phone_data_call_end();
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

// --- swipe routing ---------------------------------------------------------

static void test_swipe_dismisses_sms(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&e);
    phone_data_handle_swipe(PHONE_SWIPE_LEFT);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

static void test_swipe_does_not_dismiss_call(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&e);
    phone_data_handle_swipe(PHONE_SWIPE_LEFT);
    phone_data_handle_swipe(PHONE_SWIPE_UP);
    phone_data_handle_swipe(PHONE_SWIPE_DOWN);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);    // call must be explicitly handled
}

static void test_swipe_up_shows_media_banner_when_idle(void)
{
    fresh();
    phone_event_t m = make_media(MEDIA_STATE_PLAYING, "Ramones", "Blitzkrieg Bop");
    phone_data_apply(&m);
    phone_data_handle_swipe(PHONE_SWIPE_UP);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.media_banner_shown);
}

static void test_swipe_up_ignored_when_media_stopped(void)
{
    fresh();
    phone_event_t m = make_media(MEDIA_STATE_STOPPED, "", "");
    phone_data_apply(&m);
    phone_data_handle_swipe(PHONE_SWIPE_UP);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.media_banner_shown);
}

static void test_swipe_down_hides_media_banner(void)
{
    fresh();
    phone_event_t m = make_media(MEDIA_STATE_PLAYING, "X", "Y");
    phone_data_apply(&m);
    phone_data_handle_swipe(PHONE_SWIPE_UP);
    phone_data_handle_swipe(PHONE_SWIPE_DOWN);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.media_banner_shown);
}

// --- media interactions ----------------------------------------------------

static void test_media_arrival_does_not_disturb_active_notif(void)
{
    fresh();
    phone_event_t n = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&n);
    phone_event_t m = make_media(MEDIA_STATE_PLAYING, "Ramones", "Blitzkrieg");
    phone_data_apply(&m);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_INT(MEDIA_STATE_PLAYING, s.media.state);
}

static void test_notif_arrival_hides_media_banner(void)
{
    fresh();
    phone_event_t m = make_media(MEDIA_STATE_PLAYING, "X", "Y");
    phone_data_apply(&m);
    phone_data_handle_swipe(PHONE_SWIPE_UP);     // banner shown
    phone_event_t n = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&n);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.media_banner_shown);
}

static void test_media_stop_clears_banner_shown(void)
{
    fresh();
    phone_event_t playing = make_media(MEDIA_STATE_PLAYING, "X", "Y");
    phone_data_apply(&playing);
    phone_data_handle_swipe(PHONE_SWIPE_UP);
    phone_event_t stopped = make_media(MEDIA_STATE_STOPPED, "", "");
    phone_data_apply(&stopped);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.media_banner_shown);
}

// --- contention / lock failure --------------------------------------------

static void test_apply_dropped_when_take_fails(void)
{
    fresh();
    g_stub_take_succeeds = 0;
    phone_event_t e = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&e);

    g_stub_take_succeeds = 1;
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

// Each public entry point must short-circuit cleanly when the mutex
// take fails. Without these the lock-fail paths regress silently.
static void test_get_dropped_when_take_fails(void)
{
    fresh();
    g_stub_take_succeeds = 0;
    phone_state_t s;
    memset(&s, 0x5A, sizeof(s));
    phone_data_get(&s);
    g_stub_take_succeeds = 1;

    uint8_t expected[sizeof(s)];
    memset(expected, 0x5A, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(expected, &s, sizeof(s));
}

static void test_handle_swipe_dropped_when_take_fails(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&e);

    g_stub_take_succeeds = 0;
    phone_data_handle_swipe(PHONE_SWIPE_LEFT);   // would normally dismiss
    g_stub_take_succeeds = 1;

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
}

static void test_call_accept_dropped_when_take_fails(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&e);
    g_stub_take_succeeds = 0;
    phone_data_call_accept();
    g_stub_take_succeeds = 1;

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.call_in_progress);
}

static void test_call_reject_dropped_when_take_fails(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&e);
    g_stub_take_succeeds = 0;
    phone_data_call_reject();
    g_stub_take_succeeds = 1;

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
}

static void test_call_end_dropped_when_take_fails(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&e);
    phone_data_call_accept();
    g_stub_take_succeeds = 0;
    phone_data_call_end();
    g_stub_take_succeeds = 1;

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
}

// Non-CALL notif: call actions are no-ops (must not mutate the kind tag
// or fire dismiss). Covers the kind-check guard in each action.
static void test_call_reject_noop_on_sms(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&e);
    phone_data_call_reject();

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_INT(NOTIF_KIND_SMS, s.notif.kind);
}

static void test_call_end_noop_on_sms(void)
{
    fresh();
    phone_event_t e = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&e);
    phone_data_call_end();

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
}

static void test_call_actions_safe_when_idle(void)
{
    fresh();
    phone_data_call_accept();
    phone_data_call_reject();
    phone_data_call_end();

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

// Horizontal swipe with no notif and no media is a no-op — covers the
// "neither UP nor DOWN" fall-through branch in handle_swipe.
static void test_horizontal_swipe_idle_is_noop(void)
{
    fresh();
    phone_data_handle_swipe(PHONE_SWIPE_LEFT);
    phone_data_handle_swipe(PHONE_SWIPE_RIGHT);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
    TEST_ASSERT_FALSE(s.media_banner_shown);
}

// Stub the wire-format buttons through. They forward to a backend that
// isn't linked yet; this just exercises the function body so it doesn't
// regress to dead code.
static void test_media_action_does_not_crash(void)
{
    fresh();
    phone_data_media_action(PHONE_MEDIA_ACTION_PREV);
    phone_data_media_action(PHONE_MEDIA_ACTION_PLAY_PAUSE);
    phone_data_media_action(PHONE_MEDIA_ACTION_NEXT);
    // Out-of-range cast exercises the switch's no-match exit so the
    // function stays a tested no-op for garbage input instead of dead
    // code in branch-coverage reports.
    phone_data_media_action((phone_media_action_t)0xFF);
}

// Dismiss for an id that isn't active and isn't queued must iterate the
// queue, find nothing, and leave state untouched. Covers the loop's
// no-match exit branch.
static void test_dismiss_unknown_id_with_populated_queue(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_CALL, "A");
    phone_event_t b = make_notif(2, NOTIF_KIND_SMS,  "B");
    phone_data_apply(&a);
    phone_data_apply(&b);
    phone_event_t miss = make_dismiss(999);
    phone_data_apply(&miss);

    // Active still A, queue still has B (next dismiss promotes it).
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
    phone_event_t dn = make_dismiss(1);
    phone_data_apply(&dn);
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(2, s.notif.id);
}

// A wire byte we don't recognise still consumes the event — the switch
// must fall through without touching state. This guards the implicit
// no-op case so adding a new event type can't silently drop existing
// ones.
static void test_unknown_event_type_is_safe_noop(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&a);

    phone_event_t weird;
    memset(&weird, 0, sizeof(weird));
    weird.type = (phone_event_type_t)0xFF;
    phone_data_apply(&weird);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
}

// PAUSED media must keep the banner-shown flag if the user has it open;
// only STOPPED tears it down. Covers the AND/OR sub-branches in
// phone_data_apply (MEDIA case) and phone_data_handle_swipe (UP case).
static void test_paused_media_keeps_banner_shown(void)
{
    fresh();
    phone_event_t playing = make_media(MEDIA_STATE_PLAYING, "X", "Y");
    phone_data_apply(&playing);
    phone_data_handle_swipe(PHONE_SWIPE_UP);

    phone_event_t paused = make_media(MEDIA_STATE_PAUSED, "X", "Y");
    phone_data_apply(&paused);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.media_banner_shown);
}

// DISMISS arriving while nothing is shown (BLE re-delivery after a
// local dismiss, say) must be safe. Covers the first short-circuit of
// the active+id check.
static void test_dismiss_when_idle_is_noop(void)
{
    fresh();
    phone_event_t d = make_dismiss(1);
    phone_data_apply(&d);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

static void test_swipe_up_with_paused_media_shows_banner(void)
{
    fresh();
    phone_event_t paused = make_media(MEDIA_STATE_PAUSED, "X", "Y");
    phone_data_apply(&paused);
    phone_data_handle_swipe(PHONE_SWIPE_UP);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.media_banner_shown);
}

static void test_null_event_safe(void)
{
    fresh();
    phone_data_apply(NULL);     // must not crash, must not take the lock
    TEST_ASSERT_EQUAL_INT(0, g_stub_take_calls);
}

static void test_get_with_null_safe(void)
{
    fresh();
    phone_data_get(NULL);       // must not crash
}

// Must match NOTIF_AUTODISMISS_MS in phone_data.c.
#define AD_MS 8000u

static void test_notif_auto_dismisses_after_timeout(void)
{
    fresh();  // tick = 0
    phone_event_t a = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&a);  // shown at tick 0
    phone_state_t s;

    lv_tick_stub_set(AD_MS - 1);
    phone_data_tick();
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);  // not yet

    lv_tick_stub_set(AD_MS);
    phone_data_tick();
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);  // cluster-cleared
}

static void test_call_never_auto_dismisses(void)
{
    fresh();
    phone_event_t c = make_notif(1, NOTIF_KIND_CALL, "Boss");
    phone_data_apply(&c);
    lv_tick_stub_set(AD_MS * 10);
    phone_data_tick();
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);  // calls are never timed out
}

static void test_auto_dismiss_promotes_queued_and_restarts_clock(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_event_t b = make_notif(2, NOTIF_KIND_APP, "Bob");
    phone_data_apply(&a);  // active id 1 (shown at 0)
    phone_data_apply(&b);  // queued
    phone_state_t s;

    lv_tick_stub_set(AD_MS);
    phone_data_tick();  // id 1 times out -> promote id 2, clock restarts
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(2, s.notif.id);

    lv_tick_stub_set(AD_MS * 2 - 1);
    phone_data_tick();
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);  // id 2 not yet timed out

    lv_tick_stub_set(AD_MS * 2);
    phone_data_tick();
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

static void test_tick_noop_when_idle(void)
{
    fresh();
    lv_tick_stub_set(AD_MS * 5);
    phone_data_tick();  // nothing active -> no-op
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);
}

static void test_tick_dropped_when_take_fails(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&a);
    lv_tick_stub_set(AD_MS);
    g_stub_take_succeeds = 0;
    phone_data_tick();  // lock unavailable -> no dismiss
    g_stub_take_succeeds = 1;
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
}

// PHONE_EVT_CONFIG is cluster config, applied in ble_peripheral; phone_data
// deliberately ignores it. Confirm it neither crashes nor disturbs state.
static void test_config_event_is_noop_in_phone_data(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&a);

    phone_event_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type                 = PHONE_EVT_CONFIG;
    cfg.config.speed_divisor = 188;
    phone_data_apply(&cfg);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
}

// PHONE_EVT_ICON is reassembled in ble_peripheral, not phone_data; confirm it
// is an ignored no-op here (and doesn't disturb the active notification).
static void test_icon_event_is_noop_in_phone_data(void)
{
    fresh();
    phone_event_t a = make_notif(1, NOTIF_KIND_APP, "Alice");
    phone_data_apply(&a);

    phone_event_t icon;
    memset(&icon, 0, sizeof(icon));
    icon.type         = PHONE_EVT_ICON;
    icon.icon.icon_id = 0xABCD;
    phone_data_apply(&icon);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
}

static phone_event_t make_evt(phone_event_type_t type)
{
    phone_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    return e;
}

// CALL_ACTIVE: rider answered on the phone -> banner goes in-progress; a repeat
// keeps the original start tick.
static void test_call_active_from_phone(void)
{
    fresh();
    phone_event_t c = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&c);
    lv_tick_stub_set(4000);
    phone_event_t act = make_evt(PHONE_EVT_CALL_ACTIVE);
    phone_data_apply(&act);
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.call_in_progress);
    TEST_ASSERT_EQUAL_UINT32(4000, s.notif.call_start_ms);

    lv_tick_stub_set(9000);
    phone_data_apply(&act);  // already in progress -> unchanged
    phone_data_get(&s);
    TEST_ASSERT_EQUAL_UINT32(4000, s.notif.call_start_ms);
}

static void test_call_active_ignored_without_call(void)
{
    fresh();
    phone_event_t act = make_evt(PHONE_EVT_CALL_ACTIVE);
    phone_data_apply(&act);  // nothing active
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);

    phone_event_t sms = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&sms);
    phone_data_apply(&act);  // active is SMS, not a call
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.call_in_progress);
}

static void test_call_end_from_phone_promotes_queue(void)
{
    fresh();
    phone_event_t c = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&c);
    phone_event_t q = make_notif(2, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&q);  // queued behind the call
    phone_event_t end = make_evt(PHONE_EVT_CALL_END);
    phone_data_apply(&end);
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(2, s.notif.id);  // SMS promoted
}

static void test_call_end_ignored_when_inapplicable(void)
{
    fresh();
    phone_event_t end = make_evt(PHONE_EVT_CALL_END);
    phone_data_apply(&end);  // nothing active -> no-op
    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_FALSE(s.notif.active);

    phone_event_t sms = make_notif(1, NOTIF_KIND_SMS, "Alice");
    phone_data_apply(&sms);
    phone_data_apply(&end);  // active is SMS, not a call -> no-op
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
}

// A re-posted notification with the same id updates the active one in place,
// preserving call state, instead of queuing a stale duplicate.
static void test_same_id_notif_updates_in_place(void)
{
    fresh();
    phone_event_t c = make_notif(1, NOTIF_KIND_CALL, "Mom");
    phone_data_apply(&c);
    lv_tick_stub_set(5000);
    phone_data_call_accept();  // sets call_in_progress + start tick

    phone_event_t upd = make_notif(1, NOTIF_KIND_CALL, "Mom mobile");
    phone_data_apply(&upd);

    phone_state_t s;
    phone_data_get(&s);
    TEST_ASSERT_TRUE(s.notif.active);
    TEST_ASSERT_EQUAL_UINT32(1, s.notif.id);
    TEST_ASSERT_EQUAL_STRING("Mom mobile", s.notif.sender);  // content updated
    TEST_ASSERT_TRUE(s.notif.call_in_progress);              // call state kept
    TEST_ASSERT_EQUAL_UINT32(5000, s.notif.call_start_ms);
}

void RunTests(void)
{
    RUN_TEST(test_first_notif_becomes_active);
    RUN_TEST(test_dismiss_clears_when_queue_empty);
    RUN_TEST(test_dismiss_with_wrong_id_does_nothing);
    RUN_TEST(test_second_notif_queues_behind_active);
    RUN_TEST(test_dismiss_active_promotes_queue_front);
    RUN_TEST(test_dismiss_queued_entry_removes_from_middle);
    RUN_TEST(test_queue_priority_evicts_non_call_when_full);
    RUN_TEST(test_queue_full_of_calls_drops_new_arrival);
    RUN_TEST(test_call_accept_sets_in_progress_and_start_tick);
    RUN_TEST(test_call_accept_noop_when_no_call);
    RUN_TEST(test_call_accept_is_idempotent);
    RUN_TEST(test_call_reject_dismisses_and_promotes);
    RUN_TEST(test_call_end_only_after_accept);
    RUN_TEST(test_swipe_dismisses_sms);
    RUN_TEST(test_swipe_does_not_dismiss_call);
    RUN_TEST(test_swipe_up_shows_media_banner_when_idle);
    RUN_TEST(test_swipe_up_ignored_when_media_stopped);
    RUN_TEST(test_swipe_down_hides_media_banner);
    RUN_TEST(test_media_arrival_does_not_disturb_active_notif);
    RUN_TEST(test_notif_arrival_hides_media_banner);
    RUN_TEST(test_media_stop_clears_banner_shown);
    RUN_TEST(test_apply_dropped_when_take_fails);
    RUN_TEST(test_get_dropped_when_take_fails);
    RUN_TEST(test_handle_swipe_dropped_when_take_fails);
    RUN_TEST(test_call_accept_dropped_when_take_fails);
    RUN_TEST(test_call_reject_dropped_when_take_fails);
    RUN_TEST(test_call_end_dropped_when_take_fails);
    RUN_TEST(test_call_reject_noop_on_sms);
    RUN_TEST(test_call_end_noop_on_sms);
    RUN_TEST(test_call_actions_safe_when_idle);
    RUN_TEST(test_horizontal_swipe_idle_is_noop);
    RUN_TEST(test_media_action_does_not_crash);
    RUN_TEST(test_dismiss_unknown_id_with_populated_queue);
    RUN_TEST(test_unknown_event_type_is_safe_noop);
    RUN_TEST(test_config_event_is_noop_in_phone_data);
    RUN_TEST(test_icon_event_is_noop_in_phone_data);
    RUN_TEST(test_call_active_from_phone);
    RUN_TEST(test_call_active_ignored_without_call);
    RUN_TEST(test_call_end_from_phone_promotes_queue);
    RUN_TEST(test_call_end_ignored_when_inapplicable);
    RUN_TEST(test_same_id_notif_updates_in_place);
    RUN_TEST(test_paused_media_keeps_banner_shown);
    RUN_TEST(test_dismiss_when_idle_is_noop);
    RUN_TEST(test_swipe_up_with_paused_media_shows_banner);
    RUN_TEST(test_null_event_safe);
    RUN_TEST(test_get_with_null_safe);
    RUN_TEST(test_notif_auto_dismisses_after_timeout);
    RUN_TEST(test_call_never_auto_dismisses);
    RUN_TEST(test_auto_dismiss_promotes_queued_and_restarts_clock);
    RUN_TEST(test_tick_noop_when_idle);
    RUN_TEST(test_tick_dropped_when_take_fails);
}
