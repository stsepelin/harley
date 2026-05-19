idf_component_register(
    SRCS
        main.c
        phone/phone_data.c
        phone/phone_protocol.c
        settings/settings.c
        settings/settings_store.c
        sound/sound.c
        vehicle/vehicle_data.c
        simulator/sim_engine.c
        simulator/gear_table.c
        simulator/sim_math.c
        display/boot_screen.c
        display/screen_ride.c
        display/screen_settings.c
        display/ui_manager.c
        display/format.c
        display/gesture.c
        display/units.c
        display/widget_util.c
        display/widgets/tach_arc.c
        display/widgets/smooth.c
        display/widgets/speed_display.c
        display/widgets/gear_indicator.c
        display/widgets/fuel_bar.c
        display/widgets/turn_signals.c
        display/widgets/temp_display.c
        display/widgets/warning_lights.c
        display/widgets/clock_display.c
        display/widgets/odometer_display.c
        display/widgets/trip_display.c
        display/widgets/notification_banner.c
        display/widgets/now_playing_display.c
        display/widgets/media_banner.c
        display/fonts/jbm_bold_26.c
        display/fonts/jbm_bold_33.c
        display/fonts/jbm_bold_45.c
        display/fonts/jbm_bold_72.c
        display/fonts/jbm_bold_144.c
        display/fonts/mdi_36.c
        display/fonts/mdi_50.c
        display/fonts/mdi_60.c
        display/fonts/mdi_96.c
    INCLUDE_DIRS
        .
        phone
        settings
        sound
        vehicle
        simulator
        display
        display/widgets
        display/fonts
    EMBED_FILES
        assets/boot.gif
    REQUIRES waveshare__esp32_p4_wifi6_touch_lcd_xc lvgl__lvgl nvs_flash
             espressif__esp_codec_dev
)

# Generated font files (from lv_font_conv) use `#ifdef LV_LVGL_H_INCLUDE_SIMPLE`
# to pick between `#include "lvgl.h"` and `#include "lvgl/lvgl.h"`. We want the
# simple form so the font .c files compile inside the main component.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# GCC 15 (IDF v6) rejects LVGL's LV_ATTRIBUTE_FAST_MEM=IRAM_ATTR because
# IRAM_ATTR's __COUNTER__ puts the header decl and the .c def in different
# .iram1.N sections. Override with a stable name; paired with
# CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n in sdkconfig.defaults.
# LVGL's bundled AnimatedGIF decoder hard-caps width at 480 px. Our boot GIF
# is 800×800; patch the constant up to 1024 so GIF_openRAM accepts the file.
# Idempotent — no-op once already patched. Runs each configure so a clean
# managed_components fetch (CI, fresh clone) re-applies it automatically.
set(_ANIM_GIF_H "${CMAKE_SOURCE_DIR}/managed_components/lvgl__lvgl/src/libs/gif/AnimatedGIF/src/AnimatedGIF.h")
if(EXISTS "${_ANIM_GIF_H}")
    file(READ "${_ANIM_GIF_H}" _content)
    string(REPLACE "#define MAX_WIDTH 480" "#define MAX_WIDTH 1024" _patched "${_content}")
    if(NOT "${_patched}" STREQUAL "${_content}")
        file(WRITE "${_ANIM_GIF_H}" "${_patched}")
        message(STATUS "Patched AnimatedGIF MAX_WIDTH 480 -> 1024")
    endif()
endif()

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${LVGL_LIB} PUBLIC
    "LV_ATTRIBUTE_FAST_MEM=__attribute__((section(\".iram1.lvgl_fast\")))"
)

