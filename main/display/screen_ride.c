#include "screen_ride.h"
#include "tach_arc.h"
#include "speed_display.h"
#include "gear_indicator.h"
#include "fuel_bar.h"
#include "turn_signals.h"
#include "temp_display.h"
#include "warning_lights.h"
#include "clock_display.h"
#include "odometer_display.h"
#include "trip_display.h"
#include "theme.h"
#include "ui_manager.h"

// Shift-light: flashing red ring at the bezel above SHIFT_LIGHT_RPM, telling
// the rider to upshift now. ~5 Hz blink (3 frames on, 3 off at 30 FPS).
#define SHIFT_LIGHT_ENABLED 1
#define SHIFT_LIGHT_RPM     9000
#define SHIFT_LIGHT_FRAMES  3

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
static lv_obj_t *s_shift_light;

// Long-press → settings, polled from the indev's raw state. Times the
// hold with lv_tick_elapsed so a stalled frame doesn't make the press
// feel sluggish (frame-count timing would lose ~33 ms per dropped frame).
// LVGL's own LV_EVENT_LONG_PRESSED is not used here because press events
// were not reaching the ride screen on this BSP — see commit history.
#define LONG_PRESS_MS 600

static bool long_press_should_fire(void)
{
    static uint32_t press_start_tick;
    static bool     pressing;
    static bool     fired;

    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev || lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
        pressing = false;
        fired    = false;
        return false;
    }

    if (!pressing) {
        pressing         = true;
        press_start_tick = lv_tick_get();
    }
    if (fired) return false;
    if (lv_tick_elaps(press_start_tick) < LONG_PRESS_MS) return false;
    fired = true;
    return true;
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
    lv_obj_center(s_speed);

    // Gear + turn signals at the same Y, with the arrows flanking the gear.
    s_gear = gear_indicator_create(s_screen);
    lv_obj_align(s_gear, LV_ALIGN_CENTER, 0, -150);
    s_turn = turn_signals_create(s_screen);
    lv_obj_align(s_turn, LV_ALIGN_CENTER, 0, -150);

    // Warning lamps flank the speed digits as two chevrons (2 lamps top + 1
    // bottom-centre — a V shape). Left = drivetrain warnings; right = lights
    // + ABS + immobiliser, with the beam slot rotating between low and high.
    static const lamp_id_t LAMPS_LEFT[]  = { LAMP_OIL, LAMP_ENGINE, LAMP_BATTERY };
    static const lamp_id_t LAMPS_RIGHT[] = { LAMP_ABS, LAMP_BEAM,   LAMP_IMMOBILISER };
    s_warn_l = warning_lights_create(s_screen, LAMPS_LEFT,  3, WARN_LAYOUT_CHEVRON);
    lv_obj_align(s_warn_l, LV_ALIGN_LEFT_MID, 125, 0);
    s_warn_r = warning_lights_create(s_screen, LAMPS_RIGHT, 3, WARN_LAYOUT_CHEVRON);
    lv_obj_align(s_warn_r, LV_ALIGN_RIGHT_MID, -125, 0);

    // Clock, odometer, and the two trip counters share one slot above the
    // fuel bar — they cycle every few seconds (see screen_ride_update). All
    // four widgets share the same alignment; three are always hidden.
    s_clock = clock_display_create(s_screen);
    lv_obj_align(s_clock, LV_ALIGN_BOTTOM_MID, 0, -203);
    s_odo   = odometer_display_create(s_screen);
    lv_obj_align(s_odo, LV_ALIGN_BOTTOM_MID, 0, -203);
    lv_obj_add_flag(s_odo, LV_OBJ_FLAG_HIDDEN);
    s_trip1 = trip_display_create(s_screen, "TRIP1");
    lv_obj_align(s_trip1, LV_ALIGN_BOTTOM_MID, 0, -203);
    lv_obj_add_flag(s_trip1, LV_OBJ_FLAG_HIDDEN);
    s_trip2 = trip_display_create(s_screen, "TRIP2");
    lv_obj_align(s_trip2, LV_ALIGN_BOTTOM_MID, 0, -203);
    lv_obj_add_flag(s_trip2, LV_OBJ_FLAG_HIDDEN);
    s_fuel = fuel_bar_create(s_screen);
    lv_obj_align(s_fuel, LV_ALIGN_BOTTOM_MID, 0, -95);
    s_temp = temp_display_create(s_screen);
    lv_obj_align(s_temp, LV_ALIGN_BOTTOM_MID, 0, -25);

#if SHIFT_LIGHT_ENABLED
    // Shift-light ring — added LAST so it draws on top of everything else.
    // Full-screen 800×800 circular border at the bezel; hidden until rpm
    // crosses the shift threshold, then blinked by screen_ride_update.
    s_shift_light = lv_obj_create(s_screen);
    lv_obj_set_size(s_shift_light, 800, 800);
    lv_obj_set_style_bg_opa(s_shift_light, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_shift_light, lv_color_hex(VROD_RED_BRIGHT), 0);
    lv_obj_set_style_border_width(s_shift_light, 15, 0);
    lv_obj_set_style_radius(s_shift_light, 400, 0);
    lv_obj_set_style_pad_all(s_shift_light, 0, 0);
    lv_obj_center(s_shift_light);
    lv_obj_remove_flag(s_shift_light, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_shift_light, LV_OBJ_FLAG_HIDDEN);
#endif

    return s_screen;
}

void screen_ride_update(const vehicle_data_t *data, const settings_t *settings)
{
    if (!s_tach) return;

    // Guard the long-press hand-off so a hold on the settings screen
    // doesn't bounce straight back here.
    if (lv_screen_active() == s_screen && long_press_should_fire()) {
        ui_manager_show_settings();
        return;
    }

    display_units_t units = settings->units;

    tach_arc_set_value(s_tach, data->rpm);
    speed_display_set_value(s_speed, data->speed_kmh, units);
    gear_indicator_set(s_gear, data->gear);
    fuel_bar_set_level(s_fuel, data->fuel_level);
    turn_signals_set(s_turn, data->turn_left, data->turn_right);
    temp_display_set_value(s_temp, data->engine_temp_c);
    warning_lights_update(s_warn_l, data);
    warning_lights_update(s_warn_r, data);

    // Rotating info slot: cycle clock → odo → trip1 → trip2 every ~5 s
    // (UI runs at ~30 FPS, so 150 frames). Only update the visible widget —
    // hidden ones get a fresh value when they cycle in. And only flip
    // HIDDEN flags on actual mode changes, otherwise every frame would
    // invalidate all four slot widgets for nothing.
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

#if SHIFT_LIGHT_ENABLED
    // Shift-light blink. Toggle visibility only when the *displayed* state
    // changes — most frames stay quiet (rpm below threshold, or mid-blink
    // half-cycle still showing the same on/off as last frame).
    static uint32_t shift_tick = 0;
    static bool     shift_shown = false;
    shift_tick++;
    bool above_threshold = data->rpm > SHIFT_LIGHT_RPM;
    bool show = above_threshold
             && ((shift_tick / SHIFT_LIGHT_FRAMES) & 1) == 0;
    if (show != shift_shown) {
        if (show) lv_obj_remove_flag(s_shift_light, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_shift_light, LV_OBJ_FLAG_HIDDEN);
        shift_shown = show;
    }
#endif
}
