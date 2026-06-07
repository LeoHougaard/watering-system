idf_component_register(SRCS "weather_manager.c" INCLUDE_DIRS "include" REQUIRES esp_http_client json config_store history_store irrigation_model)
