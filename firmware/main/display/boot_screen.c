#include "boot_screen.h"
#include "ui_manager.h"
#include "theme.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "boot";

// Embedded GIF (assets/boot.gif, registered via EMBED_FILES in main/CMakeLists.txt).
extern const uint8_t boot_gif_start[] asm("_binary_boot_gif_start");
extern const uint8_t boot_gif_end[]   asm("_binary_boot_gif_end");

// Safety net: if LV_EVENT_READY never fires (GIF parse failure, malformed
// loop count, decoder stall), swap to the ride screen after this long.
#define BOOT_SAFETY_MS  15000

static lv_image_dsc_t  s_gif_dsc;
static lv_obj_t       *s_boot_scr     = NULL;
static lv_timer_t     *s_safety_timer = NULL;
static bool            s_handed_off   = false;

static void boot_hand_off(void)
{
    if (s_handed_off) return;
    s_handed_off = true;

    ui_manager_show_home();  // swap to the saved layout + start updates

    if (s_boot_scr) {
        lv_obj_delete(s_boot_scr);
        s_boot_scr = NULL;
    }
    if (s_safety_timer) {
        lv_timer_delete(s_safety_timer);
        s_safety_timer = NULL;
    }
}

// lv_gif sends LV_EVENT_READY once its loop_count reaches zero — i.e. after
// the GIF has actually finished playing. This is what we want, not a fixed
// wall-clock timer, since per-frame decode may run slower than designed.
static void boot_gif_ready_cb(lv_event_t *e)
{
    (void)e;
    boot_hand_off();
}

static void boot_safety_cb(lv_timer_t *t)
{
    (void)t;
    boot_hand_off();
}

static void show_text_fallback(lv_obj_t *scr)
{
    // Reuse JetBrains Mono Bold (already loaded for the gauge readouts)
    // instead of pulling Montserrat into the binary for this fallback —
    // it only runs if the embedded boot.gif fails to decode, which never
    // happens at runtime because the GIF is statically embedded.
    LV_FONT_DECLARE(jbm_bold_45);
    LV_FONT_DECLARE(jbm_bold_33);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Zeppl");
    lv_obj_set_style_text_color(title, lv_color_hex(VROD_ORANGE), 0);
    lv_obj_set_style_text_font(title, &jbm_bold_45, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "VRSCF MUSCLE");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(VROD_TEXT_DIM), 0);
    lv_obj_set_style_text_font(subtitle, &jbm_bold_33, 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 30);
}

void boot_screen_show(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    s_boot_scr = scr;

    const size_t gif_bytes = (size_t)(boot_gif_end - boot_gif_start);
    if (gif_bytes == 0) {
        ESP_LOGE(TAG, "embedded boot.gif is empty; using text splash");
        show_text_fallback(scr);
        s_safety_timer = lv_timer_create(boot_safety_cb, 2000, NULL);
        lv_timer_set_repeat_count(s_safety_timer, 1);
        return;
    }

    s_gif_dsc.data      = boot_gif_start;
    s_gif_dsc.data_size = gif_bytes;

    // GIF authored at native 800×800. The bundled AnimatedGIF decoder's
    // MAX_WIDTH was patched up from 480 → 1024 to allow this; running at
    // native resolution removes LVGL scaling work, which was the main
    // smoothness bottleneck.
    lv_obj_t *gif = lv_gif_create(scr);
    // RGB565 is what the panel actually wants; default ARGB8888 burns 2× the
    // PSRAM bandwidth in the cooked buffer and forces a per-pixel format
    // conversion on every blit. The boot animation has no alpha to spare.
    lv_gif_set_color_format(gif, LV_COLOR_FORMAT_RGB565);
    lv_gif_set_src(gif, &s_gif_dsc);
    if (!lv_gif_is_loaded(gif)) {
        ESP_LOGW(TAG, "boot.gif failed to decode (%u bytes)", (unsigned)gif_bytes);
    }
    lv_obj_center(gif);

    // Hand off the moment lv_gif reports the animation is complete.
    lv_obj_add_event_cb(gif, boot_gif_ready_cb, LV_EVENT_READY, NULL);

    // Backstop in case READY never fires (GIF parse error, decoder stall).
    s_safety_timer = lv_timer_create(boot_safety_cb, BOOT_SAFETY_MS, NULL);
    lv_timer_set_repeat_count(s_safety_timer, 1);
}
