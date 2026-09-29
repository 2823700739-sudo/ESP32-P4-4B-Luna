idf_component_register(
    SRCS
        "main.c"
        "luna_agent_client.c"
        "luna_usb.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_i2s
        esp_event
        esp_http_client
        esp_netif
        esp_system
        esp_wifi
        fatfs
        espressif__cjson
        mbedtls
        nvs_flash
)
