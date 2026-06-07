idf_component_register(SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_netif
        esp_event
        spiffs
        irrigation_model
        config_store
        reservoir_manager
        pump_controller
        scheduler
        recommendation_engine
        history_store
        api_server
        wifi_provisioning
        ota_update
        time_service
        alarm_and_safety
        user_observation_store
        weather_manager
        status_display)
