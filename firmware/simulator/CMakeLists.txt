cmake_minimum_required(VERSION 3.16)
project(vrod_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(REPO_ROOT  ${CMAKE_CURRENT_LIST_DIR}/..)
set(MAIN_DIR   ${REPO_ROOT}/main)
set(SIM_DIR    ${CMAKE_CURRENT_LIST_DIR})

# --- SDL2 + FreeType ------------------------------------------------------
# pkg-config first (cleanest on Linux + Homebrew); fall back to find_package.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 sdl2)
    pkg_check_modules(FREETYPE freetype2)
endif()
if(NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED)
endif()
if(NOT FREETYPE_FOUND)
    find_package(Freetype REQUIRED)
endif()

# --- LVGL ------------------------------------------------------------------
# Our lv_conf.h lives next to this CMakeLists; tell LVGL to find it there.
# LV_CONF_INCLUDE_SIMPLE → all LVGL .c files do `#include "lv_conf.h"`.
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)
# The sim is a map-capable build (it can render the map), so compile the
# map-gated UI - notably the Settings > General LAYOUT toggle - into it.
add_compile_definitions(CONFIG_VROD_MAP_DEMO=1)
include_directories(BEFORE ${SIM_DIR})  # so lv_conf.h is found before LVGL's

# LVGL's SDL + FreeType bindings #include the SDL / ft2build.h headers
# directly. Both have to be visible globally *before* the lvgl
# add_subdirectory below, otherwise lv_sdl_window.c / lv_freetype_*.c
# can't find the headers during their own compile.
if(SDL2_INCLUDE_DIRS)
    include_directories(${SDL2_INCLUDE_DIRS})
endif()
include_directories(${FREETYPE_INCLUDE_DIRS})

# LVGL's CMake (non-ESP path) builds a `lvgl` target. Reuse the same source
# tree the firmware uses so the simulator can never drift out of sync.
add_subdirectory(${REPO_ROOT}/managed_components/lvgl__lvgl
                 ${CMAKE_BINARY_DIR}/lvgl)

# --- Production sources ----------------------------------------------------
# Everything we ship to the device EXCEPT the BSP/boot/main wiring (those
# touch ESP-only APIs). Boot screen is excluded — it embeds boot.gif via
# EMBED_FILES which is ESP-IDF-only.
file(GLOB FONT_SOURCES ${MAIN_DIR}/display/fonts/*.c)

add_executable(vrod_sim
    main.c
    external/freertos_shim/freertos_shim.c

    ${MAIN_DIR}/display/screen_ride.c
    ${MAIN_DIR}/display/screen_settings.c
    ${MAIN_DIR}/display/screen_settings_general.c
    ${MAIN_DIR}/display/screen_settings_trip.c
    ${MAIN_DIR}/display/screen_settings_odoset.c
    ${MAIN_DIR}/display/screen_settings_bluetooth.c
    ${MAIN_DIR}/display/format.c
    ${MAIN_DIR}/display/gesture.c
    ${MAIN_DIR}/display/units.c
    ${MAIN_DIR}/display/widget_util.c
    ${MAIN_DIR}/settings/settings.c
    ui_manager_shim.c
    ble_peripheral_shim.c
    test_bridge.c
    ${MAIN_DIR}/display/widgets/tach_arc.c
    ${MAIN_DIR}/display/widgets/speed_display.c
    ${MAIN_DIR}/display/widgets/gear_indicator.c
    ${MAIN_DIR}/display/widgets/fuel_arc.c
    ${MAIN_DIR}/display/widgets/fuel_scale.c

    ${MAIN_DIR}/display/widgets/rpm_bar.c

    ${MAIN_DIR}/display/widgets/rpm_scale.c
    ${MAIN_DIR}/display/widgets/turn_signals.c
    ${MAIN_DIR}/display/widgets/temp_display.c
    ${MAIN_DIR}/display/widgets/warning_lights.c
    ${MAIN_DIR}/display/widgets/clock_display.c
    ${MAIN_DIR}/display/widgets/odometer_display.c
    ${MAIN_DIR}/display/widgets/trip_display.c
    ${MAIN_DIR}/display/widgets/notification_banner.c
    ${MAIN_DIR}/display/widgets/now_playing_display.c
    ${MAIN_DIR}/display/widgets/media_banner.c
    ${MAIN_DIR}/display/widgets/smooth.c

    ${MAIN_DIR}/phone/phone_data.c
    ${MAIN_DIR}/phone/phone_protocol.c
    ${MAIN_DIR}/phone/icon_cache.c

    ${MAIN_DIR}/map/map_tile.c
    ${MAIN_DIR}/map/map_render.c
    ${MAIN_DIR}/map/map_source.c
    ${MAIN_DIR}/map/map_cells.c
    ${MAIN_DIR}/map/map_world.c
    ${MAIN_DIR}/display/screen_map.c

    ${MAIN_DIR}/simulator/sim_engine.c
    ${MAIN_DIR}/simulator/gear_table.c
    ${MAIN_DIR}/simulator/sim_math.c

    ${MAIN_DIR}/vehicle/vehicle_data.c

    ${FONT_SOURCES}
)

target_include_directories(vrod_sim PRIVATE
    ${MAIN_DIR}
    ${MAIN_DIR}/ble
    ${MAIN_DIR}/display
    ${MAIN_DIR}/display/widgets
    ${MAIN_DIR}/display/fonts
    ${MAIN_DIR}/phone
    ${MAIN_DIR}/map
    ${MAIN_DIR}/settings
    ${MAIN_DIR}/simulator
    ${MAIN_DIR}/sound
    ${MAIN_DIR}/vehicle
    ${SIM_DIR}/external/freertos_shim
    ${SIM_DIR}/external/esp_compat
)

target_link_libraries(vrod_sim PRIVATE
    lvgl
    pthread
)

# emoji_font.c reads firmware/main/assets/emoji.ttf via an absolute path on
# the host because EMBED_FILES is ESP-IDF-only. The firmware build path
# keeps embedding the same TTF via the cluster's main/CMakeLists.txt.
target_compile_definitions(vrod_sim PRIVATE
    EMOJI_TTF_PATH=\"${MAIN_DIR}/assets/emoji.ttf\"
)

if(SDL2_LIBRARIES)
    target_link_libraries(vrod_sim PRIVATE ${SDL2_LIBRARIES})
    target_include_directories(vrod_sim PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_directories(vrod_sim PRIVATE ${SDL2_LIBRARY_DIRS})
else()
    target_link_libraries(vrod_sim PRIVATE SDL2::SDL2)
endif()

# FreeType for the runtime color-emoji fallback chain. Headers were
# added globally above (before LVGL's add_subdirectory) so lv_freetype.c
# can build; here we just link the actual library into vrod_sim.
target_link_libraries(vrod_sim PRIVATE ${FREETYPE_LIBRARIES})
if(FREETYPE_LIBRARY_DIRS)
    target_link_directories(vrod_sim PRIVATE ${FREETYPE_LIBRARY_DIRS})
endif()
