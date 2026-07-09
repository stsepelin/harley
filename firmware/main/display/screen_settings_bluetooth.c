#include "screen_settings_bluetooth.h"
#include "ble_peripheral.h"
#include "settings.h"
#include "settings_store.h"
#include "theme.h"
#include "ui_manager.h"
#include <stdint.h>
#include <string.h>

LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_33);

#define ROW_W 540

// Edit buffer mirroring the main settings screen's pattern: snapshot
// persisted state on create, mutate on toggle, save on every change so
// BACK lands on a coherent state with no separate commit step.
static settings_t s_pending;

static lv_obj_t              *s_status_value;
static lv_obj_t              *s_addr_value;
static lv_obj_t              *s_visible_badge;
static lv_timer_t            *s_status_timer;
static ble_peripheral_state_t s_last_state;

// --- shared row styling (kept local to avoid pulling in screen_settings.c) -

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

// --- PHONE row: status + tap-to-disconnect -------------------------------

static void refresh_status_row(void)
{
    ble_peripheral_state_t s;
    ble_peripheral_get_state(&s);
    if (s.connected == s_last_state.connected && s.advertising == s_last_state.advertising &&
        s.powered == s_last_state.powered &&
        strcmp(s.peer_addr_str, s_last_state.peer_addr_str) == 0) {
        return;
    }
    s_last_state = s;

    const char *txt;
    uint32_t    color;
    if (s.connected) {
        txt   = "TAP TO DISCONNECT";
        color = VROD_ORANGE;
    } else if (s.advertising) {
        txt   = "ADVERTISING";
        color = VROD_TEXT_DIM;
    } else if (s.powered) {
        txt   = "IDLE";
        color = VROD_TEXT_DIM;
    } else {
        txt   = "OFF";
        color = VROD_TEXT_DIM;
    }
    lv_label_set_text(s_status_value, txt);
    lv_obj_set_style_text_color(s_status_value, lv_color_hex(color), 0);
    lv_label_set_text(s_addr_value, s.connected ? s.peer_addr_str : "");
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    // Skip when this screen isn't active — the timer outlives the
    // visibility of the row it updates (screens are kept alive across
    // ui_manager_show_*), and mutating an off-screen label still costs
    // a critical-section read of BLE state for no visual benefit.
    if (lv_screen_active() != lv_obj_get_screen(s_status_value))
        return;
    refresh_status_row();
}

static void status_row_clicked_cb(lv_event_t *e)
{
    (void)e;
    ble_peripheral_state_t s;
    ble_peripheral_get_state(&s);
    if (!s.connected)
        return;
    ble_peripheral_disconnect_active();
    refresh_status_row();
}

// --- VISIBILITY toggle ---------------------------------------------------

static void refresh_visible_badge(void)
{
    if (!s_visible_badge)
        return;
    bool on = s_pending.ble_visible_override;
    lv_label_set_text(s_visible_badge, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_visible_badge, lv_color_hex(on ? VROD_ORANGE : VROD_TEXT_DIM), 0);
}

static void visible_row_clicked_cb(lv_event_t *e)
{
    (void)e;
    s_pending.ble_visible_override = !s_pending.ble_visible_override;
    settings_store_apply(&s_pending);
    refresh_visible_badge();
    // Restart advertising so the new mode takes effect immediately when
    // no central is connected. With a connected central the call is a
    // no-op; the mode picks up at the next disconnect.
    ble_peripheral_refresh_visibility();
}

// --- FORGET ALL DEVICES --------------------------------------------------

// Two-tap confirm: the first tap arms and relabels the button; a second tap
// within the window actually wipes the bonds. Avoids a stray touch clearing
// every paired phone. A timer disarms it if the second tap never comes.
static lv_obj_t   *s_forget_lbl;
static bool        s_forget_armed;
static lv_timer_t *s_forget_revert_timer;

static void forget_disarm(void)
{
    s_forget_armed = false;
    if (s_forget_lbl)
        lv_label_set_text(s_forget_lbl, "FORGET ALL DEVICES");
    if (s_forget_revert_timer) {
        lv_timer_del(s_forget_revert_timer);
        s_forget_revert_timer = NULL;
    }
}

static void forget_revert_cb(lv_timer_t *t)
{
    (void)t;
    forget_disarm();
}

