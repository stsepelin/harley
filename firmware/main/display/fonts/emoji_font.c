#include "emoji_font.h"
#include "lvgl.h"

// Logging shim: ESP-IDF on the cluster, fprintf on the desktop sim and
// host tests. Both share the rest of the implementation below.
#ifdef ESP_PLATFORM
#  include "esp_log.h"
#  define EMOJI_LOG_TAG "emoji_font"
#  define EMOJI_LOG_E(fmt, ...) ESP_LOGE(EMOJI_LOG_TAG, fmt, ##__VA_ARGS__)
#  define EMOJI_LOG_I(fmt, ...) ESP_LOGI(EMOJI_LOG_TAG, fmt, ##__VA_ARGS__)
#  define EMOJI_LOG_W(fmt, ...) ESP_LOGW(EMOJI_LOG_TAG, fmt, ##__VA_ARGS__)
#else
#  include <stdio.h>
#  define EMOJI_LOG_E(fmt, ...) fprintf(stderr, "[emoji_font E] " fmt "\n", ##__VA_ARGS__)
#  define EMOJI_LOG_I(fmt, ...) fprintf(stderr, "[emoji_font I] " fmt "\n", ##__VA_ARGS__)
#  define EMOJI_LOG_W(fmt, ...) fprintf(stderr, "[emoji_font W] " fmt "\n", ##__VA_ARGS__)
#endif

#if LV_USE_FREETYPE

// The two body fonts grow a .fallback at runtime — both are made mutable
// by the manual const-strip in jbm_bold_*.c (see firmware/CLAUDE.md).
LV_FONT_DECLARE(jbm_bold_26);
LV_FONT_DECLARE(jbm_bold_33);

// Per-platform path provisioning. On firmware the TTF is embedded via
// EMBED_FILES and we hand FreeType a memfs-wrapped pointer (avoids
// needing a filesystem). On the host the TTF lives at EMOJI_TTF_PATH
// on disk and FreeType opens it directly via its own libc port.
#ifdef ESP_PLATFORM

#  if !LV_USE_FS_MEMFS
#    error "ESP build requires LV_USE_FS_MEMFS for the embedded-buffer path wrapper"
#  endif

extern const uint8_t emoji_ttf_start[] asm("_binary_emoji_ttf_start");
extern const uint8_t emoji_ttf_end[]   asm("_binary_emoji_ttf_end");

static lv_fs_path_ex_t s_emoji_path;

static const char *emoji_font_path(void)
{
    const size_t ttf_len = (size_t)(emoji_ttf_end - emoji_ttf_start);
    EMOJI_LOG_I("emoji.ttf embedded: %u bytes", (unsigned)ttf_len);
    lv_fs_make_path_from_buffer(&s_emoji_path, LV_FS_MEMFS_LETTER,
                                emoji_ttf_start, (uint32_t)ttf_len, "ttf");
    return (const char *)&s_emoji_path;
}

#else  /* host build (desktop sim, unit tests) */

#  ifndef EMOJI_TTF_PATH
#    error "EMOJI_TTF_PATH must be defined for host builds — set to firmware/main/assets/emoji.ttf"
#  endif

static const char *emoji_font_path(void)
{
    EMOJI_LOG_I("emoji.ttf disk path: %s", EMOJI_TTF_PATH);
    return EMOJI_TTF_PATH;
}

#endif  /* ESP_PLATFORM */

static lv_font_t *create_emoji_font(const char *path, uint32_t size_px)
{
    lv_font_t *f = lv_freetype_font_create(
        path,
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
        size_px,
        LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!f) EMOJI_LOG_E("lv_freetype_font_create size=%u failed", (unsigned)size_px);
    return f;
}

void emoji_font_init(void)
{
    const char *path = emoji_font_path();
    if (!path) {
        EMOJI_LOG_E("no emoji font path; emoji will render as boxes");
        return;
    }

    // lv_init() already calls lv_freetype_init() with the configured cache
    // cap (LV_FREETYPE_CACHE_FT_GLYPH_CNT in sdkconfig.defaults / lv_conf.h),
    // so we don't repeat that here. Just create the two sizes we need.

    lv_font_t *e26 = create_emoji_font(path, 26);
    lv_font_t *e33 = create_emoji_font(path, 33);
    if (e26) {
        // LV_FONT_DECLARE expands to `extern const lv_font_t`, but the
        // matching definition is non-const (manual const-strip). Casting
        // here writes through the real .data symbol.
        ((lv_font_t *)&jbm_bold_26)->fallback = e26;
    }
    if (e33) {
        ((lv_font_t *)&jbm_bold_33)->fallback = e33;
    }
    EMOJI_LOG_I("emoji fallback attached: 26=%p 33=%p", (void *)e26, (void *)e33);
}

#else  /* LV_USE_FREETYPE */

void emoji_font_init(void)
{
    EMOJI_LOG_W("FreeType disabled; emoji will render as boxes");
}

#endif
