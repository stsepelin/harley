#include "screen_ride.h"
#include "tach_arc.h"
#include "speed_display.h"
#include "gear_indicator.h"
#include "fuel_arc.h"
#include "turn_signals.h"
#include "temp_display.h"
#include "warning_lights.h"
#include "clock_display.h"
#include "odometer_display.h"
#include "trip_display.h"
#include "notification_banner.h"
#include "media_banner.h"
#include "ble_peripheral.h"
#include "phone_data.h"
#include "theme.h"
#include "ui_manager.h"

// Upshift warning: above the tach's redline the gear digit blinks red.
// Lives on the small gear widget so the per-blink invalidation is a tiny
// ~80×110 region rather than the full 800×800 tach area — that's what
// made the previous tach-arc-based warnings feel laggy. The threshold is
// TACH_REDLINE_RPM (tach_arc.h) so warning and scale can never disagree.

static lv_obj_t *s_screen;
static lv_obj_t *s_tach;
static lv_obj_t *s_speed;
static lv_obj_t *s_gear;
static lv_obj_t *s_fuel;
static lv_obj_t *s_turn;
static lv_obj_t *s_temp;
static lv_obj_t *s_warn_l;
static lv_obj_t *s_warn_r;
static lv_obj_t *s_clock;
static lv_obj_t *s_odo;
static lv_obj_t *s_trip1;
static lv_obj_t *s_trip2;
static lv_obj_t *s_banner;
static lv_obj_t *s_media_banner;
static lv_obj_t *s_media_hint;
static lv_obj_t *s_ble_dot;

// Long-press, turn-signal-edge detection, and now swipe-to-dismiss all
// live in the 100 Hz event-watcher task (ui_manager.c). The UI side
// gets a callback only for the explicit accept/reject button taps —
// those fire through LVGL's normal click events.

static void on_call_action(call_action_t action)
{
    switch (action) {
    case CALL_ACTION_ACCEPT: phone_data_call_accept(); break;
    case CALL_ACTION_REJECT: phone_data_call_reject(); break;
    case CALL_ACTION_END:    phone_data_call_end();    break;
    }
}

static void on_media_action(media_action_t action)
{
    switch (action) {
    case MEDIA_ACTION_PREV:       phone_data_media_action(PHONE_MEDIA_ACTION_PREV);       break;
    case MEDIA_ACTION_PLAY_PAUSE: phone_data_media_action(PHONE_MEDIA_ACTION_PLAY_PAUSE); break;
    case MEDIA_ACTION_NEXT:       phone_data_media_action(PHONE_MEDIA_ACTION_NEXT);       break;
    }
}

