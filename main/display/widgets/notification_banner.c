#include "notification_banner.h"
#include "lvgl.h"
#include "theme.h"
#include "widget_util.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(jbm_bold_26);
LV_FONT_DECLARE(jbm_bold_33);

// Banner geometry. Bottom-anchored in screen_ride so the bottom edge
// stays at the same y regardless of height; the kind drives which
// height we render at. Width is fixed and chosen so that the bottom
// corners stay inside the round bezel at y≈740 (|dy|≈340 → max half-
// width ≈ 211, so 400 wide fits).
#define BANNER_W           400
#define BANNER_H_CALL      210   // top row + message + button row
#define BANNER_H_INFO      120   // top row + message only (SMS / app)
#define BTN_W              160
#define BTN_H              60
#define BANNER_PAD         18

// Banner mode — drives which row of buttons / what message format is shown.
typedef enum {
    BANNER_MODE_NONE,           // hidden
    BANNER_MODE_INFO,           // SMS / app push — no buttons
    BANNER_MODE_INCOMING_CALL,  // REJECT + ACCEPT
    BANNER_MODE_ACTIVE_CALL,    // END CALL + running duration
} banner_mode_t;

typedef struct {
    lv_obj_t              *kind_label;
    lv_obj_t              *sender_label;
    lv_obj_t              *message_label;
    lv_obj_t              *btn_accept;
    lv_obj_t              *btn_reject;
    lv_obj_t              *btn_end_call;
    notif_call_action_cb_t on_call_action;
    bool                   last_active;
    uint32_t               last_id;
    notif_kind_t           last_kind;
    banner_mode_t          last_mode;
    uint32_t               last_call_sec;
    char                   last_sender [NOTIF_SENDER_MAX];
    char                   last_message[NOTIF_MSG_MAX];
} banner_data_t;

static const char *kind_text(banner_mode_t mode, notif_kind_t k)
{
    if (mode == BANNER_MODE_ACTIVE_CALL) return "IN CALL";
    switch (k) {
    case NOTIF_KIND_CALL: return "CALL";
    case NOTIF_KIND_SMS:  return "SMS";
    case NOTIF_KIND_APP:
    default:              return "MSG";
    }
}

static uint32_t kind_color(banner_mode_t mode, notif_kind_t k)
{
    if (mode == BANNER_MODE_ACTIVE_CALL) return VROD_GREEN_SIGNAL;
    switch (k) {
    case NOTIF_KIND_CALL: return VROD_GREEN_SIGNAL;
    case NOTIF_KIND_SMS:  return VROD_ORANGE;
    case NOTIF_KIND_APP:
    default:              return VROD_TEXT_DIM;
    }
}

static void on_btn_clicked(lv_event_t *e)
{
    // Banner is stashed as the button's user-data; the action enum is
    // on the event's user-data slot.
    lv_obj_t      *btn    = lv_event_get_target(e);
    banner_data_t *bd     = lv_obj_get_user_data(btn);
    call_action_t  action = (call_action_t)(intptr_t)lv_event_get_user_data(e);
    if (bd && bd->on_call_action) bd->on_call_action(action);
}

