idf_component_register(SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp32-camera
        esp_http_server
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_app_format
        app_update
        nvs_flash
        mdns)
