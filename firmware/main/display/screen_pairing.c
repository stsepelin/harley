#include "screen_pairing.h"
#include "ble_peripheral.h"
#include "theme.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include <stdio.h>

LV_FONT_DECLARE(jbm_bold_33);
LV_FONT_DECLARE(jbm_bold_45);
LV_FONT_DECLARE(jbm_bold_72);

// Modal lifecycle: the screen is built lazily on first show and kept
// across pair requests (the rider may need to re-pair after a "forget
// all devices"). We don't free it; pairing prompts are rare and the
// memory is sub-kilobyte.
static lv_obj_t *s_screen;
static lv_obj_t *s_passkey_label;
// Snapshot of the screen that was active before we hijacked the
// display, so accept / cancel can route back to it. The settings or
// ride screen objects live forever (ui_manager keeps them cached), so
// holding a raw pointer here is safe.
static lv_obj_t *s_previous_screen;

static void respond_and_close(bool accept)
{
    ble_peripheral_pair_respond(accept);
    if (s_previous_screen) lv_screen_load(s_previous_screen);
    s_previous_screen = NULL;
}

static void accept_cb(lv_event_t *e) { (void)e; respond_and_close(true);  }
static void cancel_cb(lv_event_t *e) { (void)e; respond_and_close(false); }

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             uint32_t bg_color, uint32_t text_color,
                             lv_align_t align, int32_t x, int32_t y,
                             lv_event_cb_t on_tap)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 260, 80);
    lv_obj_align(btn, align, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, on_tap, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &jbm_bold_33, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(text_color), 0);
    lv_obj_center(lbl);
    return btn;
}

static void build_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "PAIR PHONE?");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t *hint = lv_label_create(s_screen);
    lv_label_set_text(hint, "MATCH ON PHONE");
    lv_obj_set_style_text_color(hint, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(hint, &jbm_bold_33, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 200);

    s_passkey_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_passkey_label, &jbm_bold_72, 0);
    lv_obj_set_style_text_color(s_passkey_label, lv_color_white(), 0);
    lv_obj_align(s_passkey_label, LV_ALIGN_CENTER, 0, 0);

    // Accept (green) and cancel (red) sit side by side near the bottom
    // of the round display. 130 px in from the centre line keeps them
    // well inside the bezel at y ≈ 640.
    make_button(s_screen, "CANCEL",
                VROD_RED_BRIGHT, 0xFFFFFF,
                LV_ALIGN_BOTTOM_LEFT,  60, -100, cancel_cb);
    make_button(s_screen, "ACCEPT",
                VROD_GREEN_SIGNAL, 0x000000,
                LV_ALIGN_BOTTOM_RIGHT, -60, -100, accept_cb);
}

void screen_pairing_show(uint32_t passkey)
{
    bsp_display_lock(-1);
    if (!s_screen) build_screen();

    // SC numeric comparison passkeys are always 6 digits, zero-padded.
    char buf[8];
    snprintf(buf, sizeof(buf), "%06lu", (unsigned long)passkey);
    lv_label_set_text(s_passkey_label, buf);

    s_previous_screen = lv_screen_active();
    lv_screen_load(s_screen);
    bsp_display_unlock();
}
