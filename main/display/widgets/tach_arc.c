#include "tach_arc.h"
#include "lvgl.h"
#include "theme.h"
#include "smooth.h"
#include "widget_util.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

// JetBrains Mono Bold — monospaced, OFL. Used for tabular gauge labels.
LV_FONT_DECLARE(jbm_bold_45);

// RPM range + redline threshold are the source of truth; all angles, colours
// and sub-arc ranges below are derived from them.
#define RPM_MAX            10000
#define REDLINE_RPM        8000

// 270 deg sweep starting at 135 deg (bottom-left), wrapping clockwise through
// the top, ending at 405 deg (bottom-right). Open at bottom.
#define SWEEP_DEG          270
#define START_DEG          135
#define REDLINE_SPLIT_DEG  (START_DEG + (int)((int64_t)SWEEP_DEG * REDLINE_RPM / RPM_MAX))

// Container is 800x800 in local pixels. Visible bezel radius is 400.
#define CENTER_XY          400

// Tail glow ring image: soft Gaussian halo perpendicular to the bezel edge.
#define GLOW_IMG_SIZE      800
#define TAIL_OUTER_R       385
#define BLOOM_WIDTH        60
#define GLOW_SIGMA         22.0f
#define TAIL_ARC_DIA       (TAIL_OUTER_R * 2)
#define TAIL_ARC_WIDTH     BLOOM_WIDTH

// Track + cursor + labels all sit relative to TAIL_OUTER_R.
#define CURSOR_OUTER_R     TAIL_OUTER_R
#define CURSOR_LEN         60
#define CURSOR_INNER_R     (CURSOR_OUTER_R - CURSOR_LEN)
#define CURSOR_MID_R       ((CURSOR_INNER_R + CURSOR_OUTER_R) / 2)
#define LABEL_R            330              // labels sit inside the ticks

// Static background sprites. Bottom layer: gray track ring at the bezel.
// Top layer (above line arcs + cursor, below labels): 21 scale ticks
// with redline-section colours. Pre-baked once at boot into PSRAM —
// turns the per-frame lv_arc + lv_scale rendering into two ARGB blits.
#define BG_IMG_SIZE        800
#define TRACK_HALF_W       2          // yields 5-px ring width
#define TICK_MAJOR_W       5
#define TICK_MAJOR_L       30
#define TICK_MINOR_W       3
#define TICK_MINOR_L       18
#define TICK_COUNT         21
#define TICK_MAJOR_EVERY   4

// Solid "main line" along the bezel edge, drawn over the glow. Split at
// REDLINE_SPLIT_DEG into an orange normal-zone arc and a red redline-zone arc.
#define LINE_ARC_WIDTH       8

// Cursor sprite: small ARGB texture with a bright bar and perpendicular
// Gaussian glow, pill-shaped ends. A second sprite uses tighter sigma + red
// colours and gets swapped in when the cursor is past the redline.
#define CURSOR_IMG_W           32
#define CURSOR_IMG_H           72
#define CURSOR_SIGMA_NORMAL    6.0f
#define CURSOR_SIGMA_REDLINE   4.0f     // tighter halo for the warning state
#define CURSOR_END_TAPER       6

#define MAJOR_LABEL_COUNT  6

typedef struct {
    lv_obj_t *tail_arc;
    lv_obj_t *line_arc_normal;      // orange line over 0..REDLINE_RPM
    lv_obj_t *line_arc_redline;     // red line over REDLINE_RPM..RPM_MAX
    lv_obj_t *cursor_img;
    lv_obj_t *labels[MAJOR_LABEL_COUNT];
    int32_t  displayed_rpm;
    int32_t  last_applied_rpm;      // last value pushed through to LVGL
    int32_t  last_label_scale[MAJOR_LABEL_COUNT];  // per-label cache: skip unchanged transform_scale sets
    bool     last_redline;          // last cursor red-state we applied
    bool     has_applied;
} tach_data_t;

static const int32_t k_label_values[MAJOR_LABEL_COUNT] = {0, 2000, 4000, 6000, 8000, 10000};
static const char   *k_label_strs[MAJOR_LABEL_COUNT]   = {"0", "2", "4", "6", "8", "10"};

