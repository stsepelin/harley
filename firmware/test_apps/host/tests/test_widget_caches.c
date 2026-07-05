// Regression tests for the skip-if-unchanged caches added to every label-
// based widget. The contract under test is: if the *displayed* value is
// the same as last frame, the widget must make ZERO LVGL setter calls.
//
// Catches the failure mode where a future refactor silently removes a
// cache (no visual difference; the gauge just gets choppier under load).

#include "unity.h"
#include "lvgl_stub.h"
#include "vehicle_data.h"

#include "speed_display.h"
#include "gear_indicator.h"
#include "fuel_arc.h"
#include "temp_display.h"
#include "theme.h"
#include "turn_signals.h"
#include "clock_display.h"
#include "odometer_display.h"
#include "trip_display.h"
#include "warning_lights.h"
#include "notification_banner.h"
#include "media_banner.h"
#include "now_playing_display.h"

#include <string.h>

#include "esp_heap_caps.h"             // heap_caps_stub_fail_next
void lv_tick_stub_set(uint32_t tick);  // test hook from lvgl_tick_stub

// Number of repeated identical updates we run after priming the cache. If
// any setter fires inside this loop, the cache is broken.
#define REPEAT  100

// --- speed_display ---------------------------------------------------------

static void test_speed_cache_skips_unchanged(void)
{
    lv_obj_t *w = speed_display_create(NULL);
    speed_display_set_value(w, 50, UNITS_KPH);          // prime
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) speed_display_set_value(w, 50, UNITS_KPH);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_lv_label_set_text_calls,
        "identical speed must not re-render");
}

