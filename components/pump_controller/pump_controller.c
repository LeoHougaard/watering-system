#include "pump_controller.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "config_store.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "history_store.h"
#include "reservoir_manager.h"

static const char *TAG = "pump";
static const bool DIAGNOSTIC_FORCE_GPIO_5_HIGH_6_LOW = false;

typedef enum {
    CMD_MANUAL_RUN,
    CMD_SCHEDULED_RUN,
    CMD_STOP,
} pump_command_type_t;

typedef struct {
    pump_command_type_t type;
    uint32_t duration_sec;
    stop_reason_t stop_reason;
    char reason[32];
} pump_command_t;

static QueueHandle_t g_queue;
static runtime_state_t g_runtime = {
    .pump_state = PUMP_STATE_IDLE,
    .reservoir_ok = true,
};
static system_settings_t g_settings;
static bool g_running;
static time_t g_run_started_at;
static uint32_t g_run_duration_sec;
static watering_event_type_t g_run_type;
static int g_cmd_a;
static int g_cmd_b;
static int g_cmd_sleep = -1;

static bool pins_conflict(const system_settings_t *settings, char *reason, size_t reason_len)
{
    if (settings->pump_gpio == settings->pump_gpio_b) {
        snprintf(reason, reason_len, "Pump IN1 and IN2 are both GPIO%d.", settings->pump_gpio);
        return true;
    }
    if (!settings->reservoir_sensor_bypass && settings->reservoir_sensor_enabled &&
        (settings->reservoir_gpio == settings->pump_gpio || settings->reservoir_gpio == settings->pump_gpio_b)) {
        snprintf(reason, reason_len, "Reservoir GPIO%d conflicts with pump GPIOs.", settings->reservoir_gpio);
        return true;
    }
    if (settings->pump_sleep_gpio >= 0 &&
        (settings->pump_sleep_gpio == settings->pump_gpio || settings->pump_sleep_gpio == settings->pump_gpio_b)) {
        snprintf(reason, reason_len, "Pump SLEEP GPIO%d conflicts with pump input GPIOs.", settings->pump_sleep_gpio);
        return true;
    }
    return false;
}

static void relay_off(void)
{
    gpio_set_level(g_settings.pump_gpio, 1);
    gpio_set_level(g_settings.pump_gpio_b, 0);
    g_cmd_a = 1;
    g_cmd_b = 0;
    if (g_settings.pump_sleep_gpio >= 0) {
        gpio_set_level(g_settings.pump_sleep_gpio, 1);
        g_cmd_sleep = 1;
    } else {
        g_cmd_sleep = -1;
    }
}

static void relay_on(void)
{
    if (g_settings.pump_sleep_gpio >= 0) {
        gpio_set_level(g_settings.pump_sleep_gpio, 1);
        g_cmd_sleep = 1;
    } else {
        g_cmd_sleep = -1;
    }
    gpio_set_level(g_settings.pump_gpio, 0);
    gpio_set_level(g_settings.pump_gpio_b, 0);
    g_cmd_a = 0;
    g_cmd_b = 0;
}

static void log_stop(stop_reason_t reason, bool completed)
{
    time_t now = time(NULL);
    uint32_t actual = g_run_started_at > 0 ? (uint32_t)(now - g_run_started_at) : 0;
    watering_event_t event = {
        .timestamp = g_run_started_at > 0 ? g_run_started_at : now,
        .type = g_run_type,
        .duration_sec = actual,
        .reservoir_ok_at_start = true,
        .completed = completed,
        .stopped_reason = reason,
    };
    history_store_log_watering(&event);
    g_runtime.last_run_at = now;
    g_runtime.last_run_duration_sec = actual;
    snprintf(g_runtime.last_run_reason, sizeof(g_runtime.last_run_reason), "%s", stop_reason_to_string(reason));
}

static void stop_running(stop_reason_t reason)
{
    if (!g_running) return;
    relay_off();
    bool completed = reason == STOP_TIMER_COMPLETE;
    log_stop(reason, completed);
    g_running = false;
    g_runtime.current_run_remaining_sec = 0;
    bool cooldown_needed = g_run_type == RUN_TYPE_SCHEDULED ||
        reason == STOP_LOW_RESERVOIR ||
        reason == STOP_FAULT ||
        reason == STOP_MAX_RUNTIME;
    if (cooldown_needed) {
        g_runtime.cooldown_until = time(NULL) + g_settings.pump_cooldown_sec;
        g_runtime.pump_state = PUMP_STATE_COOLDOWN;
    } else {
        g_runtime.cooldown_until = 0;
        g_runtime.pump_state = g_runtime.reservoir_ok ? PUMP_STATE_IDLE : PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR;
    }
    ESP_LOGI(TAG, "pump stopped: %s", stop_reason_to_string(reason));
}