// --- Tail glow ring image --------------------------------------------------

static uint8_t       *s_glow_img_data = NULL;
static lv_image_dsc_t s_glow_img_dsc;

static bool glow_image_init(void)
{
    if (s_glow_img_data) return true;

    const size_t bytes = (size_t)GLOW_IMG_SIZE * GLOW_IMG_SIZE * 4;
    s_glow_img_data = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_glow_img_data) {
        ESP_LOGE("tach", "glow image alloc failed (%u bytes)", (unsigned)bytes);
        return false;
    }

    const float c = (GLOW_IMG_SIZE - 1) / 2.0f;
    const float two_sigma_sq = 2.0f * GLOW_SIGMA * GLOW_SIGMA;

    // Redline arc: last 20% of the sweep. Sweep 135..405; redline starts at
    // 135 + 270*0.8 = 351 deg. After wrap to 0..360: redline is 351..360 OR
    // 0..45.
    const float redline_start_deg   = (float)REDLINE_SPLIT_DEG;
    const float redline_end_wrapped = (float)START_DEG + (float)SWEEP_DEG - 360.0f;

    for (int y = 0; y < GLOW_IMG_SIZE; y++) {
        for (int x = 0; x < GLOW_IMG_SIZE; x++) {
            float dx = (float)x - c;
            float dy = (float)y - c;
            float d  = sqrtf(dx * dx + dy * dy);

            uint8_t a = 0;
            uint8_t r = 0xFF, g = 0x66, b = 0x00;
            if (d <= (float)TAIL_OUTER_R) {
                float d_in = (float)TAIL_OUTER_R - d;
                float a_f  = 210.0f * expf(-d_in * d_in / two_sigma_sq);
                a = (a_f >= 255.0f) ? 255 : (uint8_t)(a_f + 0.5f);

                float angle_rad = atan2f(dy, dx);
                float angle_deg = angle_rad * 180.0f / (float)M_PI;
                if (angle_deg < 0.0f) angle_deg += 360.0f;
                bool is_redline = (angle_deg >= redline_start_deg)
                               || (angle_deg <= redline_end_wrapped);

                g = is_redline ? 0x00 : 0x66;
                b = 0x00;
            }

            int idx = (y * GLOW_IMG_SIZE + x) * 4;
            s_glow_img_data[idx + 0] = b;
            s_glow_img_data[idx + 1] = g;
            s_glow_img_data[idx + 2] = r;
            s_glow_img_data[idx + 3] = a;
        }
    }

    s_glow_img_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_glow_img_dsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
    s_glow_img_dsc.header.w      = GLOW_IMG_SIZE;
    s_glow_img_dsc.header.h      = GLOW_IMG_SIZE;
    s_glow_img_dsc.header.stride = GLOW_IMG_SIZE * 4;
    s_glow_img_dsc.data_size     = bytes;
    s_glow_img_dsc.data          = s_glow_img_data;
    return true;
}

// --- Cursor sprite --------------------------------------------------------
// uint8_t arrays default to 1-byte alignment, but LVGL's draw-buf init
// checks the pointer against LV_DRAW_BUF_ALIGN (4 by default) and warns
// "Data is not aligned, ignored" if it isn't. Force 4-byte alignment.

static uint8_t        s_cursor_normal_data[CURSOR_IMG_W * CURSOR_IMG_H * 4] __attribute__((aligned(4)));
static uint8_t        s_cursor_red_data   [CURSOR_IMG_W * CURSOR_IMG_H * 4] __attribute__((aligned(4)));
static lv_image_dsc_t s_cursor_normal_dsc;
static lv_image_dsc_t s_cursor_red_dsc;
static bool           s_cursor_images_built = false;

