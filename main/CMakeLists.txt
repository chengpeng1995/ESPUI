# ESP-IDF first scans components in CMake script mode, where CONFIGURE_DEPENDS
# is unsupported. The normal configure pass still tracks new generated files.
if(CMAKE_SCRIPT_MODE_FILE)
    set(ESPUI_GLOB_OPTIONS)
else()
    set(ESPUI_GLOB_OPTIONS CONFIGURE_DEPENDS)
endif()
file(GLOB FONT_SOURCES ${ESPUI_GLOB_OPTIONS} "../assets/fonts/*.c")
file(GLOB ICON_SOURCES ${ESPUI_GLOB_OPTIONS} "../assets/icons/*.c" "../assets/blue_ui/*.c")
file(GLOB UI_SOURCES ${ESPUI_GLOB_OPTIONS} "../ui/*.cpp")
idf_component_register(SRCS "main.cpp" "../core/system.cpp" "../core/i18n.cpp"
    ${UI_SOURCES} "../platform/esp_platform.cpp" "../platform/power_manager.cpp" "../platform/board_audio.c" "../platform/board_display.c"
    "../vendor/SensorLib/src/platform/SensorCommStatic.cpp" "../vendor/XPowersLib/src/XPowersLibInterface.cpp" ${FONT_SOURCES} ${ICON_SOURCES}
    INCLUDE_DIRS "." "../core" "../ui" "../platform" "../assets/fonts" "../assets/blue_ui"
        "../vendor/XPowersLib/src" "../vendor/SensorLib/src"
    PRIV_REQUIRES esp_pm json nvs_flash esp_timer esp_driver_usb_serial_jtag esp_wifi esp_netif bt fatfs esp_driver_spi espressif__esp_lcd_co5300
        waveshare__esp32_s3_touch_amoled_1_75 lvgl__lvgl mooncake smooth_ui_toolkit)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_XPOWERS_ESP_IDF_NEW_API=1 CONFIG_SENSORLIB_ESP_IDF_NEW_API=1 LV_LVGL_H_INCLUDE_SIMPLE=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-misleading-indentation)
