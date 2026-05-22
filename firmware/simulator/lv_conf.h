// LVGL configuration for the desktop simulator. Picks the SDL2 display
// driver and the POSIX OS layer; everything else uses LVGL's defaults
// (defined in lv_conf_internal.h) so adding a widget on firmware
// doesn't require a parallel toggle here.
#pragma once

// --- Display + colour ------------------------------------------------------
#define LV_COLOR_DEPTH      32      // 32-bit on the desktop is the simplest path

// --- OS layer --------------------------------------------------------------
#define LV_USE_OS           1       // LV_OS_PTHREAD (matches POSIX hosts)

// --- Memory ----------------------------------------------------------------
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

// --- Logging ---------------------------------------------------------------
// ERROR (not WARN) so the SDL mouse driver's "Y is N which is greater than
// ver. res" message during off-window drags doesn't spam the console. That
// input never happens on the real (physically clamped) GT911 panel.
#define LV_USE_LOG          1
#define LV_LOG_LEVEL        LV_LOG_LEVEL_ERROR
#define LV_LOG_PRINTF       1

// --- SDL2 display + input driver ------------------------------------------
#define LV_USE_SDL                  1
#define LV_SDL_INCLUDE_PATH         <SDL.h>
#define LV_SDL_RENDER_MODE          LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT            1
#define LV_SDL_FULLSCREEN           0
#define LV_SDL_DIRECT_EXIT          1
#define LV_SDL_MOUSEWHEEL_MODE      LV_SDL_MOUSEWHEEL_MODE_ENCODER

// --- Image / GIF -----------------------------------------------------------
#define LV_USE_GIF          1

// --- Fonts that boot_screen falls back on ---------------------------------
#define LV_FONT_MONTSERRAT_22   1
#define LV_FONT_MONTSERRAT_48   1

// --- FreeType for the color-emoji fallback chain --------------------------
// Mirrors the firmware sdkconfig so jbm_bold_26 / jbm_bold_33 grow a
// .fallback to a runtime-built FreeType font over firmware/main/assets/emoji.ttf.
// LV_FREETYPE_USE_LVGL_PORT is off on the host — the LVGL port reaches
// into FreeType internal headers (ftdebug.h etc.) that homebrew's
// libfreetype doesn't ship. The default FreeType libc port handles
// fopen/fread itself, which is fine for the desktop. On firmware we
// keep the LVGL port + memfs so the TTF stays in flash.
#define LV_USE_FREETYPE                 1
#define LV_FREETYPE_USE_LVGL_PORT       0
#define LV_FREETYPE_CACHE_FT_GLYPH_CNT  64
// COLR rasterizer is stack-hungry; LVGL's compile-time check fails under 32 KB.
#define LV_DRAW_THREAD_STACK_SIZE       32768