// Pill-shaped bar with Gaussian falloff perpendicular to its long axis.
// core_g/core_b is the bright tip hue at eff=0 (blends into the inner
// colour over the first 3 px); inner_g/inner_b is the saturated tube hue
// at eff>=3 px.
static void bake_cursor_sprite(uint8_t *buf, lv_image_dsc_t *dsc,
                               float sigma,
                               uint8_t core_g, uint8_t core_b,
                               uint8_t inner_g, uint8_t inner_b)
{
    const float cx = (CURSOR_IMG_W - 1) / 2.0f;
    const float two_sigma_sq = 2.0f * sigma * sigma;
    const float bar_top    = (float)CURSOR_END_TAPER;
    const float bar_bottom = (float)(CURSOR_IMG_H - 1 - CURSOR_END_TAPER);

    for (int y = 0; y < CURSOR_IMG_H; y++) {
        for (int x = 0; x < CURSOR_IMG_W; x++) {
            float perp = fabsf((float)x - cx);
            float dy_over = 0.0f;
            if ((float)y < bar_top)         dy_over = bar_top - (float)y;
            else if ((float)y > bar_bottom) dy_over = (float)y - bar_bottom;
            float eff = sqrtf(perp * perp + dy_over * dy_over);

            float a_f;
            if (eff < 1.5f) {
                a_f = 255.0f;
            } else {
                float fade = eff - 1.5f;
                a_f = 255.0f * expf(-fade * fade / two_sigma_sq);
            }
            uint8_t a = (a_f >= 255.0f) ? 255 : (uint8_t)(a_f + 0.5f);

            uint8_t r = 0xFF, g, b;
            if (eff < 3.0f) {
                float t = eff / 3.0f;
                g = (uint8_t)(core_g + (inner_g - core_g) * t);
                b = (uint8_t)(core_b + (inner_b - core_b) * t);
            } else {
                g = inner_g;
                b = inner_b;
            }

            int idx = (y * CURSOR_IMG_W + x) * 4;
            buf[idx + 0] = b;
            buf[idx + 1] = g;
            buf[idx + 2] = r;
            buf[idx + 3] = a;
        }
    }

    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf     = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w      = CURSOR_IMG_W;
    dsc->header.h      = CURSOR_IMG_H;
    dsc->header.stride = CURSOR_IMG_W * 4;
    dsc->data_size     = CURSOR_IMG_W * CURSOR_IMG_H * 4;
    dsc->data          = buf;
}

// --- Static background sprites: track ring + scale ticks ------------------
//
// Both sprites are 800×800 ARGB8888 in PSRAM, mostly transparent. They
// replace the per-frame lv_arc track + lv_scale tick rendering with two
// cheap ARGB blits. Built once at boot; never invalidated thereafter.
//
// The track sprite goes at the BOTTOM of the tach (under the line arcs,
// which cover it in the active sweep). The ticks sprite goes ABOVE the
// line arcs so the full tick length still shows through.

static uint8_t       *s_track_img_data = NULL;
static lv_image_dsc_t s_track_img_dsc;
static uint8_t       *s_ticks_img_data = NULL;
static lv_image_dsc_t s_ticks_img_dsc;

static inline void put_px(uint8_t *buf, int x, int y, uint32_t argb_le)
{
    if (x < 0 || y < 0 || x >= BG_IMG_SIZE || y >= BG_IMG_SIZE) return;
    *(uint32_t *)(buf + (y * BG_IMG_SIZE + x) * 4) = argb_le;
}

// ARGB8888 stored little-endian: byte order in memory is B, G, R, A.
// Returning A<<24 | R<<16 | G<<8 | B writes those bytes correctly on
// the RISC-V (LE) target.
static inline uint32_t hex_to_argb(uint32_t hex_rgb)
{
    return 0xFF000000u | hex_rgb;
}

static void bake_bg_descriptor(lv_image_dsc_t *dsc, uint8_t *data)
{
    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf     = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w      = BG_IMG_SIZE;
    dsc->header.h      = BG_IMG_SIZE;
    dsc->header.stride = BG_IMG_SIZE * 4;
    dsc->data_size     = (size_t)BG_IMG_SIZE * BG_IMG_SIZE * 4;
    dsc->data          = data;
}