static void forget_button_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (!s_forget_armed) {
        s_forget_armed = true;
        if (s_forget_lbl)
            lv_label_set_text(s_forget_lbl, "TAP AGAIN TO CONFIRM");
        s_forget_revert_timer = lv_timer_create(forget_revert_cb, 4000, NULL);
        return;
    }
    forget_disarm();
    ble_peripheral_disconnect_active();
    ble_peripheral_forget_all_bonds();
    refresh_status_row();
}

// --- BACK ----------------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_show_settings();
}

// --- screen --------------------------------------------------------------

lv_obj_t *screen_settings_bluetooth_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_pending = *settings_store_current();

    // Title.
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "BLUETOOTH");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    // PHONE — status + peer address. Tap to disconnect active link.
    lv_obj_t *phone_row = make_row(scr, 110, 140);
    lv_obj_add_flag(phone_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(phone_row, status_row_clicked_cb, LV_EVENT_CLICKED, NULL);
    make_caption(phone_row, "PHONE", LV_ALIGN_TOP_LEFT, VROD_TEXT);

    s_status_value = lv_label_create(phone_row);
    lv_obj_set_style_text_font(s_status_value, &jbm_bold_33, 0);
    lv_obj_align(s_status_value, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_addr_value = lv_label_create(phone_row);
    lv_obj_set_style_text_font(s_addr_value, &jbm_bold_33, 0);
    lv_obj_set_style_text_color(s_addr_value, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_align(s_addr_value, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // VISIBILITY — toggle. ON forces undirected advertising so a new
    // phone can find the cluster; OFF (the default once bonded) uses
    // directed advertising so strangers don't see "Zeppl" in
    // their scan results.
    lv_obj_t *vis_row = make_row(scr, 80, 270);
    lv_obj_add_flag(vis_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(vis_row, visible_row_clicked_cb, LV_EVENT_CLICKED, NULL);
    make_caption(vis_row, "VISIBLE", LV_ALIGN_LEFT_MID, VROD_TEXT);
    s_visible_badge = lv_label_create(vis_row);
    lv_obj_set_style_text_font(s_visible_badge, &jbm_bold_33, 0);
    lv_obj_align(s_visible_badge, LV_ALIGN_RIGHT_MID, 0, 0);
    refresh_visible_badge();

    // Hint text under the VISIBLE row so a first-time user understands
    // what the toggle does. Two short lines, dim grey — doesn't draw
    // attention away from the controls but explains the model.
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "ON: any phone can pair.\n"
                            "OFF: only bonded phone reconnects.");
    lv_obj_set_style_text_color(hint, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(hint, &jbm_bold_33, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 365);

    // FORGET ALL DEVICES — destructive, but the only path back to a
    // truly fresh pairing if the rider wants to switch phones without
    // the auto-revert path. Styled as a button (vs row) to signal that
    // it's an action, not a state toggle.
    lv_obj_t *forget = lv_button_create(scr);
    lv_obj_set_size(forget, ROW_W, 80);
    lv_obj_align(forget, LV_ALIGN_TOP_MID, 0, 470);
    lv_obj_set_style_bg_color(forget, lv_color_hex(0x331111), 0);
    lv_obj_set_style_border_color(forget, lv_color_hex(VROD_RED), 0);
    lv_obj_set_style_border_width(forget, 1, 0);
    lv_obj_set_style_radius(forget, 12, 0);
    lv_obj_add_event_cb(forget, forget_button_clicked_cb, LV_EVENT_CLICKED, NULL);
    s_forget_armed        = false;
    s_forget_revert_timer = NULL;
    s_forget_lbl          = lv_label_create(forget);
    lv_label_set_text(s_forget_lbl, "FORGET ALL DEVICES");
    lv_obj_set_style_text_color(s_forget_lbl, lv_color_hex(VROD_RED), 0);
    lv_obj_set_style_text_font(s_forget_lbl, &jbm_bold_33, 0);
    lv_obj_center(s_forget_lbl);

    // Force the first paint before the timer fires so the status row
    // isn't blank for up to a second after opening the page.
    memset(&s_last_state, 0xff, sizeof(s_last_state));  // force diff
    refresh_status_row();
    if (!s_status_timer) {
        s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    }

    // BACK returns to the main settings screen, not the ride screen —
    // riders descending into this subpage expect a one-step retreat.
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
