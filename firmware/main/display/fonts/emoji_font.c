#include "emoji_font.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "emoji_font";

#if LV_USE_FREETYPE && LV_USE_FS_MEMFS

// Linker symbols emitted by EMBED_FILES (main/CMakeLists.txt). Wrap the
// raw bytes in an lv_fs_path_ex_t so lv_freetype_font_create can open
// them via the memfs driver. Storage for the path lives static so it
// outlives the create call (LVGL holds a pointer into it).
extern const uint8_t emoji_ttf_start[] asm("_binary_emoji_ttf_start");
extern const uint8_t emoji_ttf_end[]   asm("_binary_emoji_ttf_end");

static lv_fs_path_ex_t s_emoji_path;

// These two structs are made mutable by the CMake configure-time patch
// in main/CMakeLists.txt — without it the fallback assignment below
// would write into .rodata (flash) and fault on the P4.
LV_FONT_DECLARE(jbm_bold_26);
LV_FONT_DECLARE(jbm_bold_33);

static lv_font_t *create_emoji_font(uint32_t size_px)
{
    lv_font_t *f = lv_freetype_font_create(
        (const char *)&s_emoji_path,
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
        size_px,
        LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!f) ESP_LOGE(TAG, "lv_freetype_font_create size=%u failed", (unsigned)size_px);
    return f;
}

void emoji_font_init(void)
{
    const size_t ttf_len = (size_t)(emoji_ttf_end - emoji_ttf_start);
    ESP_LOGI(TAG, "emoji.ttf embedded: %u bytes", (unsigned)ttf_len);

    lv_fs_make_path_from_buffer(&s_emoji_path, LV_FS_MEMFS_LETTER,
                                emoji_ttf_start, (uint32_t)ttf_len, "ttf");

    // Cache cap kept in sync with CONFIG_LV_FREETYPE_CACHE_FT_GLYPH_CNT
    // (sdkconfig.defaults). The LVGL FreeType layer enforces both —
    // passing a larger value here would still get clamped at the lower
    // Kconfig ceiling. 64 nodes ≈ 170 KB PSRAM with ARGB8888 ~32 px
    // emoji glyphs, plenty of headroom for the rider's actual working
    // set (a few smiles + weather + ✅).
    if (lv_freetype_init(64) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "lv_freetype_init failed");
        return;
    }

    lv_font_t *e26 = create_emoji_font(26);
    lv_font_t *e33 = create_emoji_font(33);
    if (e26) {
        // Cast: LV_FONT_DECLARE expands to `extern const lv_font_t`, but
        // the matching definition is non-const (CMake patch). Casting
        // here writes through the real .data symbol.
        ((lv_font_t *)&jbm_bold_26)->fallback = e26;
    }
    if (e33) {
        ((lv_font_t *)&jbm_bold_33)->fallback = e33;
    }
    ESP_LOGI(TAG, "emoji fallback attached: 26=%p 33=%p", (void *)e26, (void *)e33);
}

#else  /* LV_USE_FREETYPE && LV_USE_FS_MEMFS */

void emoji_font_init(void)
{
    ESP_LOGW(TAG, "FreeType or memfs disabled; emoji will render as boxes");
}

#endif