// Walk along the ring at a fine angular step (≈0.5 px arc length at the
// outer radius) so consecutive pixel coords overlap and the ring stays
// gap-free without per-pixel sqrt.
static void draw_track_ring(uint8_t *buf, uint32_t color)
{
    const float step_deg = (0.5f / (float)TAIL_OUTER_R) * 180.0f / (float)M_PI;
    for (float a = (float)START_DEG; a <= (float)(START_DEG + SWEEP_DEG); a += step_deg) {
        float ar = a * (float)M_PI / 180.0f;
        float cx_u = cosf(ar), cy_u = sinf(ar);
        for (int dr = -TRACK_HALF_W; dr <= TRACK_HALF_W; dr++) {
            int r  = TAIL_OUTER_R + dr;
            int px = CENTER_XY + (int)lroundf((float)r * cx_u);
            int py = CENTER_XY + (int)lroundf((float)r * cy_u);
            put_px(buf, px, py, color);
        }
    }
}

// Filled rectangle in tick-local space, rotated by angle_deg. The
// bounding box is small (≤31×31 even for a major tick), so a per-pixel
// scan is cheap and trivially correct.
static void draw_tick(uint8_t *buf, float angle_deg, int width, int length, uint32_t color)
{
    float ar    = angle_deg * (float)M_PI / 180.0f;
    float dir_x = cosf(ar), dir_y = sinf(ar);
    float per_x = -dir_y,   per_y = dir_x;
    float outer = (float)TAIL_OUTER_R;
    float inner = outer - (float)length;
    float halfw = (float)width / 2.0f;

    float corners_r[2] = {inner, outer};
    float corners_w[2] = {-halfw, +halfw};
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (int ci = 0; ci < 2; ci++) {
        for (int cj = 0; cj < 2; cj++) {
            float wx = corners_r[ci] * dir_x + corners_w[cj] * per_x;
            float wy = corners_r[ci] * dir_y + corners_w[cj] * per_y;
            if (wx < min_x) min_x = wx;
            if (wx > max_x) max_x = wx;
            if (wy < min_y) min_y = wy;
            if (wy > max_y) max_y = wy;
        }
    }
    int bb_x0 = (int)floorf(min_x) - 1, bb_x1 = (int)ceilf(max_x) + 1;
    int bb_y0 = (int)floorf(min_y) - 1, bb_y1 = (int)ceilf(max_y) + 1;

    for (int dy = bb_y0; dy <= bb_y1; dy++) {
        for (int dx = bb_x0; dx <= bb_x1; dx++) {
            float rad  = (float)dx * dir_x + (float)dy * dir_y;
            float perp = (float)dx * per_x + (float)dy * per_y;
            if (rad < inner || rad > outer) continue;
            if (fabsf(perp) > halfw)        continue;
            put_px(buf, CENTER_XY + dx, CENTER_XY + dy, color);
        }
    }
}

static bool track_image_init(void)
{
    if (s_track_img_data) return true;
    const size_t bytes = (size_t)BG_IMG_SIZE * BG_IMG_SIZE * 4;
    s_track_img_data = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_track_img_data) {
        ESP_LOGE("tach", "track sprite alloc failed (%u bytes)", (unsigned)bytes);
        return false;
    }
    memset(s_track_img_data, 0, bytes);
    draw_track_ring(s_track_img_data, hex_to_argb(VROD_RAIL));
    bake_bg_descriptor(&s_track_img_dsc, s_track_img_data);
    return true;
}

static bool ticks_image_init(void)
{
    if (s_ticks_img_data) return true;
    const size_t bytes = (size_t)BG_IMG_SIZE * BG_IMG_SIZE * 4;
    s_ticks_img_data = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ticks_img_data) {
        ESP_LOGE("tach", "ticks sprite alloc failed (%u bytes)", (unsigned)bytes);
        return false;
    }
    memset(s_ticks_img_data, 0, bytes);

    for (int i = 0; i < TICK_COUNT; i++) {
        float angle = (float)START_DEG
                    + ((float)i / (float)(TICK_COUNT - 1)) * (float)SWEEP_DEG;
        bool major   = (i % TICK_MAJOR_EVERY) == 0;
        int  rpm     = i * RPM_MAX / (TICK_COUNT - 1);
        bool redline = rpm >= REDLINE_RPM;
        uint32_t hex = major
            ? (redline ? VROD_RED_TICK     : VROD_TEXT)
            : (redline ? VROD_RED_TICK_DIM : VROD_TICK_MINOR);
        draw_tick(s_ticks_img_data, angle,
                  major ? TICK_MAJOR_W : TICK_MINOR_W,
                  major ? TICK_MAJOR_L : TICK_MINOR_L,
                  hex_to_argb(hex));
    }

    bake_bg_descriptor(&s_ticks_img_dsc, s_ticks_img_data);
    return true;
}

