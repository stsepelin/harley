#include "screen_settings.h"
#include "settings_store.h"
#include "sound.h"
#include "theme.h"
#include "ui_manager.h"
#include "bsp/display.h"
#include <stdint.h>
#include <stdio.h>

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

// --- bluetooth button ----------------------------------------------------

static void bluetooth_button_clicked_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings_bluetooth();
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

    // BLUETOOTH — opens the dedicated sub-page (status, visibility,
    // forget devices). Styled as a row to fit the rest of the screen
    // visually but with a right-edge chevron-style cue ("›") so it
    // reads as a navigable subpage rather than a value-toggle.
    lv_obj_t *bt_row = make_row(scr, 80, 525);
    lv_obj_add_flag(bt_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bt_row, bluetooth_button_clicked_cb, LV_EVENT_CLICKED, NULL);
    make_caption(bt_row, "BLUETOOTH", LV_ALIGN_LEFT_MID, VROD_TEXT);
    make_caption(bt_row, ">", LV_ALIGN_RIGHT_MID, VROD_ORANGE);

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
