#include "alarm_and_safety.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"
#include "pump_controller.h"
#include "reservoir_manager.h"

static void safety_task(void *arg)
{
    while (true) {
        runtime_state_t runtime = pump_controller_get_runtime();
        if (!reservoir_manager_is_ok() &&
            (runtime.pump_state == PUMP_STATE_MANUAL_RUNNING || runtime.pump_state == PUMP_STATE_SCHEDULED_RUNNING)) {
            pump_controller_stop(STOP_LOW_RESERVOIR);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t alarm_and_safety_start(void)
{
    xTaskCreate(safety_task, "Safety Task", 3072, NULL, 7, NULL);
    return ESP_OK;
}