static void cursor_image_init(void)
{
    if (s_cursor_images_built) return;
    // Normal: warm near-white core (#FFEECC), orange inner tube, soft sigma.
    bake_cursor_sprite(s_cursor_normal_data, &s_cursor_normal_dsc,
                       CURSOR_SIGMA_NORMAL,
                       0xEE, 0xCC,    // bright core hue
                       0x66, 0x00);   // inner tube hue
    // Redline: pinkish-red core (#FFAA88) instead of near-white so the cursor
    // reads more red overall. Pure-red inner + tighter sigma keep the halo
    // focused around it.
    bake_cursor_sprite(s_cursor_red_data, &s_cursor_red_dsc,
                       CURSOR_SIGMA_REDLINE,
                       0xAA, 0x88,    // pinkish-red bright core
                       0x00, 0x00);   // pure red inner
    s_cursor_images_built = true;
}

static void cursor_set_state(tach_data_t *td, int32_t value, bool redline)
{
    if (value < 0) value = 0;
    if (value > RPM_MAX) value = RPM_MAX;

    float angle_deg = (float)START_DEG + (float)SWEEP_DEG * value / (float)RPM_MAX;
    float angle_rad = angle_deg * (float)M_PI / 180.0f;
    int px = CENTER_XY + (int)(CURSOR_MID_R * cosf(angle_rad));
    int py = CENTER_XY + (int)(CURSOR_MID_R * sinf(angle_rad));

    lv_obj_set_pos(td->cursor_img, px - CURSOR_IMG_W / 2, py - CURSOR_IMG_H / 2);

    // Image's natural Y axis points down (LVGL angle 90). To align with the
    // radial direction at angle_deg: rotation = angle_deg - 90.
    int rot_x10 = (int)((angle_deg - 90.0f) * 10.0f);
    while (rot_x10 < 0)     rot_x10 += 3600;
    while (rot_x10 >= 3600) rot_x10 -= 3600;
    lv_image_set_rotation(td->cursor_img, rot_x10);

    // Swap to the pre-baked red sprite (tighter glow, baked-in red halo)
    // when the cursor enters the warning zone.
    lv_image_set_src(td->cursor_img,
                     redline ? &s_cursor_red_dsc : &s_cursor_normal_dsc);
}

// --- Manual labels with smooth zoom near the cursor -----------------------

static void labels_update(tach_data_t *td, int32_t value)
{
    float cursor_angle = (float)START_DEG + (float)SWEEP_DEG * value / (float)RPM_MAX;

    for (int i = 0; i < MAJOR_LABEL_COUNT; i++) {
        float label_angle = (float)START_DEG
            + (float)SWEEP_DEG * k_label_values[i] / (float)RPM_MAX;
        float dist = fabsf(cursor_angle - label_angle);

        // Smoothstep curve: full 2x scale at cursor angle, fading back to 1x
        // by four tick spans away (~54 deg) so the zoom starts earlier and
        // feels less abrupt.
        const float falloff_deg = 54.0f;
        float t = dist / falloff_deg;
        if (t > 1.0f) t = 1.0f;
        float smooth_t = t * t * (3.0f - 2.0f * t);
        float scale = 2.0f - 1.0f * smooth_t;

        int scale_int = (int)(scale * 256.0f + 0.5f);
        // Skip set_style on labels whose scale didn't change since last
        // frame. During a hard RPM transient (e.g. WOT → cruise) only 1-2
        // labels are near the cursor and actually change scale; the other
        // four stay locked at 1.0× but were re-invalidated every frame.
        // 6 labels × 2 styles × ~15 transient frames = 180 redundant
        // invalidations per WOT exit — the user could feel them.
        if (scale_int == td->last_label_scale[i]) continue;
        td->last_label_scale[i] = scale_int;
        lv_obj_set_style_transform_scale_x(td->labels[i], scale_int, 0);
        lv_obj_set_style_transform_scale_y(td->labels[i], scale_int, 0);
    }
}

