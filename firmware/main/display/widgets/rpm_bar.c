#include "rpm_bar.h"
#include "rpm_scale.h"
#include "lvgl.h"
#include "theme.h"
#include "sprite_raster.h"
#include "widget_util.h"
#include "esp_heap_caps.h"
#include <string.h>

// Compact shift-light RPM bar: a row of RPM_SCALE_SEGMENTS rounded segments,
// lit left-to-right with the current rpm (orange, red in the redline zone) over
// a dim rail. Rendered as ONE baked ARGB image and re-baked only when the lit
// segment count changes -- the same bake-on-change strategy as fuel_arc, so the
// per-frame cost is a single blit and the device's DSI path never sees a swarm
// of small object redraws.
#define BAR_W   560
#define BAR_H   22
#define SEG_GAP 4
#define SEG_CR  5.0f  // rounded-rect corner radius, like the tach redline segments

typedef struct {
    lv_obj_t      *img;
    uint8_t       *buf;  // ARGB8888, redrawn when the lit count changes
    lv_image_dsc_t dsc;
    int            last_lit;
    bool           has_value;
} rpm_data_t;

static void bake_bar(rpm_data_t *rd, int lit)
{
    memset(rd->buf, 0, (size_t)BAR_W * BAR_H * 4);
    int         redline = rpm_scale_redline_seg();
    const float seg_w   = (float)(BAR_W - (RPM_SCALE_SEGMENTS - 1) * SEG_GAP) / RPM_SCALE_SEGMENTS;
    const float half_w  = BAR_H / 2.0f;
    const float cy      = BAR_H / 2.0f;
    for (int i = 0; i < RPM_SCALE_SEGMENTS; i++) {
        float    sx  = i * (seg_w + SEG_GAP);
        float    ccx = sx + seg_w / 2.0f;
        uint32_t col = (i < lit) ? ((i >= redline) ? VROD_RED_BRIGHT : VROD_ORANGE) : VROD_RAIL;
        uint8_t  cb = col & 0xFF, cg = (col >> 8) & 0xFF, cr = (col >> 16) & 0xFF;
        // Solid rounded-rectangle chunk (redline-segment style), AA via the
        // shared rounded-box SDF in (x,y) space.
        int x0 = (int)sx - 1, x1 = (int)(sx + seg_w) + 1;
        for (int y = 0; y < BAR_H; y++) {
            for (int x = x0; x <= x1; x++) {
                if (x < 0 || x >= BAR_W)
                    continue;
                float cov = sprite_arc_seg_cov((float)x + 0.5f - ccx, (float)y + 0.5f - cy,
                                               seg_w / 2.0f, half_w, SEG_CR);
                if (cov <= 0.0f)
                    continue;
                int idx          = (y * BAR_W + x) * 4;
                rd->buf[idx + 0] = cb;
                rd->buf[idx + 1] = cg;
                rd->buf[idx + 2] = cr;
                rd->buf[idx + 3] = (uint8_t)(cov * 255.0f + 0.5f);
            }
        }
    }
}

lv_obj_t *rpm_bar_create(lv_obj_t *parent)
{
    lv_obj_t *cont = widget_container_create(parent, BAR_W, BAR_H);

    rpm_data_t *rd = lv_malloc(sizeof(rpm_data_t));
    memset(rd, 0, sizeof(*rd));
    rd->last_lit  = 0;
    rd->has_value = false;
    rd->buf = heap_caps_malloc((size_t)BAR_W * BAR_H * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    lv_obj_t *img = lv_image_create(cont);
    if (rd->buf) {
        bake_bar(rd, 0);
        sprite_dsc_init_argb(&rd->dsc, rd->buf, BAR_W, BAR_H);
        lv_image_set_src(img, &rd->dsc);
    }
    lv_obj_set_pos(img, 0, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    rd->img = img;

    lv_obj_set_user_data(cont, rd);
    return cont;
}

void rpm_bar_set_rpm(lv_obj_t *cont, int rpm)
{
    rpm_data_t *rd = lv_obj_get_user_data(cont);
    if (!rd)
        return;
    int lit = rpm_scale_lit(rpm);
    if (rd->has_value && rd->last_lit == lit)
        return;
    rd->last_lit  = lit;
    rd->has_value = true;
    if (rd->buf) {
        bake_bar(rd, lit);
        lv_obj_invalidate(rd->img);
    }
}
