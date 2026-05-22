#include "screen_settings.h"
#include "ble_peripheral.h"
#include "settings_store.h"
#include "sound.h"
#include "theme.h"
#include "ui_manager.h"
#include "bsp/display.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);

// Row geometry. Width is the key bezel-clearance number: an 800×800 round
// display has visible radius 400 from centre, and the row corners are the
// pixels closest to the bezel. 540-wide rows centred at y=130 keep every
// corner ~380 px from centre, comfortably inside the circle.
#define ROW_W   540

// In-memory edit buffer. Mutated by the row handlers, applied (validated +
// saved + made current) on every change so that backing out via BACK
// always lands on the persisted state — no separate save step.
static settings_t s_pending;

static lv_obj_t *s_units_value;
static lv_obj_t *s_sound_badge;
static lv_obj_t *s_brightness_value;
static lv_obj_t *s_volume_value;
static lv_obj_t *s_phone_status;
static lv_obj_t *s_phone_addr;
static lv_timer_t *s_phone_timer;
static ble_peripheral_state_t s_phone_last;

// --- shared row styling --------------------------------------------------

static lv_obj_t *make_row(lv_obj_t *parent, int32_t height, int32_t y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, ROW_W, height);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *make_caption(lv_obj_t *row, const char *text, lv_align_t align, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, &jbm_bold_33, 0);
    lv_obj_align(lbl, align, 0, 0);
    return lbl;
}

// Slider on the bottom-left of a row, percent label on the bottom-right.
// Shared between the SOUND volume control and the BRIGHTNESS control —
// the two are visually identical so the layout/styling lives in one place.
typedef struct {
    lv_obj_t *slider;
    lv_obj_t *value;
} percent_control_t;

static percent_control_t make_percent_control(lv_obj_t *row,
                                              int32_t min, int32_t max, int32_t initial,
                                              lv_event_cb_t on_change,
                                              lv_event_cb_t on_release)
{
    percent_control_t out;

    out.slider = lv_slider_create(row);
    lv_obj_set_size(out.slider, 350, 24);
    lv_obj_align(out.slider, LV_ALIGN_BOTTOM_LEFT, 10, -12);
    lv_slider_set_range(out.slider, min, max);
    lv_slider_set_value(out.slider, initial, LV_ANIM_OFF);
    lv_obj_add_event_cb(out.slider, on_change,  LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(out.slider, on_release, LV_EVENT_RELEASED,      NULL);

    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)initial);
    out.value = lv_label_create(row);
    lv_label_set_text(out.value, buf);
    lv_obj_set_style_text_color(out.value, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(out.value, &jbm_bold_33, 0);
    lv_obj_align(out.value, LV_ALIGN_BOTTOM_RIGHT, -10, -8);

    return out;
}

// Updates the SOUND badge label + colour to match the current pending
// state. Pulled out so we can call it from both the create path and the
// toggle handler without duplicating styling.
static void refresh_sound_badge(void)
{
    bool on = s_pending.sound_enabled;
    lv_label_set_text(s_sound_badge, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_sound_badge,
        lv_color_hex(on ? VROD_ORANGE : VROD_TEXT_DIM), 0);
}

// --- units row -----------------------------------------------------------

static void units_row_clicked_cb(lv_event_t *e)
{
    (void)e;
    s_pending.units = (s_pending.units == UNITS_KPH) ? UNITS_MPH : UNITS_KPH;
    lv_label_set_text(s_units_value, units_distance_label(s_pending.units));
    settings_store_apply(&s_pending);
}

// --- sound row (toggle badge + volume slider in one card) ----------------

static void sound_badge_clicked_cb(lv_event_t *e)
{
    (void)e;
    s_pending.sound_enabled = !s_pending.sound_enabled;
    refresh_sound_badge();
    sound_set_enabled(s_pending.sound_enabled);
    settings_store_apply(&s_pending);
}

static void volume_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(slider);
    s_pending.volume = (uint8_t)v;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", v);
    lv_label_set_text(s_volume_value, buf);
    sound_set_volume((uint8_t)v);   // live preview
}

static void volume_released_cb(lv_event_t *e)
{
    (void)e;
    settings_store_apply(&s_pending);
}

// --- brightness row ------------------------------------------------------

static void brightness_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(slider);
    s_pending.brightness = (uint8_t)v;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", v);
    lv_label_set_text(s_brightness_value, buf);
    bsp_display_brightness_set(v);   // live preview
}

static void brightness_released_cb(lv_event_t *e)
{
    (void)e;
    settings_store_apply(&s_pending);
}

// --- phone row -----------------------------------------------------------

// Connection state changes asynchronously from the NimBLE host task, so a
// timer is cheaper than wiring a callback all the way through the BLE
// subsystem. 1 Hz is plenty — a human reading "CONNECTED" appearing one
// second after the actual connect is fine. Skip the label rewrite when
// state hasn't moved so we don't invalidate the row every tick.
static void refresh_phone_row(void)
{
    ble_peripheral_state_t s;
    ble_peripheral_get_state(&s);
    if (s.connected   == s_phone_last.connected &&
        s.advertising == s_phone_last.advertising &&
        s.powered     == s_phone_last.powered &&
        strcmp(s.peer_addr_str, s_phone_last.peer_addr_str) == 0) {
        return;
    }
    s_phone_last = s;

    const char *txt;
    uint32_t color;
    if (s.connected) {
        txt = "TAP TO DISCONNECT";
        color = VROD_ORANGE;
    } else if (s.advertising) {
        txt = "ADVERTISING";
        color = VROD_TEXT_DIM;
    } else if (s.powered) {
        txt = "IDLE";
        color = VROD_TEXT_DIM;
    } else {
        txt = "OFF";
        color = VROD_TEXT_DIM;
    }
    lv_label_set_text(s_phone_status, txt);
    lv_obj_set_style_text_color(s_phone_status, lv_color_hex(color), 0);
    lv_label_set_text(s_phone_addr, s.connected ? s.peer_addr_str : "");
}