static void start_run(watering_event_type_t type, uint32_t requested_duration, const char *reason)
{
    time_t now = time(NULL);
    g_runtime.reservoir_ok = reservoir_manager_is_ok();
    if (!g_runtime.reservoir_ok) {
        g_runtime.pump_state = PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR;
        snprintf(g_runtime.block_reason, sizeof(g_runtime.block_reason), "Reservoir sensor reads low.");
        watering_event_t skipped = {
            .timestamp = now,
            .type = RUN_TYPE_SKIPPED,
            .duration_sec = 0,
            .reservoir_ok_at_start = false,
            .completed = false,
            .stopped_reason = STOP_LOW_RESERVOIR,
        };
        history_store_log_watering(&skipped);
        return;
    }
    if (now < g_runtime.cooldown_until) {
        g_runtime.pump_state = PUMP_STATE_COOLDOWN;
        snprintf(g_runtime.block_reason, sizeof(g_runtime.block_reason), "Pump is cooling down.");
        watering_event_t skipped = {
            .timestamp = now,
            .type = RUN_TYPE_SKIPPED,
            .duration_sec = 0,
            .reservoir_ok_at_start = g_runtime.reservoir_ok,
            .completed = false,
            .stopped_reason = STOP_COOLDOWN,
        };
        history_store_log_watering(&skipped);
        return;
    }
    uint32_t max = type == RUN_TYPE_MANUAL ? g_settings.manual_max_run_sec : g_settings.scheduled_max_run_sec;
    uint32_t duration = requested_duration > max ? max : requested_duration;
    if (requested_duration > max) g_runtime.pump_state = PUMP_STATE_LOCKED_OUT_MAX_RUNTIME;
    snprintf(g_runtime.block_reason, sizeof(g_runtime.block_reason), "Running.");

    g_running = true;
    g_run_started_at = now;
    g_run_duration_sec = duration;
    g_run_type = type;
    g_runtime.current_run_remaining_sec = duration;
    g_runtime.pump_state = type == RUN_TYPE_MANUAL ? PUMP_STATE_MANUAL_RUNNING : PUMP_STATE_SCHEDULED_RUNNING;
    snprintf(g_runtime.last_run_reason, sizeof(g_runtime.last_run_reason), "%s", reason ? reason : event_type_to_string(type));
    relay_on();
    ESP_LOGI(TAG, "pump started: %s for %lu sec", event_type_to_string(type), (unsigned long)duration);
}

