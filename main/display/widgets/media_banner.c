#include "media_banner.h"
#include "lvgl.h"
#include "theme.h"
#include "widget_util.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(jbm_bold_26);
LV_FONT_DECLARE(jbm_bold_33);
LV_FONT_DECLARE(mdi_36);

// MDI codepoints for the transport row. Encoded as 4-byte UTF-8 because
// the glyphs live in the U+F0000 plane.
#define ICON_PREV    "\xF3\xB0\x92\xAE"   // U+F04AE skip-previous
#define ICON_PLAY    "\xF3\xB0\x90\x8A"   // U+F040A play
#define ICON_PAUSE   "\xF3\xB0\x8F\xA4"   // U+F03E4 pause
#define ICON_NEXT    "\xF3\xB0\x92\xAD"   // U+F04AD skip-next

// Same width as the notification banner so they stack visually when
// both could be active (notification takes priority — phone_data
// auto-clears the media-shown flag on notif arrival).
// Vertical layout: pad / title 33 / 6 / artist 26 / 22 / buttons 64 / pad.
// MB_H is the sum (18+33+6+26+22+64+18 = 187 → 188 even). The 22 px
// gap between artist and the button row is intentional — at 170 the
// row felt cramped against the text.
#define MB_W            400
#define MB_H            188
#define MB_PAD          18
#define MB_BTN_W        100
#define MB_BTN_H        64
#define MB_LINE_BUF     (MEDIA_FIELD_MAX * 2 + 8)

typedef struct {
    lv_obj_t          *title_label;
    lv_obj_t          *artist_label;
    lv_obj_t          *btn_play_pause_label;
    media_action_cb_t  on_action;
    char               last_title [MEDIA_FIELD_MAX];
    char               last_artist[MEDIA_FIELD_MAX];
    media_state_t      last_state;
    bool               last_visible;
    bool               has_value;
} mb_data_t;

static void on_btn_clicked(lv_event_t *e)
{
    mb_data_t      *bd     = lv_obj_get_user_data(lv_event_get_target(e));
    media_action_t  action = (media_action_t)(intptr_t)lv_event_get_user_data(e);
    if (bd && bd->on_action) bd->on_action(action);
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *label_text, mb_data_t *bd, media_action_t action)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, MB_BTN_W, MB_BTN_H);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_user_data(btn, bd);
    lv_obj_add_event_cb(btn, on_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)action);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &mdi_36, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return lbl;   // caller may want to reuse for play/pause label swap
}

lv_obj_t *media_banner_create(lv_obj_t *parent, media_action_cb_t on_action)
{
    lv_obj_t *cont = widget_container_create(parent, MB_W, MB_H);
    lv_obj_set_style_bg_color    (cont, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa      (cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius      (cont, 14, 0);
    lv_obj_set_style_pad_all     (cont, MB_PAD, 0);

    lv_obj_t *title = lv_label_create(cont);
    lv_obj_set_style_text_font(title, &jbm_bold_33, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_TEXT), 0);
    lv_label_set_text(title, "");
    lv_obj_set_width(title, MB_W - 2 * MB_PAD);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *artist = lv_label_create(cont);
    lv_obj_set_style_text_font(artist, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(artist, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(artist, "");
    lv_obj_set_width(artist, MB_W - 2 * MB_PAD);
    lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
    lv_obj_align(artist, LV_ALIGN_TOP_LEFT, 0, 39);

    mb_data_t *bd = lv_malloc(sizeof(mb_data_t));
    bd->title_label  = title;
    bd->artist_label = artist;
    bd->on_action    = on_action;
    bd->last_title [0] = '\0';
    bd->last_artist[0] = '\0';
    bd->last_state   = MEDIA_STATE_STOPPED;
    bd->last_visible = false;
    bd->has_value    = false;
    lv_obj_set_user_data(cont, bd);

    // Three transport buttons across the bottom row. Inner width is
    // MB_W - 2*MB_PAD = 364; 3 × 100 buttons + 2 × 32 gaps = 364.
    lv_obj_t *prev_lbl = make_btn(cont, ICON_PREV, bd, MEDIA_ACTION_PREV);
    lv_obj_align(lv_obj_get_parent(prev_lbl), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *pp_lbl = make_btn(cont, ICON_PAUSE, bd, MEDIA_ACTION_PLAY_PAUSE);
    lv_obj_align(lv_obj_get_parent(pp_lbl), LV_ALIGN_BOTTOM_MID, 0, 0);
    bd->btn_play_pause_label = pp_lbl;

    lv_obj_t *next_lbl = make_btn(cont, ICON_NEXT, bd, MEDIA_ACTION_NEXT);
    lv_obj_align(lv_obj_get_parent(next_lbl), LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    return cont;
}

void media_banner_update(lv_obj_t *cont, const now_playing_t *np, bool visible)
{
    mb_data_t *bd = lv_obj_get_user_data(cont);
    if (!bd || !np) return;

    if (visible != bd->last_visible) {
        if (visible) lv_obj_remove_flag(cont, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag   (cont, LV_OBJ_FLAG_HIDDEN);
        bd->last_visible = visible;
    }
    if (!visible) return;

    if (strcmp(np->title,  bd->last_title)  != 0) {
        lv_label_set_text(bd->title_label, np->title[0] ? np->title : "(unknown title)");
        memcpy(bd->last_title, np->title, sizeof(bd->last_title));
    }
    if (strcmp(np->artist, bd->last_artist) != 0) {
        lv_label_set_text(bd->artist_label, np->artist[0] ? np->artist : "(unknown artist)");
        memcpy(bd->last_artist, np->artist, sizeof(bd->last_artist));
    }
    if (np->state != bd->last_state) {
        lv_label_set_text(bd->btn_play_pause_label,
            np->state == MEDIA_STATE_PLAYING ? ICON_PAUSE : ICON_PLAY);
        bd->last_state = np->state;
    }
}
