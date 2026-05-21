#include "now_playing_display.h"
#include "theme.h"
#include "widget_util.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(jbm_bold_26);

#define NP_LINE_BUF  (MEDIA_FIELD_MAX * 2 + 8)

typedef struct {
    lv_obj_t *label;
    // Cache the raw artist + title fields instead of the formatted line.
    // Two cheap strcmps (each ≤ MEDIA_FIELD_MAX = 48 bytes) replace the
    // unconditional snprintf-then-compare pattern, so steady-state
    // frames where nothing's changed skip the format entirely.
    char      last_artist[MEDIA_FIELD_MAX];
    char      last_title [MEDIA_FIELD_MAX];
    bool      has_value;
} np_data_t;

// Render "Artist - Title". ASCII hyphen (not em-dash) because our
// JetBrains Mono Bold font subset doesn't include U+2014 — the glyph
// rendered as a missing-character box. Falls back gracefully when one
// field is empty so we don't show a stray separator on partial metadata.
static void format_line(const now_playing_t *np, char *out, size_t out_size)
{
    bool has_artist = np->artist[0] != '\0';
    bool has_title  = np->title[0]  != '\0';
    if (has_artist && has_title)      snprintf(out, out_size, "%s - %s", np->artist, np->title);
    else if (has_title)               snprintf(out, out_size, "%s", np->title);
    else if (has_artist)              snprintf(out, out_size, "%s", np->artist);
    else                              snprintf(out, out_size, "%s", "(playing)");
}

lv_obj_t *now_playing_display_create(lv_obj_t *parent)
{
    lv_obj_t *cont = widget_container_create(parent, LV_SIZE_CONTENT, 32);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl, &jbm_bold_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_label_set_text(lbl, "");
    lv_obj_center(lbl);

    np_data_t *nd = lv_malloc(sizeof(np_data_t));
    nd->label          = lbl;
    nd->last_artist[0] = '\0';
    nd->last_title[0]  = '\0';
    nd->has_value      = false;
    lv_obj_set_user_data(cont, nd);
    return cont;
}

void now_playing_display_set(lv_obj_t *cont, const now_playing_t *np)
{
    np_data_t *nd = lv_obj_get_user_data(cont);
    if (!nd || !np) return;

    if (nd->has_value
     && strcmp(np->artist, nd->last_artist) == 0
     && strcmp(np->title,  nd->last_title)  == 0) return;

    strncpy(nd->last_artist, np->artist, sizeof(nd->last_artist) - 1);
    nd->last_artist[sizeof(nd->last_artist) - 1] = '\0';
    strncpy(nd->last_title, np->title, sizeof(nd->last_title) - 1);
    nd->last_title[sizeof(nd->last_title) - 1] = '\0';
    nd->has_value = true;

    char buf[NP_LINE_BUF];
    format_line(np, buf, sizeof(buf));
    lv_label_set_text(nd->label, buf);
}