static void pump_task(void *arg)
{
    while (true) {
        pump_command_t cmd;
        if (xQueueReceive(g_queue, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (cmd.type == CMD_MANUAL_RUN) start_run(RUN_TYPE_MANUAL, cmd.duration_sec, cmd.reason);
            else if (cmd.type == CMD_SCHEDULED_RUN) start_run(RUN_TYPE_SCHEDULED, cmd.duration_sec, "scheduled");
            else if (cmd.type == CMD_STOP) stop_running(cmd.stop_reason);
        }

        g_runtime.reservoir_ok = reservoir_manager_is_ok();
        if (g_running) {
            time_t now = time(NULL);
            uint32_t elapsed = (uint32_t)(now - g_run_started_at);
            g_runtime.current_run_remaining_sec = elapsed >= g_run_duration_sec ? 0 : g_run_duration_sec - elapsed;
            if (!g_runtime.reservoir_ok) stop_running(STOP_LOW_RESERVOIR);
            else if (elapsed >= g_run_duration_sec) stop_running(STOP_TIMER_COMPLETE);
        } else if (g_runtime.pump_state == PUMP_STATE_COOLDOWN && time(NULL) >= g_runtime.cooldown_until) {
            g_runtime.pump_state = g_runtime.reservoir_ok ? PUMP_STATE_IDLE : PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR;
            snprintf(g_runtime.block_reason, sizeof(g_runtime.block_reason), "%s", g_runtime.reservoir_ok ? "" : "Reservoir sensor reads low.");
        } else if (!g_runtime.reservoir_ok && g_runtime.pump_state == PUMP_STATE_IDLE) {
            g_runtime.pump_state = PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR;
            snprintf(g_runtime.block_reason, sizeof(g_runtime.block_reason), "Reservoir sensor reads low.");
        } else if (g_runtime.reservoir_ok && g_runtime.pump_state == PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR) {
            g_runtime.pump_state = PUMP_STATE_IDLE;
            g_runtime.block_reason[0] = '\0';
        }
    }
}

esp_err_t pump_controller_start(void)
{
    config_store_get_settings(&g_settings);
    if (DIAGNOSTIC_FORCE_GPIO_5_HIGH_6_LOW) {
        g_settings.pump_gpio = 5;
        g_settings.pump_gpio_b = 6;
        g_settings.pump_sleep_gpio = -1;
    }
    if (pins_conflict(&g_settings, g_runtime.block_reason, sizeof(g_runtime.block_reason))) {
        g_runtime.pump_state = PUMP_STATE_FAULT;
        ESP_LOGE(TAG, "%s", g_runtime.block_reason);
        return ESP_ERR_INVALID_STATE;
    }
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << g_settings.pump_gpio) | (1ULL << g_settings.pump_gpio_b),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    if (g_settings.pump_sleep_gpio >= 0) {
        gpio_config_t sleep_io = {
            .pin_bit_mask = 1ULL << g_settings.pump_sleep_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&sleep_io);
        gpio_set_level(g_settings.pump_sleep_gpio, 1);
    }
    if (DIAGNOSTIC_FORCE_GPIO_5_HIGH_6_LOW) {
        relay_on();
        g_runtime.pump_state = PUMP_STATE_MANUAL_RUNNING;
        snprintf(g_runtime.last_run_reason, sizeof(g_runtime.last_run_reason), "diagnostic_hold");
        ESP_LOGW(TAG, "diagnostic hold active: GPIO5 high, GPIO6 low");
    } else {
        relay_off();
    }
    g_queue = xQueueCreate(8, sizeof(pump_command_t));
    xTaskCreate(pump_task, "Pump Task", 4096, NULL, 8, NULL);
    return g_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t pump_controller_manual_run(uint32_t duration_sec, const char *reason)
{
    pump_command_t cmd = {.type = CMD_MANUAL_RUN, .duration_sec = duration_sec};
    snprintf(cmd.reason, sizeof(cmd.reason), "%s", reason ? reason : "manual_user_request");
    return xQueueSend(g_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t pump_controller_can_manual_run(uint32_t duration_sec, char *reason, size_t reason_len)
{
    (void)duration_sec;
    system_settings_t settings;
    config_store_get_settings(&settings);
    if (pins_conflict(&settings, reason, reason_len)) return ESP_ERR_INVALID_STATE;
    reservoir_status_t reservoir = reservoir_manager_get_status();
    if (!reservoir.ok) {
        snprintf(reason, reason_len, "Reservoir is low on GPIO%d (raw=%d, active-%s).", reservoir.gpio, reservoir.raw_level, reservoir.active_high ? "high" : "low");
        return ESP_ERR_INVALID_STATE;
    }
    runtime_state_t runtime = pump_controller_get_runtime();
    if (runtime.cooldown_until > time(NULL)) {
        snprintf(reason, reason_len, "Pump cooldown is active.");
        return ESP_ERR_INVALID_STATE;
    }
    if (runtime.pump_state == PUMP_STATE_MANUAL_RUNNING || runtime.pump_state == PUMP_STATE_SCHEDULED_RUNNING) {
        snprintf(reason, reason_len, "Pump is already running.");
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(reason, reason_len, "Ready.");
    return ESP_OK;
}

esp_err_t pump_controller_scheduled_run(uint32_t duration_sec)
{
    pump_command_t cmd = {.type = CMD_SCHEDULED_RUN, .duration_sec = duration_sec};
    return xQueueSend(g_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t pump_controller_stop(stop_reason_t reason)
{
    pump_command_t cmd = {.type = CMD_STOP, .stop_reason = reason};
    return xQueueSend(g_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

runtime_state_t pump_controller_get_runtime(void)
{
    g_runtime.pump_gpio = g_settings.pump_gpio;
    g_runtime.pump_gpio_b = g_settings.pump_gpio_b;
    g_runtime.pump_gpio_level = g_cmd_a;
    g_runtime.pump_gpio_b_level = g_cmd_b;
    g_runtime.pump_sleep_gpio = g_settings.pump_sleep_gpio;
    g_runtime.pump_sleep_gpio_level = g_cmd_sleep;
    reservoir_status_t reservoir = reservoir_manager_get_status();
    g_runtime.reservoir_gpio = reservoir.gpio;
    g_runtime.reservoir_raw_level = reservoir.raw_level;
    g_runtime.reservoir_bypass = reservoir.bypass;
    g_runtime.reservoir_active_high = reservoir.active_high;
    if (g_runtime.pump_state == PUMP_STATE_IDLE && reservoir.ok) {
        g_runtime.block_reason[0] = '\0';
    }
    return g_runtime;
}
