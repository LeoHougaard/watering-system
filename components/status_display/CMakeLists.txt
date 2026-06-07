idf_component_register(SRCS "status_display.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        irrigation_model
        moisture_manager
        pump_controller
        scheduler)
