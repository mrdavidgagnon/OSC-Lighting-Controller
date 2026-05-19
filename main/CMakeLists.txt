idf_component_register(SRCS "main.c" "wifi_manager.c" "captive_portal.c" "web_config.c" "osc.c" "onyx.c" "oled_display.c" "i2c_scan.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c esp_event esp_http_server esp_netif esp_wifi nvs_flash)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE BUILD_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT BUILD_GIT_HASH OR BUILD_GIT_HASH STREQUAL "")
    set(BUILD_GIT_HASH "unknown")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BUILD_GIT_HASH="${BUILD_GIT_HASH}"
)
