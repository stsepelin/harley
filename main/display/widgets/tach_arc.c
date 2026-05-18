#include "tach_arc.h"
#include "lvgl.h"
#include "theme.h"
#include "smooth.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>

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
#define TRACK_DIA          (TAIL_OUTER_R * 2)
#define SCALE_DIA          (TAIL_OUTER_R * 2)
#define CURSOR_OUTER_R     TAIL_OUTER_R
#define CURSOR_LEN         60
#define CURSOR_INNER_R     (CURSOR_OUTER_R - CURSOR_LEN)
#define CURSOR_MID_R       ((CURSOR_INNER_R + CURSOR_OUTER_R) / 2)
#define LABEL_R            330              // labels sit inside the ticks

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
    lv_obj_t *scale;
    lv_obj_t *tail_arc;
    lv_obj_t *line_arc_normal;      // orange line over 0..REDLINE_RPM
    lv_obj_t *line_arc_redline;     // red line over REDLINE_RPM..RPM_MAX
    lv_obj_t *cursor_img;
    lv_obj_t *labels[MAJOR_LABEL_COUNT];
    int32_t  displayed_rpm;
    int32_t  last_applied_rpm;      // last value pushed through to LVGL
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
    const float redline_start_deg = (float)REDLINE_SPLIT_DEG;
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

                // Pure Gaussian bloom — the bright "line" itself is now a
                // separate solid arc on top, so the image just carries the
                // soft halo fading inward. Peak alpha capped so the bloom
                // doesn't compete with the solid line at the bezel.
                float a_f = 210.0f * expf(-d_in * d_in / two_sigma_sq);
                a = (a_f >= 255.0f) ? 255 : (uint8_t)(a_f + 0.5f);

                float angle_rad = atan2f(dy, dx);
                float angle_deg = angle_rad * 180.0f / (float)M_PI;
                if (angle_deg < 0.0f) angle_deg += 360.0f;
                bool is_redline = (angle_deg >= redline_start_deg)
                               || (angle_deg <= redline_end_wrapped);

                g = is_redline ? 0x00 : 0x66;       // orange / red bloom hue
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

static uint8_t        s_cursor_normal_data[CURSOR_IMG_W * CURSOR_IMG_H * 4];
static uint8_t        s_cursor_red_data[CURSOR_IMG_W * CURSOR_IMG_H * 4];
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

lv_obj_t *tach_arc_create(lv_obj_t *parent)
{
    glow_image_init();
    cursor_image_init();

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 800, 800);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Track: a thin gray "rail" at the bezel radius — the unselected half of
    // the main line. The orange / red line arcs draw on top in the selected
    // sweep range.
    lv_obj_t *track = make_arc(cont, TRACK_DIA, START_DEG, START_DEG + SWEEP_DEG);
    lv_obj_set_style_arc_color(track, lv_color_hex(VROD_RAIL), LV_PART_MAIN);
    lv_obj_set_style_arc_width(track, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(track, LV_OPA_TRANSP, LV_PART_INDICATOR);

    tach_data_t *td = lv_malloc(sizeof(tach_data_t));
    td->displayed_rpm = 0;
    td->last_applied_rpm = 0;
    td->last_redline = false;
    td->has_applied = false;

    // Glow tail
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

    // Solid lines at the bezel, value-clipped to current RPM. Drawn ABOVE
    // the gradient image so they stay crisp and fully opaque; cover the
    // gray rail in the selected portion. Two arcs split the colour: orange
    // up to REDLINE_RPM, red above it.
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

    // Cursor: single rotated image instead of stacked layers, so the gradient
    // is as smooth as the tail's.
    lv_obj_t *cursor = lv_image_create(cont);
    lv_image_set_src(cursor, &s_cursor_normal_dsc);
    lv_image_set_pivot(cursor, CURSOR_IMG_W / 2, CURSOR_IMG_H / 2);
    td->cursor_img = cursor;

    // Scale: ticks only (labels are drawn manually below for the zoom effect).
    lv_obj_t *scale = lv_scale_create(cont);
    lv_obj_set_size(scale, SCALE_DIA, SCALE_DIA);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(scale, 21);
    lv_scale_set_major_tick_every(scale, 4);
    lv_scale_set_range(scale, 0, RPM_MAX);
    lv_scale_set_angle_range(scale, SWEEP_DEG);
    lv_scale_set_rotation(scale, START_DEG);
    lv_scale_set_label_show(scale, false);    // we render labels ourselves
    lv_obj_remove_flag(scale, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scale, 0, 0);
    lv_obj_set_style_pad_all(scale, 0, 0);
    lv_obj_set_style_line_width(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(scale, lv_color_hex(VROD_TICK_MINOR), LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 3, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 18, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, lv_color_hex(VROD_TEXT), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 5, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 30, LV_PART_INDICATOR);
    lv_obj_center(scale);
    td->scale = scale;

    // Red redline section: colors the major and minor ticks above REDLINE_RPM.
    static lv_style_t s_redline_indicator;
    static lv_style_t s_redline_items;
    static lv_style_t s_redline_main;
    static bool s_redline_styled = false;
    if (!s_redline_styled) {
        lv_style_init(&s_redline_indicator);
        lv_style_set_line_color(&s_redline_indicator, lv_color_hex(VROD_RED_TICK));
        lv_style_init(&s_redline_items);
        lv_style_set_line_color(&s_redline_items, lv_color_hex(VROD_RED_TICK_DIM));
        lv_style_init(&s_redline_main);
        s_redline_styled = true;
    }
    lv_scale_section_t *redline = lv_scale_add_section(scale);
    lv_scale_section_set_range(redline, REDLINE_RPM, RPM_MAX);
    lv_scale_section_set_style(redline, LV_PART_INDICATOR, &s_redline_indicator);
    lv_scale_section_set_style(redline, LV_PART_ITEMS,     &s_redline_items);
    lv_scale_section_set_style(redline, LV_PART_MAIN,      &s_redline_main);

    // Manual labels: 6 lv_label widgets, scaled per-frame so the one nearest
    // the cursor grows to 2x. Color is red for the redline values (8, 10).
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