// --- Widget construction --------------------------------------------------

// Bare-bones decorative arc: no knob, no click, value=0, centered in parent.
// Caller styles MAIN/INDICATOR parts and sets the range explicitly.
static lv_obj_t *make_arc(lv_obj_t *parent, int dia, int start_deg, int end_deg)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, dia, dia);
    lv_arc_set_bg_angles(arc, start_deg, end_deg);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(arc);
    return arc;
}

// --- Sub-builders for the tach widget --------------------------------------
// Each makes one visible element of the tach and (where needed) stashes the
// child obj into the tach_data_t. tach_arc_create just calls them in order
// so the function reads like a checklist.

// Pre-baked gray rail at the bezel — the unselected half of the main line.
// The orange/red line arcs draw on top in the selected sweep range.
// Sprite created once in track_image_init().
static void build_track_image(lv_obj_t *cont)
{
    lv_obj_t *img = lv_image_create(cont);
    lv_image_set_src(img, &s_track_img_dsc);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(img);
}

// Glow tail — a pre-baked Gaussian ARGB image painted as the arc's
// indicator brush. Falls back to a flat orange if the image alloc failed.
static void build_tail_glow(lv_obj_t *cont, tach_data_t *td)
{
    lv_obj_t *tail = make_arc(cont, TAIL_ARC_DIA, START_DEG, START_DEG + SWEEP_DEG);
    lv_arc_set_range(tail, 0, RPM_MAX);
    lv_obj_set_style_arc_opa(tail, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(tail, TAIL_ARC_WIDTH, LV_PART_INDICATOR);
    if (s_glow_img_data) {
        lv_obj_set_style_arc_image_src(tail, &s_glow_img_dsc, LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_arc_color(tail, lv_color_hex(VROD_ORANGE), LV_PART_INDICATOR);
    }
    lv_obj_set_style_arc_rounded(tail, false, LV_PART_INDICATOR);
    td->tail_arc = tail;
}

// Solid orange + red lines at the bezel, value-clipped to current RPM.
// Drawn above the gradient so they stay crisp and fully opaque; cover the
// gray rail in the selected portion. Two arcs split the colour: orange up
// to REDLINE_RPM, red above it.
static void build_line_arcs(lv_obj_t *cont, tach_data_t *td)
{
    for (int variant = 0; variant < 2; variant++) {
        bool is_red = (variant == 1);
        int start = is_red ? REDLINE_SPLIT_DEG : START_DEG;
        int end   = is_red ? (START_DEG + SWEEP_DEG) : REDLINE_SPLIT_DEG;
        lv_obj_t *line = make_arc(cont, TAIL_OUTER_R * 2, start, end);
        lv_arc_set_range(line,
                         is_red ? REDLINE_RPM : 0,
                         is_red ? RPM_MAX : REDLINE_RPM);
        lv_obj_set_style_arc_opa(line, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_color(line,
            lv_color_hex(is_red ? VROD_RED : VROD_ORANGE),
            LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(line, LINE_ARC_WIDTH, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(line, false, LV_PART_INDICATOR);
        if (is_red) td->line_arc_redline = line;
        else        td->line_arc_normal  = line;
    }
}

// Cursor: a single rotated image (not stacked layers), so the gradient is
// as smooth as the tail's. Position + rotation are set per-frame by
// cursor_set_state().
static void build_cursor(lv_obj_t *cont, tach_data_t *td)
{
    lv_obj_t *cursor = lv_image_create(cont);
    lv_image_set_src(cursor, &s_cursor_normal_dsc);
    lv_image_set_pivot(cursor, CURSOR_IMG_W / 2, CURSOR_IMG_H / 2);
    td->cursor_img = cursor;
}

// Pre-baked 21-tick scale (major every 4, redline subset coloured red).
// Sprite created once in ticks_image_init(); sits above the line arcs +
// cursor so the full tick length shows through.
static void build_ticks_image(lv_obj_t *cont)
{
    lv_obj_t *img = lv_image_create(cont);
    lv_image_set_src(img, &s_ticks_img_dsc);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(img);
}

// Manual labels: 6 lv_label widgets at the major tick positions, scaled
// per-frame by labels_update() so the one nearest the cursor grows to 2x.
// Colour is red for redline values (8, 10).
static void build_labels(lv_obj_t *cont, tach_data_t *td)
{
    for (int i = 0; i < MAJOR_LABEL_COUNT; i++) {
        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, k_label_strs[i]);
        uint32_t color = (k_label_values[i] >= REDLINE_RPM) ? VROD_RED_TICK : VROD_TEXT;
        lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
        lv_obj_set_style_text_font(lbl, &jbm_bold_45, 0);
        lv_obj_set_style_transform_pivot_x(lbl, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(lbl, LV_PCT(50), 0);

        float angle_rad = ((float)START_DEG
            + (float)SWEEP_DEG * k_label_values[i] / (float)RPM_MAX)
            * (float)M_PI / 180.0f;
        int px = CENTER_XY + (int)(LABEL_R * cosf(angle_rad));
        int py = CENTER_XY + (int)(LABEL_R * sinf(angle_rad));
        lv_obj_align(lbl, LV_ALIGN_CENTER, px - CENTER_XY, py - CENTER_XY);
        td->labels[i] = lbl;
    }
}

lv_obj_t *tach_arc_create(lv_obj_t *parent)
{
    glow_image_init();
    cursor_image_init();
    track_image_init();
    ticks_image_init();

    lv_obj_t *cont = widget_container_create(parent, 800, 800);

    tach_data_t *td = lv_malloc(sizeof(tach_data_t));
    td->displayed_rpm    = 0;
    td->last_applied_rpm = 0;
    td->last_redline     = false;
    td->has_applied      = false;
    for (int i = 0; i < MAJOR_LABEL_COUNT; i++) td->last_label_scale[i] = -1;

    // Z-order matters: track at the bottom, then glow, then the dynamic
    // line arcs + cursor, then the ticks sprite above them (so ticks
    // stay visible through the lines), then the animated labels on top.
    build_track_image(cont);
    build_tail_glow(cont, td);
    build_line_arcs(cont, td);
    build_cursor(cont, td);
    build_ticks_image(cont);
    build_labels(cont, td);

    lv_obj_set_user_data(cont, td);
    cursor_set_state(td, 0, false);
    labels_update(td, 0);
    return cont;
}

void tach_arc_set_value(lv_obj_t *cont, uint16_t target_rpm)
{
    tach_data_t *td = lv_obj_get_user_data(cont);
    if (!td) return;
    if (target_rpm > RPM_MAX) target_rpm = RPM_MAX;

    td->displayed_rpm = smooth_step(td->displayed_rpm, (int32_t)target_rpm);

    // Skip the (relatively expensive) arc/cursor/label updates when nothing
    // visible has changed since the last frame. Once smoothing has caught
    // up to a steady target_rpm, this turns the entire tach update into a
    // no-op until the rider's input moves again.
    bool redline = target_rpm > REDLINE_RPM;
    if (td->has_applied
        && td->last_applied_rpm == td->displayed_rpm
        && td->last_redline == redline) {
        return;
    }
    td->last_applied_rpm = td->displayed_rpm;
    td->last_redline = redline;
    td->has_applied = true;

    lv_arc_set_value(td->tail_arc, td->displayed_rpm);

    // The normal-zone arc caps at REDLINE_RPM; the redline-zone arc takes
    // over past it.
    int32_t normal_v = td->displayed_rpm;
    if (normal_v > REDLINE_RPM) normal_v = REDLINE_RPM;
    lv_arc_set_value(td->line_arc_normal, normal_v);
    int32_t redline_v = td->displayed_rpm;
    if (redline_v < REDLINE_RPM) redline_v = REDLINE_RPM;
    lv_arc_set_value(td->line_arc_redline, redline_v);

    cursor_set_state(td, td->displayed_rpm, redline);
    labels_update(td, td->displayed_rpm);
}