static void test_speed_cache_fires_on_change(void)
{
    lv_obj_t *w = speed_display_create(NULL);
    speed_display_set_value(w, 50, UNITS_KPH);
    lv_stub_reset();
    speed_display_set_value(w, 51, UNITS_KPH);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// Switching the displayed unit must repaint both the value digits and
// the "km/h" / "mph" subtitle even when the underlying mph value is
// identical — same input, different output.
static void test_speed_cache_fires_on_units_change(void)
{
    lv_obj_t *w = speed_display_create(NULL);
    speed_display_set_value(w, 40, UNITS_KPH);  // 40 mph shown as 64 km/h
    lv_stub_reset();
    speed_display_set_value(w, 40, UNITS_MPH);  // same 40 mph, shown as 40
    // 64 -> 40: both digit slots flip (6->4, 4->0) plus the subtitle,
    // so three per-digit label writes.
    TEST_ASSERT_EQUAL_INT(3, g_lv_label_set_text_calls);
}

// Shrinking digit count must hide the now-empty slot, and growing back must
// re-show it: 100 -> 99 leaves the leading slot blank (2 digit rewrites),
// 99 -> 100 repaints all three. Uses mph (the canonical unit) so the shown
// value equals the input with no conversion in the way.
static void test_speed_digit_count_shrink_and_grow(void)
{
    lv_obj_t *w = speed_display_create(NULL);
    speed_display_set_value(w, 100, UNITS_MPH);
    lv_stub_reset();
    speed_display_set_value(w, 99, UNITS_MPH);
    TEST_ASSERT_EQUAL_INT(2, g_lv_label_set_text_calls);  // slots: hide, '9', '9'
    lv_stub_reset();
    speed_display_set_value(w, 100, UNITS_MPH);
    TEST_ASSERT_EQUAL_INT(3, g_lv_label_set_text_calls);  // '1' re-shown, '0', '0'
}

// Garbage 4-digit readings peg at 999 rather than truncating to a confident
// wrong leading-3-digits number. Observable without string capture: primed
// with 999, a pegged 1024 renders the SAME digits, so zero label writes.
static void test_speed_pegs_at_999(void)
{
    lv_obj_t *w = speed_display_create(NULL);
    speed_display_set_value(w, 999, UNITS_KPH);
    lv_stub_reset();
    speed_display_set_value(w, 1024, UNITS_KPH);  // pegged -> still "999"
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

// Every setter must survive an object that carries no widget data (the
// `!sd`-style guards) without crashing or painting.
static void test_setters_guard_null_user_data(void)
{
    lv_obj_t      *bare = lv_obj_create(NULL);
    vehicle_data_t d    = {0};
    lv_stub_reset();
    speed_display_set_value(bare, 50, UNITS_KPH);
    gear_indicator_set(bare, GEAR_1);
    gear_indicator_set_warning(bare, true);
    fuel_arc_set_level(bare, 3);
    temp_display_set_value(bare, 90);
    turn_signals_set(bare, true, true);
    clock_display_set(bare, 8, 24);
    odometer_display_set(bare, 1000, UNITS_KPH);
    trip_display_set(bare, 1000, UNITS_KPH);
    warning_lights_update(bare, &d);
    notification_banner_update(bare, NULL);
    media_banner_update(bare, NULL, true);
    now_playing_display_set(bare, NULL);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_invalidate_calls);

    // And valid widgets must reject NULL payloads the same way.
    lv_obj_t *nb = notification_banner_create(NULL, NULL);
    lv_obj_t *mb = media_banner_create(NULL, NULL);
    lv_obj_t *np = now_playing_display_create(NULL);
    lv_stub_reset();
    notification_banner_update(nb, NULL);
    media_banner_update(mb, NULL, true);
    now_playing_display_set(np, NULL);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

// --- gear_indicator --------------------------------------------------------

// MUST be the first test that creates a gear indicator: the baked outline is
// a one-shot static, so the alloc-failure path is only reachable on the very
// first build attempt. A later create retries and succeeds.
static void test_gear_outline_alloc_fail_degrades(void)
{
    heap_caps_stub_fail_next(1);
    lv_obj_t *w = gear_indicator_create(NULL);  // outline bake fails, widget survives
    lv_stub_reset();
    gear_indicator_set(w, GEAR_2);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);  // digit still works
}

static void test_gear_cache_skips_unchanged(void)
{
    lv_obj_t *w = gear_indicator_create(NULL);
    gear_indicator_set(w, GEAR_3);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) gear_indicator_set(w, GEAR_3);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_gear_cache_fires_on_change(void)
{
    lv_obj_t *w = gear_indicator_create(NULL);
    gear_indicator_set(w, GEAR_3);
    lv_stub_reset();
    gear_indicator_set(w, GEAR_4);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// Every gear value renders with exactly one label write — sweeps the whole
// text table including the "-" fallback for an out-of-range value.
static void test_gear_full_table(void)
{
    static const gear_t seq[] = {GEAR_1, GEAR_2, GEAR_3,       GEAR_4,
                                 GEAR_5, GEAR_6, GEAR_NEUTRAL, (gear_t)99};
    lv_obj_t           *w     = gear_indicator_create(NULL);
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        lv_stub_reset();
        gear_indicator_set(w, seq[i]);
        TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
    }
}

// Neutral renders orange, in-gear renders white; the colour rides along
// with every gear change (apply_color), so assert the value, not the count.
static void test_gear_neutral_is_orange(void)
{
    lv_obj_t *w = gear_indicator_create(NULL);
    gear_indicator_set(w, GEAR_2);
    TEST_ASSERT_EQUAL_HEX32(VROD_TEXT, g_lv_last_text_color);
    gear_indicator_set(w, GEAR_NEUTRAL);
    TEST_ASSERT_EQUAL_HEX32(VROD_ORANGE, g_lv_last_text_color);
}

// The blink timer alternates the digit between warning red and the gear's
// base colour on every tick.
static void test_gear_blink_timer_toggles_color(void)
{
    lv_obj_t *w = gear_indicator_create(NULL);
    gear_indicator_set(w, GEAR_3);
    gear_indicator_set_warning(w, true);
    TEST_ASSERT_EQUAL_HEX32(VROD_RED_BRIGHT, g_lv_last_text_color);
    lv_timer_stub_fire_all();  // blink_red -> false
    TEST_ASSERT_EQUAL_HEX32(VROD_TEXT, g_lv_last_text_color);
    lv_timer_stub_fire_all();  // back to red
    TEST_ASSERT_EQUAL_HEX32(VROD_RED_BRIGHT, g_lv_last_text_color);
    gear_indicator_set_warning(w, false);  // stops + restores base
    TEST_ASSERT_EQUAL_HEX32(VROD_TEXT, g_lv_last_text_color);
}

// Warning blink toggles the gear digit's text colour. Same-state calls
// must not invalidate; a real edge fires exactly one set_style_text_color.
static void test_gear_warning_cache_skips_unchanged(void)
{
    lv_obj_t *w = gear_indicator_create(NULL);
    gear_indicator_set_warning(w, true);   // prime active
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) gear_indicator_set_warning(w, true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_lv_obj_set_style_text_color_calls,
        "repeated set_warning(true) must not re-paint");
}

static void test_gear_warning_fires_on_edge(void)
{
    lv_obj_t *w = gear_indicator_create(NULL);
    // Fresh widget starts inactive — calling set_warning(false) is a no-op.
    lv_stub_reset();
    gear_indicator_set_warning(w, true);
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);
    lv_stub_reset();
    gear_indicator_set_warning(w, false);
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);
}

