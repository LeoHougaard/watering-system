idf_component_register(SRCS "pump_controller.c" INCLUDE_DIRS "include" REQUIRES esp_timer driver config_store reservoir_manager history_store irrigation_model)