lv_obj_t *screen_ride_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Main tach + speed (centered).
    s_tach = tach_arc_create(s_screen);
    lv_obj_center(s_tach);
    s_speed = speed_display_create(s_screen);
    lv_obj_align(s_speed, LV_ALIGN_CENTER, 0,
                 -108);  // digit row (container -15) lands on the arrow line

    // Gear sits in the bottom-centre pocket above the fuel arc (where the fuel
    // pump icon used to be); the turn arrows stay flanking the top-centre slot.
    s_gear = gear_indicator_create(s_screen);
    lv_obj_align(s_gear, LV_ALIGN_BOTTOM_MID, 0, -70);
    s_turn = turn_signals_create(s_screen);
    lv_obj_align(s_turn, LV_ALIGN_CENTER, 0, -123);

    // Warning lamps flank the speed digits as two chevrons (2 lamps top + 1
    // bottom-centre — a V shape). Left = drivetrain warnings; right = lights
    // + ABS + immobiliser, with the beam slot rotating between low and high.
    static const lamp_id_t LAMPS_LEFT[]  = { LAMP_OIL, LAMP_ENGINE, LAMP_BATTERY };
    static const lamp_id_t LAMPS_RIGHT[] = { LAMP_ABS, LAMP_BEAM,   LAMP_IMMOBILISER };
    s_warn_l = warning_lights_create(s_screen, LAMPS_LEFT,  3, WARN_LAYOUT_CHEVRON);
    lv_obj_align(s_warn_l, LV_ALIGN_LEFT_MID, 156, 83);
    s_warn_r = warning_lights_create(s_screen, LAMPS_RIGHT, 3, WARN_LAYOUT_CHEVRON);
    lv_obj_align(s_warn_r, LV_ALIGN_RIGHT_MID, -156, 83);

    // Clock, odometer, and the two trip counters share one slot above the
    // fuel bar — they cycle every few seconds (see screen_ride_update). All
    // four widgets share the same alignment; three are always hidden.
    s_clock = clock_display_create(s_screen);
    lv_obj_align(s_clock, LV_ALIGN_BOTTOM_MID, 0, -265);
    s_odo   = odometer_display_create(s_screen);
    lv_obj_align(s_odo, LV_ALIGN_BOTTOM_MID, 0, -265);
    lv_obj_add_flag(s_odo, LV_OBJ_FLAG_HIDDEN);
    s_trip1 = trip_display_create(s_screen, "TRIP1");
    lv_obj_align(s_trip1, LV_ALIGN_BOTTOM_MID, 0, -265);
    lv_obj_add_flag(s_trip1, LV_OBJ_FLAG_HIDDEN);
    s_trip2 = trip_display_create(s_screen, "TRIP2");
    lv_obj_align(s_trip2, LV_ALIGN_BOTTOM_MID, 0, -265);
    lv_obj_add_flag(s_trip2, LV_OBJ_FLAG_HIDDEN);

    // Fuel arc hugs the bottom bezel; temp sits above it.
    s_fuel = fuel_arc_create(s_screen);
    lv_obj_align(s_fuel, LV_ALIGN_BOTTOM_MID, 0, -8);
    s_temp = temp_display_create(s_screen);
    lv_obj_align(s_temp, LV_ALIGN_BOTTOM_MID, 0, -165);

    // Bottom-anchored overlays. The notification banner is shown
    // automatically when a notification arrives (resizes between
    // CALL-with-buttons and the shorter SMS/APP form); the media
    // banner is user-toggled via swipe-up / swipe-down. They share
    // the same anchor — the notification takes priority, and
    // phone_data auto-clears `media_banner_shown` on notif arrival.
    //
    // y_offset -55 lifts the bottom edge clear of the bezel: a 400-wide
    // banner has visible-circle clearance at y ≤ 746, so the bottom
    // sits at 745 with corners just inside the round visible area.
    s_banner = notification_banner_create(s_screen, on_call_action);
    lv_obj_align(s_banner, LV_ALIGN_BOTTOM_MID, 0, -55);

    s_media_banner = media_banner_create(s_screen, on_media_action);
    lv_obj_align(s_media_banner, LV_ALIGN_BOTTOM_MID, 0, -55);

    // Hidden-banner hint: a small grab-bar at the bottom edge, visible
    // only when media is available but the banner is hidden. Mirrors the
    // iOS/Android sheet-handle convention so the swipe-up gesture is
    // discoverable. y=-12 puts it just inside the round bezel.
    s_media_hint = lv_obj_create(s_screen);
    lv_obj_set_size(s_media_hint, 50, 4);
    lv_obj_align(s_media_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(s_media_hint, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_bg_opa(s_media_hint, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_media_hint, 0, 0);
    lv_obj_set_style_radius(s_media_hint, 2, 0);
    lv_obj_remove_flag(s_media_hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_media_hint, LV_OBJ_FLAG_HIDDEN);

    // BLE connection dot: small blue pip at the top of the round face,
    // shown only while a central is connected. y=60 sits between the tach
    // labels and the turn arrows and well inside the visible circle
    // (half-chord at y=60 is ~213 px).
    s_ble_dot = lv_obj_create(s_screen);
    lv_obj_set_size(s_ble_dot, 16, 16);
    lv_obj_align(s_ble_dot, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(s_ble_dot, lv_color_hex(VROD_BLUE_HIGH_BEAM), 0);
    lv_obj_set_style_bg_opa(s_ble_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ble_dot, 0, 0);
    lv_obj_set_style_radius(s_ble_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(s_ble_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ble_dot, LV_OBJ_FLAG_HIDDEN);

    return s_screen;
}

void screen_ride_update(const vehicle_data_t *data, const settings_t *settings)
{
    if (!s_tach) return;

    display_units_t units = settings->units;

    tach_arc_set_value(s_tach, data->rpm);
    speed_display_set_value(s_speed, data->speed_mph, units);
    gear_indicator_set(s_gear, data->gear);
    gear_indicator_set_warning(s_gear, data->rpm > TACH_REDLINE_RPM);
    fuel_arc_set_level(s_fuel, data->fuel_level);
    turn_signals_set(s_turn, data->turn_left, data->turn_right);
    temp_display_set_value(s_temp, data->engine_temp_c);
    warning_lights_update(s_warn_l, data);
    warning_lights_update(s_warn_r, data);

    // Phone-bridge snapshot drives both bottom overlays. Banners sit
    // above the info-slot in z-order; the info-slot rotation runs
    // continuously underneath so it's already current when a banner
    // dismisses.
    phone_state_t phone;
    phone_data_get(&phone);
    notification_banner_update(s_banner, &phone.notif);
    // Media banner is only visible when the user has explicitly pulled
    // it up AND there's no active notification claiming the space.
    bool media_visible = phone.media_banner_shown && !phone.notif.active;
    media_banner_update(s_media_banner, &phone.media, media_visible);

    // Hint pill: shows only when media is loaded but the banner is hidden
    // (and nothing else is occupying the bottom slot). Toggled on
    // transitions only so a steady-state frame doesn't invalidate it.
    bool hint_visible = !media_visible
                     && !phone.notif.active
                     && (phone.media.state == MEDIA_STATE_PLAYING
                      || phone.media.state == MEDIA_STATE_PAUSED);
    static int prev_hint = -1;
    if ((int)hint_visible != prev_hint) {
        if (hint_visible) lv_obj_remove_flag(s_media_hint, LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag   (s_media_hint, LV_OBJ_FLAG_HIDDEN);
        prev_hint = (int)hint_visible;
    }

    // BLE connection dot: same toggle-on-change pattern. ble_peripheral_get_state
    // is a short critical section so polling at 30 Hz is fine.
    ble_peripheral_state_t ble;
    ble_peripheral_get_state(&ble);
    static int prev_ble = -1;
    if ((int)ble.connected != prev_ble) {
        if (ble.connected) lv_obj_remove_flag(s_ble_dot, LV_OBJ_FLAG_HIDDEN);
        else               lv_obj_add_flag   (s_ble_dot, LV_OBJ_FLAG_HIDDEN);
        prev_ble = (int)ble.connected;
    }

    // Rotating info slot: clock → odo → trip1 → trip2. Toggle HIDDEN
    // flags only on mode changes, otherwise every frame would
    // invalidate all four widgets.
    enum { INFO_SLOT_FRAMES = 150, INFO_SLOT_COUNT = 4 };
    static uint32_t info_tick = 0;
    static int      prev_mode = -1;
    info_tick++;
    int mode = (info_tick / INFO_SLOT_FRAMES) % INFO_SLOT_COUNT;
    if (mode != prev_mode) {
        lv_obj_t *slots[INFO_SLOT_COUNT] = { s_clock, s_odo, s_trip1, s_trip2 };
        for (int i = 0; i < INFO_SLOT_COUNT; i++) lv_obj_add_flag(slots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(slots[mode], LV_OBJ_FLAG_HIDDEN);
        prev_mode = mode;
    }
    switch (mode) {
        case 0: clock_display_set(s_clock, data->clock_hours, data->clock_minutes); break;
        case 1: odometer_display_set(s_odo, data->odometer_m, units);               break;
        case 2: trip_display_set(s_trip1, data->trip1_m, units);                    break;
        case 3: trip_display_set(s_trip2, data->trip2_m, units);                    break;
    }
}