// --- temp_display ----------------------------------------------------------
// Two independent caches: the value label updates whenever celsius changes;
// the colour pair (icon + value) only re-applies when the hot/cold state
// flips. Bumping by 1 °C below the 110 °C threshold is a text update only.

static void test_temp_cache_skips_unchanged(void)
{
    lv_obj_t *w = temp_display_create(NULL);
    temp_display_set_value(w, 92);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) temp_display_set_value(w, 92);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_temp_cache_value_change_no_color(void)
{
    // Below threshold, ticking up by 1 °C: text re-renders, colour stays.
    lv_obj_t *w = temp_display_create(NULL);
    temp_display_set_value(w, 92);
    lv_stub_reset();
    temp_display_set_value(w, 93);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_temp_cache_hot_transition(void)
{
    // Crossing the 110 °C threshold flips both labels' colours plus
    // re-renders the text. Going back down flips colours again.
    lv_obj_t *w = temp_display_create(NULL);
    temp_display_set_value(w, 109);
    lv_stub_reset();
    temp_display_set_value(w, 110);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(2, g_lv_obj_set_style_text_color_calls);  // icon + value
    lv_stub_reset();
    temp_display_set_value(w, 109);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(2, g_lv_obj_set_style_text_color_calls);
}

// --- turn_signals ----------------------------------------------------------
// Only recolours the side(s) that actually flipped.

static void test_turn_signals_cache_skips_unchanged(void)
{
    lv_obj_t *w = turn_signals_create(NULL);
    turn_signals_set(w, false, false);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) turn_signals_set(w, false, false);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_turn_signals_only_changed_side_repaints(void)
{
    lv_obj_t *w = turn_signals_create(NULL);
    turn_signals_set(w, false, false);
    lv_stub_reset();
    turn_signals_set(w, true, false);   // only left flipped
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);
}

// --- fuel_arc ---------------------------------------------------------------
// The BMW fuel arc redraws by re-baking its ARGB strip and invalidating the
// image once — far more work per update than the old segmented fuel_bar, so
// a lost cache here costs a full 376x84 SDF re-bake every frame.

static void test_fuel_cache_skips_unchanged(void)
{
    lv_obj_t *w = fuel_arc_create(NULL);
    fuel_arc_set_level(w, 4);  // prime
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++)
        fuel_arc_set_level(w, 4);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_lv_obj_invalidate_calls,
                                  "identical fuel level must not re-bake the strip");
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_fuel_cache_fires_on_change(void)
{
    lv_obj_t *w = fuel_arc_create(NULL);
    fuel_arc_set_level(w, 4);
    lv_stub_reset();
    fuel_arc_set_level(w, 3);
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_invalidate_calls);
}

static void test_fuel_cache_clamps_overrange(void)
{
    // Levels above the J1850 0..6 range clamp to 6 — and a clamped repeat
    // must hit the cache, not re-bake.
    lv_obj_t *w = fuel_arc_create(NULL);
    fuel_arc_set_level(w, 6);
    lv_stub_reset();
    fuel_arc_set_level(w, 7);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_invalidate_calls);
}

// Low fuel actually bakes the red band (caught by the CI gate: every other
// test uses level >= 3, so the red arm of the fill colour never ran), and
// critically-low turns the pump icon red.
static void test_fuel_low_level_renders_red(void)
{
    lv_obj_t *w = fuel_arc_create(NULL);
    fuel_arc_set_level(w, 4);
    lv_stub_reset();
    fuel_arc_set_level(w, 2);  // low: band rebakes red
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_invalidate_calls);
    fuel_arc_set_level(w, 1);  // critically low: icon follows
    TEST_ASSERT_EQUAL_HEX32(VROD_RED_BRIGHT, g_lv_last_text_color);
}

// Strip-buffer allocation failure: the widget must degrade to icon-only
// (no bake, no invalidate) instead of crashing.
static void test_fuel_alloc_fail_degrades(void)
{
    heap_caps_stub_fail_next(1);
    lv_obj_t *w = fuel_arc_create(NULL);
    lv_stub_reset();
    fuel_arc_set_level(w, 1);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_invalidate_calls);
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);  // icon still recolours
}

// --- notification_banner -----------------------------------------------------
// Runs every frame while a notification is up, so its per-field caches are
// the difference between one repaint per change and a repaint per frame.

static notification_t make_sms(void)
{
    notification_t n = {0};
    n.active         = true;
    n.id             = 1;
    n.kind           = NOTIF_KIND_SMS;
    strcpy(n.sender, "ALICE");
    strcpy(n.message, "see you at six");
    return n;
}

