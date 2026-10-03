# Current product plus the explicitly requested secured transport diagnostic.
set(ble_sources "luna_ble_b0.c" "luna_ble_frame.c" "luna_ble_wifi.c" "luna_weather.c" "luna_weather_parse.c")
if(LUNA_BLE_B1_BUILD)
    file(GLOB luna_ui_assets "${CMAKE_CURRENT_LIST_DIR}/assets/*.c")
    list(APPEND ble_sources "luna_ble_music.c" "luna_dashboard.c" "luna_preview_ui.c" "luna_power.c" "luna_ui_motion.c" "luna_font_decode.c" "luna_time.c" "luna_time_policy.c" ${luna_ui_assets})
endif()
idf_component_register(SRCS ${ble_sources}
    INCLUDE_DIRS "."
    PRIV_REQUIRES bt esp_timer esp_system heap nvs_flash espressif__cjson
                  esp_event esp_netif esp_wifi esp_http_client mbedtls lwip esp_pm)
