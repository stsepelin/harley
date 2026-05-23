idf_component_register(
    SRCS
        main.c
        ble/ble_peripheral.c
        ble/ble_visibility.c
        phone/phone_data.c
        phone/phone_protocol.c
        settings/settings.c
        settings/settings_store.c
        sound/sound.c
        vehicle/vehicle_data.c
        simulator/gear_table.c
        gps/gps_source.c
        poi/poi_math.c
        poi/poi_db.c
        poi/poi_alert.c
        display/boot_screen.c
        display/screen_ride.c
        display/screen_settings.c
        display/screen_settings_bluetooth.c
        display/screen_pairing.c
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
        display/widgets/poi_alert_popup.c
        display/fonts/emoji_font.c
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
        ble
        phone
        settings
        sound
        vehicle
        simulator
        gps
        poi
        display
        display/widgets
        display/fonts
    EMBED_FILES
        assets/boot.gif
        assets/emoji.ttf
    REQUIRES waveshare__esp32_p4_wifi6_touch_lcd_xc lvgl__lvgl nvs_flash
             espressif__esp_codec_dev bt spi_flash
    LDFRAGMENTS linker.lf
)

# Synthetic driving-cycle simulator. Pulled in by default for bench
# builds where the gauge needs to visibly swing; compiled out when
# CONFIG_VROD_INCLUDE_SIM_ENGINE=n so real-bike builds (Phase 3 J1850
# driver as the producer) don't carry dead weight. gear_table.c stays
# unconditional — the gear indicator widget reads from it independently.
if(CONFIG_VROD_INCLUDE_SIM_ENGINE)
    target_sources(${COMPONENT_LIB} PRIVATE
        simulator/sim_engine.c
        simulator/sim_math.c
        gps/gps_sim.c
    )
endif()

# Generated font files (from lv_font_conv) use `#ifdef LV_LVGL_H_INCLUDE_SIMPLE`
# to pick between `#include "lvgl.h"` and `#include "lvgl/lvgl.h"`. We want the
# simple form so the font .c files compile inside the main component.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# LVGL's bundled AnimatedGIF decoder hard-caps width at 480 px. Our boot GIF
# is 800×800; patch the constant up to 800 to match the panel width so
# GIF_openRAM accepts the file. Idempotent — no-op once already patched.
# Runs each configure so a clean managed_components fetch (CI, fresh clone)
# re-applies it automatically.
set(_ANIM_GIF_H "${CMAKE_SOURCE_DIR}/managed_components/lvgl__lvgl/src/libs/gif/AnimatedGIF.h")
if(EXISTS "${_ANIM_GIF_H}")
    file(READ "${_ANIM_GIF_H}" _content)
    string(REPLACE "#define MAX_WIDTH 480" "#define MAX_WIDTH 800" _patched "${_content}")
    if(NOT "${_patched}" STREQUAL "${_content}")
        file(WRITE "${_ANIM_GIF_H}" "${_patched}")
        message(STATUS "Patched AnimatedGIF MAX_WIDTH 480 -> 800")
    endif()
endif()

# jbm_bold_26 / jbm_bold_33 have their `lv_font_t` struct definition
# manually patched to drop `const` (search the .c files for the literal
# "lv_font_t jbm_bold_" — no `const` qualifier on those lines). Without
# that the FreeType emoji fallback can't be attached at boot: ESP32-P4
# maps .rodata to flash (XIP), so writing to the fallback field through
# a const cast would fault. The big glyph bitmap and dsc tables still
# have `const` and stay in flash. If you regenerate either font via
# lv_font_conv (see the `Opts:` comment in each file's header), re-apply
# the const strip on the lv_font_t line — see CLAUDE.md note.

# LVGL fast-mem section override — two concerns served by this one line:
#  1) GCC 15 rejects LVGL's bundled LV_ATTRIBUTE_FAST_MEM=IRAM_ATTR because
#     IRAM_ATTR's __COUNTER__ produces different .iram1.N section names for
#     the header decl and the .c definition. A stable section name fixes it.
#  2) Route the ~118 KB block to flash (.text.lvgl_fast). Putting the
#     whole block in IRAM no longer links — stage 5's mbedtls ECC + BLE
#     SM additions pushed the cumulative IRAM-marked section size past
#     the placer's budget under -Wl,--enable-non-contiguous-regions on
#     P4 < v3 silicon (binutils 2.45 segfaults at the final ld step,
#     discards include _global_interrupt_handler and the spi_flash
#     generic driver). See docs/ble-bringup-bisect.md.
#
# The flash placement DOES cost render time — measured at 110 ms / 7 FPS
# during a tach sweep, vs the ~30 FPS the perf-bake commit (35c67c2) was
# measured at with .iram1.lvgl_fast back when it still linked. The fix
# is in main/linker.lf, which surgically pulls just the inner ARGB->RGB565
# blender into internal SRAM. Recovers most of the loss without
# re-triggering the link trap.
idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${LVGL_LIB} PUBLIC
    "LV_ATTRIBUTE_FAST_MEM=__attribute__((section(\".text.lvgl_fast\")))"
)