static void test_notif_banner_inactive_is_quiet(void)
{
    lv_obj_t      *w = notification_banner_create(NULL, NULL);
    notification_t n = {0};
    notification_banner_update(w, &n);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++)
        notification_banner_update(w, &n);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_notif_banner_cache_skips_unchanged(void)
{
    lv_obj_t      *w = notification_banner_create(NULL, NULL);
    notification_t n = make_sms();
    notification_banner_update(w, &n);  // prime
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++)
        notification_banner_update(w, &n);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_notif_banner_message_change_repaints_once(void)
{
    lv_obj_t      *w = notification_banner_create(NULL, NULL);
    notification_t n = make_sms();
    notification_banner_update(w, &n);
    lv_stub_reset();
    strcpy(n.message, "actually make it seven");
    notification_banner_update(w, &n);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);  // message label only
}

// Active-call timer: same elapsed second must stay quiet, the next second
// repaints the MM:SS label exactly once.
static void test_notif_banner_call_timer_per_second(void)
{
    lv_obj_t *w = notification_banner_create(NULL, NULL);
    lv_tick_stub_set(10000);
    notification_t n   = make_sms();
    n.kind             = NOTIF_KIND_CALL;
    n.call_in_progress = true;
    n.call_start_ms    = 10000;
    notification_banner_update(w, &n);  // prime: IN CALL mode, 0:00
    lv_stub_reset();
    lv_tick_stub_set(10500);  // same elapsed second
    for (int i = 0; i < REPEAT; i++)
        notification_banner_update(w, &n);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
    lv_tick_stub_set(11000);  // 1 s elapsed
    notification_banner_update(w, &n);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

static void test_notif_banner_dismiss_hides_and_goes_quiet(void)
{
    lv_obj_t      *w = notification_banner_create(NULL, NULL);
    notification_t n = make_sms();
    notification_banner_update(w, &n);
    n.active = false;
    lv_stub_reset();
    notification_banner_update(w, &n);  // hide edge
    for (int i = 0; i < REPEAT; i++)
        notification_banner_update(w, &n);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

// Kind tag text + colour per kind, including the incoming-call button row
// and the in-call green tag.
static void test_notif_banner_kind_variants(void)
{
    lv_obj_t      *w = notification_banner_create(NULL, NULL);
    notification_t n = make_sms();
    notification_banner_update(w, &n);  // SMS -> orange tag

    n.kind = NOTIF_KIND_APP;
    lv_stub_reset();
    notification_banner_update(w, &n);  // MSG -> dim tag
    TEST_ASSERT_EQUAL_HEX32(VROD_TEXT_DIM, g_lv_last_text_color);

    n.kind = NOTIF_KIND_CALL;  // incoming call
    lv_stub_reset();
    notification_banner_update(w, &n);
    TEST_ASSERT_EQUAL_HEX32(VROD_GREEN_SIGNAL, g_lv_last_text_color);

    n.call_in_progress = true;  // accepted
    lv_tick_stub_set(0);
    n.call_start_ms = 0;
    lv_stub_reset();
    notification_banner_update(w, &n);  // IN CALL tag + timer
    TEST_ASSERT_EQUAL_HEX32(VROD_GREEN_SIGNAL, g_lv_last_text_color);
}

// INFO banner height follows the wrapped message height between the MIN and
// MAX clamps; an unchanged resulting height must not relayout.
static void test_notif_banner_info_height_clamps(void)
{
    lv_obj_t      *w = notification_banner_create(NULL, NULL);
    notification_t n = make_sms();

    g_lv_stub_obj_height = 30;  // 1-2 lines: within range
    notification_banner_update(w, &n);

    strcpy(n.message, "a much longer message that needs more lines");
    g_lv_stub_obj_height = 200;  // wrapped taller than MAX -> clamps
    notification_banner_update(w, &n);

    strcpy(n.message, "x");
    g_lv_stub_obj_height = 5;  // shorter than MIN -> clamps
    notification_banner_update(w, &n);

    strcpy(n.message, "same-height edge");
    g_lv_stub_obj_height = 175;  // want clamps to MAX == reported height: no set
    notification_banner_update(w, &n);
}

// The call buttons must route their actions through the registered callback.
static call_action_t s_actions[8];
static int           s_action_count = 0;
static void          capture_call_action(call_action_t a)
{
    if (s_action_count < 8)
        s_actions[s_action_count++] = a;
}

static void test_notif_banner_buttons_route_actions(void)
{
    lv_obj_t *w = notification_banner_create(NULL, capture_call_action);
    (void)w;
    s_action_count = 0;
    lv_event_stub_click_all();  // earlier banners have NULL callbacks -> no-ops
    TEST_ASSERT_EQUAL_INT(3, s_action_count);
    TEST_ASSERT_EQUAL_INT(CALL_ACTION_REJECT, s_actions[0]);
    TEST_ASSERT_EQUAL_INT(CALL_ACTION_ACCEPT, s_actions[1]);
    TEST_ASSERT_EQUAL_INT(CALL_ACTION_END, s_actions[2]);
}

// --- media_banner ------------------------------------------------------------

static now_playing_t make_track(void)
{
    now_playing_t np = {0};
    np.state         = MEDIA_STATE_PLAYING;
    strcpy(np.artist, "Motorhead");
    strcpy(np.title, "Overkill");
    return np;
}

static void test_media_banner_hidden_is_quiet(void)
{
    lv_obj_t     *w  = media_banner_create(NULL, NULL);
    now_playing_t np = make_track();
    media_banner_update(w, &np, false);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++)
        media_banner_update(w, &np, false);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);

    // Show then dismiss: the hide edge must also go quiet afterwards.
    media_banner_update(w, &np, true);
    media_banner_update(w, &np, false);
    lv_stub_reset();
    media_banner_update(w, &np, false);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_media_banner_cache_skips_unchanged(void)
{
    lv_obj_t     *w  = media_banner_create(NULL, NULL);
    now_playing_t np = make_track();
    media_banner_update(w, &np, true);  // prime
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++)
        media_banner_update(w, &np, true);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_media_banner_track_and_state_repaint(void)
{
    lv_obj_t     *w  = media_banner_create(NULL, NULL);
    now_playing_t np = make_track();
    media_banner_update(w, &np, true);
    lv_stub_reset();
    strcpy(np.title, "Bomber");
    media_banner_update(w, &np, true);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);  // title only
    lv_stub_reset();
    np.state = MEDIA_STATE_PAUSED;
    media_banner_update(w, &np, true);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);  // play/pause icon only
}