lv_obj_t *notification_banner_create(lv_obj_t *parent, notif_call_action_cb_t on_call_action)
{
    lv_obj_t *cont = widget_container_create(parent, BANNER_W, BANNER_H_INFO);
    lv_obj_set_style_bg_color    (cont, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa      (cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius      (cont, 14, 0);
    lv_obj_set_style_pad_all     (cont, BANNER_PAD, 0);

    // Top row: kind tag (left) + sender (right). Both jbm_bold_33 so
    // their baselines line up — earlier sender@45 made the row feel
    // off-balance with the smaller kind label.
    lv_obj_t *kind = lv_label_create(cont);
    lv_obj_set_style_text_font(kind, &jbm_bold_33, 0);
    lv_obj_set_style_text_color(kind, lv_color_hex(VROD_ORANGE), 0);
    lv_label_set_text(kind, "");
    lv_obj_align(kind, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *sender = lv_label_create(cont);
    lv_obj_set_style_text_font(sender, &jbm_bold_33, 0);
    lv_obj_set_style_text_color(sender, lv_color_hex(VROD_TEXT), 0);
    lv_label_set_text(sender, "");
    lv_obj_align(sender, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Message body. Anchored below the top row (not centred), so on a
    // 1-line message it doesn't drift into the button row underneath.
    lv_obj_t *message = lv_label_create(cont);
    lv_obj_set_style_text_font(message, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(message, "");
    lv_obj_set_width(message, BANNER_W - 2 * BANNER_PAD);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_align(message, LV_ALIGN_TOP_LEFT, 0, 46);

    // Call action buttons. All start hidden; update() reveals the right
    // pair (incoming vs active) when a CALL notification is active.
    lv_obj_t *reject = lv_button_create(cont);
    lv_obj_set_size(reject, BTN_W, BTN_H);
    lv_obj_align(reject, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(reject, lv_color_hex(VROD_RED_BRIGHT), 0);
    lv_obj_set_style_radius(reject, 12, 0);
    lv_obj_add_flag(reject, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *reject_lbl = lv_label_create(reject);
    lv_label_set_text(reject_lbl, "REJECT");
    lv_obj_set_style_text_font(reject_lbl, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(reject_lbl, lv_color_white(), 0);
    lv_obj_center(reject_lbl);

    lv_obj_t *accept = lv_button_create(cont);
    lv_obj_set_size(accept, BTN_W, BTN_H);
    lv_obj_align(accept, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(accept, lv_color_hex(VROD_GREEN_SIGNAL), 0);
    lv_obj_set_style_radius(accept, 12, 0);
    lv_obj_add_flag(accept, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *accept_lbl = lv_label_create(accept);
    lv_label_set_text(accept_lbl, "ACCEPT");
    lv_obj_set_style_text_font(accept_lbl, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(accept_lbl, lv_color_black(), 0);
    lv_obj_center(accept_lbl);

    // Wide end-call button used in the active-call layout. Spans the
    // full inner width so it's hard to miss in a glove.
    lv_obj_t *end_call = lv_button_create(cont);
    lv_obj_set_size(end_call, BANNER_W - 2 * BANNER_PAD, BTN_H);
    lv_obj_align(end_call, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(end_call, lv_color_hex(VROD_RED_BRIGHT), 0);
    lv_obj_set_style_radius(end_call, 12, 0);
    lv_obj_add_flag(end_call, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *end_lbl = lv_label_create(end_call);
    lv_label_set_text(end_lbl, "END CALL");
    lv_obj_set_style_text_font(end_lbl, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(end_lbl, lv_color_white(), 0);
    lv_obj_center(end_lbl);

    banner_data_t *bd = lv_malloc(sizeof(banner_data_t));
    bd->kind_label     = kind;
    bd->sender_label   = sender;
    bd->message_label  = message;
    bd->btn_accept     = accept;
    bd->btn_reject     = reject;
    bd->btn_end_call   = end_call;
    bd->on_call_action = on_call_action;
    bd->last_active    = false;
    bd->last_id        = 0;
    bd->last_kind      = NOTIF_KIND_APP;
    bd->last_mode      = BANNER_MODE_NONE;
    bd->last_call_sec  = (uint32_t)-1;
    bd->last_sender [0] = '\0';
    bd->last_message[0] = '\0';
    lv_obj_set_user_data(cont, bd);

    lv_obj_set_user_data(reject,   bd);
    lv_obj_set_user_data(accept,   bd);
    lv_obj_set_user_data(end_call, bd);
    lv_obj_add_event_cb(reject,   on_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)CALL_ACTION_REJECT);
    lv_obj_add_event_cb(accept,   on_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)CALL_ACTION_ACCEPT);
    lv_obj_add_event_cb(end_call, on_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)CALL_ACTION_END);

    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    return cont;
}

static banner_mode_t mode_for(const notification_t *notif)
{
    if (!notif->active) return BANNER_MODE_NONE;
    if (notif->kind == NOTIF_KIND_CALL) {
        return notif->call_in_progress ? BANNER_MODE_ACTIVE_CALL
                                       : BANNER_MODE_INCOMING_CALL;
    }
    return BANNER_MODE_INFO;
}

static void apply_mode(lv_obj_t *cont, banner_data_t *bd, banner_mode_t mode)
{
    // Resize + toggle the right button row. Edge-triggered so we don't
    // re-invalidate the layout every frame.
    switch (mode) {
    case BANNER_MODE_INCOMING_CALL:
        lv_obj_set_height(cont, BANNER_H_CALL);
        lv_obj_remove_flag(bd->btn_accept, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(bd->btn_reject, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag   (bd->btn_end_call, LV_OBJ_FLAG_HIDDEN);
        break;
    case BANNER_MODE_ACTIVE_CALL:
        lv_obj_set_height(cont, BANNER_H_CALL);
        lv_obj_add_flag   (bd->btn_accept, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag   (bd->btn_reject, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(bd->btn_end_call, LV_OBJ_FLAG_HIDDEN);
        break;
    case BANNER_MODE_INFO:
    default:
        lv_obj_set_height(cont, BANNER_H_INFO);
        lv_obj_add_flag   (bd->btn_accept,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag   (bd->btn_reject,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag   (bd->btn_end_call, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void notification_banner_update(lv_obj_t *cont, const notification_t *notif)
{
    banner_data_t *bd = lv_obj_get_user_data(cont);
    if (!bd || !notif) return;

    banner_mode_t mode = mode_for(notif);

    if (notif->active != bd->last_active) {
        if (notif->active) lv_obj_remove_flag(cont, LV_OBJ_FLAG_HIDDEN);
        else               lv_obj_add_flag   (cont, LV_OBJ_FLAG_HIDDEN);
        bd->last_active = notif->active;
    }
    if (!notif->active) {
        bd->last_mode = BANNER_MODE_NONE;
        return;
    }

    // Mode transitions (incoming → active, or kind change) update the
    // banner geometry + button visibility. We also clear the labels
    // here so a stale value (e.g. the MM:SS timer from a just-ended
    // call) can't survive into the next mode if the new message text
    // happens to match the cached one.
    if (mode != bd->last_mode) {
        apply_mode(cont, bd, mode);
        lv_label_set_text(bd->message_label, "");
        bd->last_kind       = (notif_kind_t)-1;
        bd->last_message[0] = '\0';
        bd->last_call_sec   = (uint32_t)-1;
        bd->last_mode       = mode;
    }

    // Kind tag (CALL / IN CALL / SMS / MSG) + colour.
    if (notif->kind != bd->last_kind) {
        lv_label_set_text(bd->kind_label, kind_text(mode, notif->kind));
        lv_obj_set_style_text_color(bd->kind_label,
            lv_color_hex(kind_color(mode, notif->kind)), 0);
        bd->last_kind = notif->kind;
    }

    // Sender.
    if (strcmp(notif->sender, bd->last_sender) != 0) {
        lv_label_set_text(bd->sender_label, notif->sender);
        memcpy(bd->last_sender, notif->sender, sizeof(bd->last_sender));
    }

    // Message slot: in active-call mode it's a running MM:SS timer.
    if (mode == BANNER_MODE_ACTIVE_CALL) {
        uint32_t elapsed_sec = lv_tick_elaps(notif->call_start_ms) / 1000u;
        if (elapsed_sec != bd->last_call_sec) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%lu:%02lu",
                     (unsigned long)(elapsed_sec / 60u),
                     (unsigned long)(elapsed_sec % 60u));
            lv_label_set_text(bd->message_label, buf);
            bd->last_call_sec = elapsed_sec;
        }
    } else if (strcmp(notif->message, bd->last_message) != 0) {
        lv_label_set_text(bd->message_label, notif->message);
        memcpy(bd->last_message, notif->message, sizeof(bd->last_message));
    }

    bd->last_id = notif->id;
}