static void phone_timer_cb(lv_timer_t *t)
{
    (void)t;
    // Skip when settings isn't the active screen — the timer outlives the
    // visibility of the row it updates (screens are kept alive across
    // ui_manager_show_*), and mutating an off-screen label still costs a
    // critical-section read of BLE state for no visual benefit. Lifecycle
    // hygiene more than perf — at 1 Hz the wasted work is microscopic.
    if (lv_screen_active() != lv_obj_get_screen(s_phone_status)) return;
    refresh_phone_row();
}

static void phone_row_clicked_cb(lv_event_t *e)
{
    (void)e;
    ble_peripheral_state_t s;
    ble_peripheral_get_state(&s);
    if (!s.connected) return;  // tap is a no-op when nothing's there
    ble_peripheral_disconnect_active();
    refresh_phone_row();
}

// Long-press the PHONE row to forget every stored bond. Consistent with
// the long-press-to-enter-settings gesture pattern — discoverable for
// anyone who already knows that shortcut. Disconnects the active link
// first so the central isn't talking to a cluster that no longer
// remembers it.
static void phone_row_long_press_cb(lv_event_t *e)
{
    (void)e;
    ble_peripheral_disconnect_active();
    ble_peripheral_forget_all_bonds();
    refresh_phone_row();
}

// --- back ----------------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_ride();
}

// --- screen --------------------------------------------------------------

lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Snapshot persisted state into the edit buffer; handlers mutate it.
    s_pending = *settings_store_current();

    // Title.
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    // UNITS — tap row to toggle km/mi.
    lv_obj_t *units_row = make_row(scr, 80, 130);
    lv_obj_add_flag(units_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(units_row, units_row_clicked_cb, LV_EVENT_CLICKED, NULL);
    make_caption(units_row, "UNITS", LV_ALIGN_LEFT_MID, VROD_TEXT);
    s_units_value = make_caption(units_row,
        units_distance_label(s_pending.units), LV_ALIGN_RIGHT_MID, VROD_ORANGE);

    // SOUND — single card: caption + ON/OFF badge at top, volume slider
    // along the bottom. The badge itself is the toggle target so the
    // slider area stays free for drag gestures.
    lv_obj_t *sound_row = make_row(scr, 130, 230);
    make_caption(sound_row, "SOUND", LV_ALIGN_TOP_LEFT, VROD_TEXT);

    s_sound_badge = lv_label_create(sound_row);
    lv_obj_set_style_text_font(s_sound_badge, &jbm_bold_33, 0);
    // Generous tap area around the short "ON"/"OFF" text so it's still
    // glove-friendly. ext_click_pad expands the hit region without
    // affecting layout.
    lv_obj_set_ext_click_area(s_sound_badge, 20);
    lv_obj_add_flag(s_sound_badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_sound_badge, sound_badge_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(s_sound_badge, LV_ALIGN_TOP_RIGHT, 0, 0);
    refresh_sound_badge();

    {
        percent_control_t vol = make_percent_control(sound_row, 0, 100, s_pending.volume,
                                                     volume_changed_cb, volume_released_cb);
        s_volume_value = vol.value;
    }

    // BRIGHTNESS — same layout as the SOUND row (caption top-left, slider
    // bottom-left, percent bottom-right), minus the on/off badge so the
    // top-right stays empty.
    lv_obj_t *bright_row = make_row(scr, 130, 380);
    make_caption(bright_row, "BRIGHTNESS", LV_ALIGN_TOP_LEFT, VROD_TEXT);

    {
        percent_control_t b = make_percent_control(bright_row,
            SETTINGS_BRIGHTNESS_MIN, 100, s_pending.brightness,
            brightness_changed_cb, brightness_released_cb);
        s_brightness_value = b.value;
    }

    // PHONE — connection status + tap-to-disconnect. Sits between the
    // BRIGHTNESS card (ends y=510) and the BACK button (starts y=640
    // measured from the top, since LV_ALIGN_BOTTOM_MID with y=-80 puts
    // an 80-tall button there). Row height 100 at y=525 leaves a 15-px
    // breather above BACK.
    lv_obj_t *phone_row = make_row(scr, 100, 525);
    lv_obj_add_flag(phone_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(phone_row, phone_row_clicked_cb,    LV_EVENT_CLICKED,     NULL);
    lv_obj_add_event_cb(phone_row, phone_row_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    make_caption(phone_row, "PHONE", LV_ALIGN_TOP_LEFT, VROD_TEXT);

    s_phone_status = lv_label_create(phone_row);
    lv_obj_set_style_text_font(s_phone_status, &jbm_bold_33, 0);
    lv_obj_align(s_phone_status, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_phone_addr = lv_label_create(phone_row);
    lv_obj_set_style_text_font(s_phone_addr, &jbm_bold_33, 0);
    lv_obj_set_style_text_color(s_phone_addr, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_align(s_phone_addr, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // Force the first paint with the real state before the timer's first
    // tick — otherwise the row sits empty for up to a second after open.
    memset(&s_phone_last, 0xff, sizeof(s_phone_last));   // force diff
    refresh_phone_row();
    if (!s_phone_timer) {
        s_phone_timer = lv_timer_create(phone_timer_cb, 1000, NULL);
    }

    // BACK button — glove-friendly tap target.
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_size(back, 260, 80);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_color(back, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_radius(back, 12, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "BACK");
    lv_obj_set_style_text_color(back_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(back_lbl, &jbm_bold_45, 0);
    lv_obj_center(back_lbl);

    return scr;
}
