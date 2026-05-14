idf_component_register(
    SRCS
        main.c
        vehicle/vehicle_data.c
        simulator/sim_engine.c
        display/boot_screen.c
    INCLUDE_DIRS
        .
        vehicle
        simulator
        display
    REQUIRES waveshare__esp32_p4_wifi6_touch_lcd_xc lvgl__lvgl
)

# GCC 15 (IDF v6) rejects LVGL's LV_ATTRIBUTE_FAST_MEM=IRAM_ATTR because
# IRAM_ATTR's __COUNTER__ puts the header decl and the .c def in different
# .iram1.N sections. Override with a stable name; paired with
# CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n in sdkconfig.defaults.
idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${LVGL_LIB} PUBLIC
    "LV_ATTRIBUTE_FAST_MEM=__attribute__((section(\".iram1.lvgl_fast\")))"
)