// Empty metadata renders the "(unknown ...)" fallbacks. The caches start
// empty so the first all-empty update only flips the play/pause icon; the
// fallbacks fire when a real value is replaced by an empty one.
static void test_media_banner_unknown_field_fallbacks(void)
{
    lv_obj_t     *w  = media_banner_create(NULL, NULL);
    now_playing_t np = {0};
    np.state         = MEDIA_STATE_PLAYING;
    lv_stub_reset();
    media_banner_update(w, &np, true);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);  // icon only

    strcpy(np.title, "t");
    strcpy(np.artist, "a");
    media_banner_update(w, &np, true);
    np.title[0] = np.artist[0] = '\0';  // metadata dropped
    lv_stub_reset();
    media_banner_update(w, &np, true);
    TEST_ASSERT_EQUAL_INT(2, g_lv_label_set_text_calls);  // both fallbacks
}

static media_action_t s_media_actions[8];
static int            s_media_action_count = 0;
static void           capture_media_action(media_action_t a)
{
    if (s_media_action_count < 8)
        s_media_actions[s_media_action_count++] = a;
}

static void test_media_banner_buttons_route_actions(void)
{
    lv_obj_t *w = media_banner_create(NULL, capture_media_action);
    (void)w;
    s_media_action_count = 0;
    s_action_count       = 0;  // the click sweep also hits earlier call banners
    lv_event_stub_click_all();
    TEST_ASSERT_EQUAL_INT(3, s_media_action_count);
    TEST_ASSERT_EQUAL_INT(MEDIA_ACTION_PREV, s_media_actions[0]);
    TEST_ASSERT_EQUAL_INT(MEDIA_ACTION_PLAY_PAUSE, s_media_actions[1]);
    TEST_ASSERT_EQUAL_INT(MEDIA_ACTION_NEXT, s_media_actions[2]);
}

// --- now_playing_display -----------------------------------------------------

