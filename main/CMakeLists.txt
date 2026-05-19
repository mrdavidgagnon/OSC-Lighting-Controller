idf_component_register(SRCS "main.c" "wifi_manager.c" "captive_portal.c" "web_config.c" "osc.c" "onyx.c" "mcp23017.c" "i2c_bus.c" "ads1115.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_i2c esp_event esp_http_server esp_netif esp_wifi nvs_flash)
