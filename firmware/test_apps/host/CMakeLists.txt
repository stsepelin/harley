cmake_minimum_required(VERSION 3.16)
project(vrod_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(MAIN_DIR  ${REPO_ROOT}/main)

# --- Coverage knob ---------------------------------------------------------
# Off by default so day-to-day runs stay fast. Turn on with -DCOVERAGE=ON,
# then `lcov` against ./CMakeFiles/<target>.dir.
option(COVERAGE "Enable gcov instrumentation" OFF)
if(COVERAGE)
    add_compile_options(-fprofile-arcs -ftest-coverage -O0 -g)
    add_link_options(-fprofile-arcs -ftest-coverage)
endif()

# --- Unity (vendored) ------------------------------------------------------
add_library(unity STATIC external/unity/unity.c)
target_include_directories(unity PUBLIC external/unity)
# The Unity in ESP-IDF brings in some target helpers via unity_config.h; we
# don't want any of that on host, so neuter the config include.
target_compile_definitions(unity PUBLIC UNITY_EXCLUDE_CONFIG_H)

# --- Modules under test ----------------------------------------------------
# Exactly the files extracted as pure-logic helpers. Adding anything else
# here should be a deliberate, reviewed choice — see tests/README.md.
add_library(vrod_pure STATIC
    ${MAIN_DIR}/simulator/gear_table.c
    ${MAIN_DIR}/simulator/sim_math.c
    ${MAIN_DIR}/display/format.c
    ${MAIN_DIR}/display/gesture.c
    ${MAIN_DIR}/display/units.c
    ${MAIN_DIR}/display/widgets/smooth.c
    ${MAIN_DIR}/display/widgets/fuel_scale.c
    ${MAIN_DIR}/phone/phone_data.c
    ${MAIN_DIR}/phone/phone_protocol.c
    ${MAIN_DIR}/settings/settings.c
    ${MAIN_DIR}/vehicle/vehicle_data.c
    ${MAIN_DIR}/vehicle/gear_calc.c
    ${MAIN_DIR}/vehicle/trip_meter.c
    ${MAIN_DIR}/j1850/j1850_vpw.c
    ${MAIN_DIR}/j1850/j1850_tx_logic.c
    ${MAIN_DIR}/j1850/j1850_parse.c
    ${MAIN_DIR}/j1850/j1850_driver.c
    ${MAIN_DIR}/j1850/j1850_edge.c
    ${MAIN_DIR}/j1850/ride_log_format.c
    ${MAIN_DIR}/ble/ble_visibility.c
    external/freertos_stub/freertos_stub.c
    external/lvgl_tick_stub/lv_tick_stub.c
)
target_include_directories(vrod_pure PUBLIC
    ${MAIN_DIR}/simulator
    ${MAIN_DIR}/display
    ${MAIN_DIR}/display/widgets
    ${MAIN_DIR}/phone
    ${MAIN_DIR}/settings
    ${MAIN_DIR}/vehicle
    ${MAIN_DIR}/j1850
    ${MAIN_DIR}/ble
    external/freertos_stub
    external/lvgl_tick_stub
)
# libm for the trig in the widget/geometry code that links on top.
target_link_libraries(vrod_pure PUBLIC m)

# --- Widget library --------------------------------------------------------
# Every display widget + the LVGL stub. The stub fakes the full surface the
# widgets touch (labels, images, canvas + a deterministic block-glyph
# rasterizer, timers, events, heap_caps) so even the baked-sprite widgets
# run their real bake/update code on host.
add_library(vrod_widgets STATIC
    ${MAIN_DIR}/display/widgets/tach_arc.c
    ${MAIN_DIR}/display/widgets/speed_display.c
    ${MAIN_DIR}/display/widgets/gear_indicator.c
    ${MAIN_DIR}/display/widgets/fuel_arc.c
    ${MAIN_DIR}/display/widgets/temp_display.c
    ${MAIN_DIR}/display/widgets/turn_signals.c
    ${MAIN_DIR}/display/widgets/clock_display.c
    ${MAIN_DIR}/display/widgets/odometer_display.c
    ${MAIN_DIR}/display/widgets/trip_display.c
    ${MAIN_DIR}/display/widgets/warning_lights.c
    ${MAIN_DIR}/display/widgets/notification_banner.c
    ${MAIN_DIR}/display/widgets/media_banner.c
    ${MAIN_DIR}/display/widgets/now_playing_display.c
    ${MAIN_DIR}/display/widget_util.c
    external/lvgl_stub/lvgl_stub.c
)
target_include_directories(vrod_widgets PUBLIC
    ${MAIN_DIR}/display
    ${MAIN_DIR}/display/widgets
    ${MAIN_DIR}/vehicle
    external/lvgl_stub
    external/esp_compat
)
target_link_libraries(vrod_widgets PUBLIC vrod_pure)  # format helpers

# --- Tests -----------------------------------------------------------------
enable_testing()

function(add_unit_test name)
    add_executable(${name} tests/${name}.c tests/test_runner.c)
    target_link_libraries(${name} PRIVATE vrod_pure unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_gear_table)
add_unit_test(test_sim_math)
add_unit_test(test_format)
add_unit_test(test_smooth)
add_unit_test(test_fuel_scale)
add_unit_test(test_vehicle_data)
add_unit_test(test_gear_calc)
add_unit_test(test_trip_meter)
add_unit_test(test_units)
add_unit_test(test_settings)
add_unit_test(test_phone_protocol)
add_unit_test(test_phone_data)
add_unit_test(test_gesture)
add_unit_test(test_j1850_vpw)
add_unit_test(test_j1850_tx_logic)
add_unit_test(test_j1850_parse)
add_unit_test(test_j1850_driver)
add_unit_test(test_j1850_edge)
add_unit_test(test_ride_log_format)
add_unit_test(test_ble_visibility)

# Widget cache-regression tests link against the LVGL stub.
function(add_widget_test name)
    add_executable(${name} tests/${name}.c tests/test_runner.c)
    target_link_libraries(${name} PRIVATE vrod_widgets unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_widget_test(test_widget_caches)
add_widget_test(test_sprite_raster)
add_widget_test(test_tach_arc)