static void test_now_playing_cache_skips_unchanged(void)
{
    lv_obj_t     *w  = now_playing_display_create(NULL);
    now_playing_t np = make_track();
    now_playing_display_set(w, &np);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++)
        now_playing_display_set(w, &np);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_now_playing_fires_on_change(void)
{
    lv_obj_t     *w  = now_playing_display_create(NULL);
    now_playing_t np = make_track();
    now_playing_display_set(w, &np);
    lv_stub_reset();
    strcpy(np.title, "Ace of Spades");
    now_playing_display_set(w, &np);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
    lv_stub_reset();
    strcpy(np.artist, "Hawkwind");  // artist-only change repaints too
    now_playing_display_set(w, &np);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// format_line fallbacks: title-only, artist-only, and the "(playing)"
// placeholder when both fields are empty.
static void test_now_playing_partial_metadata(void)
{
    lv_obj_t     *w  = now_playing_display_create(NULL);
    now_playing_t np = {0};
    strcpy(np.title, "Overkill");  // title only
    lv_stub_reset();
    now_playing_display_set(w, &np);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);

    np.title[0] = '\0';
    strcpy(np.artist, "Motorhead");  // artist only
    now_playing_display_set(w, &np);

    np.artist[0] = '\0';  // neither -> "(playing)"
    lv_stub_reset();
    now_playing_display_set(w, &np);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// --- clock_display ---------------------------------------------------------

static void test_clock_cache_skips_unchanged(void)
{
    lv_obj_t *w = clock_display_create(NULL);
    clock_display_set(w, 8, 24);
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) clock_display_set(w, 8, 24);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_clock_cache_fires_on_minute_change(void)
{
    lv_obj_t *w = clock_display_create(NULL);
    clock_display_set(w, 8, 24);
    lv_stub_reset();
    clock_display_set(w, 8, 25);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// --- odometer_display ------------------------------------------------------
// Cache key is km (meters / 1000) — sub-km changes must not re-render.

static void test_odometer_cache_skips_sub_km(void)
{
    lv_obj_t *w = odometer_display_create(NULL);
    odometer_display_set(w, 12847000, UNITS_KPH);   // 12,847 km
    lv_stub_reset();
    // Same km value reached via several sub-km updates — must stay quiet.
    for (uint32_t m = 12847001; m < 12847000 + 1000; m += 7) {
        odometer_display_set(w, m, UNITS_KPH);
    }
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_odometer_cache_fires_on_km_change(void)
{
    lv_obj_t *w = odometer_display_create(NULL);
    odometer_display_set(w, 12847000, UNITS_KPH);
    lv_stub_reset();
    odometer_display_set(w, 12848000, UNITS_KPH);   // next km
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

static void test_odometer_cache_fires_on_units_change(void)
{
    lv_obj_t *w = odometer_display_create(NULL);
    odometer_display_set(w, 12847000, UNITS_KPH);
    lv_stub_reset();
    odometer_display_set(w, 12847000, UNITS_MPH);   // same metres, different unit
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// 0 km == 0 mi: the displayed value matches across a units switch, so the
// units term of the cache key must trigger the repaint (the "mi" suffix
// changes even though the number doesn't).
static void test_odometer_units_change_with_equal_value(void)
{
    lv_obj_t *w = odometer_display_create(NULL);
    odometer_display_set(w, 0, UNITS_KPH);
    lv_stub_reset();
    odometer_display_set(w, 0, UNITS_MPH);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// --- trip_display ----------------------------------------------------------
// Cache key is tenth-of-km. Changes within 100 m must stay quiet.

static void test_trip_cache_skips_sub_tenth_km(void)
{
    lv_obj_t *w = trip_display_create(NULL, "TRIP1");
    trip_display_set(w, 1234, UNITS_KPH);            // 1.2 km
    lv_stub_reset();
    for (uint32_t m = 1235; m < 1300; m++) trip_display_set(w, m, UNITS_KPH);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
}

static void test_trip_cache_fires_on_tenth_change(void)
{
    lv_obj_t *w = trip_display_create(NULL, "TRIP1");
    trip_display_set(w, 1234, UNITS_KPH);            // 1.2 km
    lv_stub_reset();
    trip_display_set(w, 1300, UNITS_KPH);            // 1.3 km
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

static void test_trip_cache_fires_on_units_change(void)
{
    lv_obj_t *w = trip_display_create(NULL, "TRIP1");
    trip_display_set(w, 1234, UNITS_KPH);
    lv_stub_reset();
    trip_display_set(w, 1234, UNITS_MPH);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// Same as the odometer: 0.0 km == 0.0 mi, only the suffix differs.
static void test_trip_units_change_with_equal_value(void)
{
    lv_obj_t *w = trip_display_create(NULL, "TRIP1");
    trip_display_set(w, 0, UNITS_KPH);
    lv_stub_reset();
    trip_display_set(w, 0, UNITS_MPH);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// --- warning_lights --------------------------------------------------------
// Per-lamp icon+colour cache. Idle (all off) → no work. Flipping one lamp
// fires set_text + set_style_text_color exactly once (one lamp painted).

static lv_obj_t *make_warn_chevron(void)
{
    static const lamp_id_t ids[] = { LAMP_OIL, LAMP_ENGINE, LAMP_BATTERY };
    return warning_lights_create(NULL, ids, 3, WARN_LAYOUT_CHEVRON);
}

static void test_warning_lights_cache_idle(void)
{
    lv_obj_t *w = make_warn_chevron();
    vehicle_data_t d = {0};
    warning_lights_update(w, &d);        // prime
    lv_stub_reset();
    for (int i = 0; i < REPEAT; i++) warning_lights_update(w, &d);
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(0, g_lv_obj_set_style_text_color_calls);
}

static void test_warning_lights_only_changed_lamp_repaints(void)
{
    lv_obj_t *w = make_warn_chevron();
    vehicle_data_t d = {0};
    warning_lights_update(w, &d);
    lv_stub_reset();

    d.oil_pressure_warning = true;
    warning_lights_update(w, &d);
    // lamp_apply_appearance writes icon AND colour, but only for the
    // changed lamp.
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);
}

// Mixed-field updates: each cache key alone must trigger exactly one repaint.
static void test_clock_fires_on_hour_change(void)
{
    lv_obj_t *w = clock_display_create(NULL);
    clock_display_set(w, 8, 24);
    lv_stub_reset();
    clock_display_set(w, 9, 24);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

static void test_turn_signals_right_side_and_both(void)
{
    lv_obj_t *w = turn_signals_create(NULL);
    turn_signals_set(w, false, false);
    lv_stub_reset();
    turn_signals_set(w, false, true);  // right only
    TEST_ASSERT_EQUAL_INT(1, g_lv_obj_set_style_text_color_calls);
    lv_stub_reset();
    turn_signals_set(w, true, false);  // both flip at once
    TEST_ASSERT_EQUAL_INT(2, g_lv_obj_set_style_text_color_calls);
}

// --- warning_lights: layouts, lamp table, beam rotation ----------------------

static const lamp_id_t k_all_lamps[] = {
    LAMP_OIL,         LAMP_ENGINE,   LAMP_ABS,       LAMP_BATTERY,
    LAMP_IMMOBILISER, LAMP_LOW_BEAM, LAMP_HIGH_BEAM, LAMP_BEAM,
};

// Every lamp's vehicle_data mapping fires exactly one repaint per toggle.
static void test_warning_lights_full_lamp_table(void)
{
    lv_obj_t      *w = warning_lights_create(NULL, k_all_lamps, 8, WARN_LAYOUT_ROW);
    vehicle_data_t d = {0};
    warning_lights_update(w, &d);
    lv_stub_reset();
    d.oil_pressure_warning = true;
    d.check_engine         = true;
    d.abs_warning          = true;
    d.battery_warning      = true;
    d.immobiliser_warning  = true;
    d.low_beam             = true;  // lights LOW_BEAM and the BEAM slot (showing low)
    d.high_beam            = true;
    warning_lights_update(w, &d);
    TEST_ASSERT_EQUAL_INT(8, g_lv_label_set_text_calls);
}

// The virtual beam slot swaps its icon every BEAM_ROTATE_FRAMES updates and
// follows the matching beam state.
static void test_warning_lights_beam_slot_rotates(void)
{
    static const lamp_id_t beam_only[] = {LAMP_BEAM};
    lv_obj_t              *w = warning_lights_create(NULL, beam_only, 1, WARN_LAYOUT_COLUMN);
    vehicle_data_t         d = {0};
    d.high_beam              = true;  // high on, low off
    warning_lights_update(w, &d);     // tick 1: shows LOW icon, off colour
    lv_stub_reset();
    for (int i = 0; i < 148; i++)
        warning_lights_update(w, &d);                     // ticks 2..149
    TEST_ASSERT_EQUAL_INT(0, g_lv_label_set_text_calls);  // not rotated yet
    warning_lights_update(w, &d);                         // tick 150: flips to HIGH icon, lit
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);
}

// Chevron demands exactly 3 lamps; other counts fall back to a column.
// Over-long id lists clamp to the lamp table size.
static void test_warning_lights_layout_fallbacks(void)
{
    static const lamp_id_t two[] = {LAMP_OIL, LAMP_ABS};
    lv_obj_t              *w     = warning_lights_create(NULL, two, 2, WARN_LAYOUT_CHEVRON);
    vehicle_data_t         d     = {0};
    d.oil_pressure_warning       = true;
    lv_stub_reset();
    warning_lights_update(w, &d);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);  // still functional as a column

    static const lamp_id_t nine[] = {LAMP_OIL,       LAMP_ENGINE,      LAMP_ABS,
                                     LAMP_BATTERY,   LAMP_IMMOBILISER, LAMP_LOW_BEAM,
                                     LAMP_HIGH_BEAM, LAMP_BEAM,        LAMP_OIL};
    lv_obj_t *w9 = warning_lights_create(NULL, nine, 9, WARN_LAYOUT_ROW);  // clamps to 8
    lv_stub_reset();
    warning_lights_update(w9, &d);
    TEST_ASSERT_EQUAL_INT(1, g_lv_label_set_text_calls);  // only the oil lamp lit
}

void RunTests(void)
{
    RUN_TEST(test_speed_cache_skips_unchanged);
    RUN_TEST(test_speed_cache_fires_on_change);
    RUN_TEST(test_speed_cache_fires_on_units_change);
    RUN_TEST(test_speed_pegs_at_999);
    RUN_TEST(test_speed_digit_count_shrink_and_grow);
    RUN_TEST(test_setters_guard_null_user_data);
    RUN_TEST(test_gear_outline_alloc_fail_degrades);  // must precede other gear tests
    RUN_TEST(test_gear_cache_skips_unchanged);
    RUN_TEST(test_gear_cache_fires_on_change);
    RUN_TEST(test_gear_full_table);
    RUN_TEST(test_gear_neutral_is_orange);
    RUN_TEST(test_gear_blink_timer_toggles_color);
    RUN_TEST(test_gear_warning_cache_skips_unchanged);
    RUN_TEST(test_gear_warning_fires_on_edge);
    RUN_TEST(test_temp_cache_skips_unchanged);
    RUN_TEST(test_temp_cache_value_change_no_color);
    RUN_TEST(test_temp_cache_hot_transition);
    RUN_TEST(test_turn_signals_cache_skips_unchanged);
    RUN_TEST(test_turn_signals_only_changed_side_repaints);
    RUN_TEST(test_fuel_cache_skips_unchanged);
    RUN_TEST(test_fuel_cache_fires_on_change);
    RUN_TEST(test_fuel_cache_clamps_overrange);
    RUN_TEST(test_fuel_low_level_renders_red);
    RUN_TEST(test_fuel_alloc_fail_degrades);
    RUN_TEST(test_notif_banner_inactive_is_quiet);
    RUN_TEST(test_notif_banner_cache_skips_unchanged);
    RUN_TEST(test_notif_banner_message_change_repaints_once);
    RUN_TEST(test_notif_banner_call_timer_per_second);
    RUN_TEST(test_notif_banner_dismiss_hides_and_goes_quiet);
    RUN_TEST(test_notif_banner_kind_variants);
    RUN_TEST(test_notif_banner_info_height_clamps);
    RUN_TEST(test_notif_banner_buttons_route_actions);
    RUN_TEST(test_media_banner_hidden_is_quiet);
    RUN_TEST(test_media_banner_cache_skips_unchanged);
    RUN_TEST(test_media_banner_track_and_state_repaint);
    RUN_TEST(test_media_banner_unknown_field_fallbacks);
    RUN_TEST(test_media_banner_buttons_route_actions);
    RUN_TEST(test_now_playing_cache_skips_unchanged);
    RUN_TEST(test_now_playing_fires_on_change);
    RUN_TEST(test_now_playing_partial_metadata);
    RUN_TEST(test_clock_cache_skips_unchanged);
    RUN_TEST(test_clock_cache_fires_on_minute_change);
    RUN_TEST(test_clock_fires_on_hour_change);
    RUN_TEST(test_turn_signals_right_side_and_both);
    RUN_TEST(test_warning_lights_full_lamp_table);
    RUN_TEST(test_warning_lights_beam_slot_rotates);
    RUN_TEST(test_warning_lights_layout_fallbacks);
    RUN_TEST(test_odometer_cache_skips_sub_km);
    RUN_TEST(test_odometer_cache_fires_on_km_change);
    RUN_TEST(test_odometer_cache_fires_on_units_change);
    RUN_TEST(test_odometer_units_change_with_equal_value);
    RUN_TEST(test_trip_cache_skips_sub_tenth_km);
    RUN_TEST(test_trip_cache_fires_on_tenth_change);
    RUN_TEST(test_trip_cache_fires_on_units_change);
    RUN_TEST(test_trip_units_change_with_equal_value);
    RUN_TEST(test_warning_lights_cache_idle);
    RUN_TEST(test_warning_lights_only_changed_lamp_repaints);
}
