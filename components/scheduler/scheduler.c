#include "scheduler.h"

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "config_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"
#include "pump_controller.h"

static const char *TAG = "scheduler";
static char g_next_run[48] = "calculating";
static int g_last_run_yday[MAX_SCHEDULE_SLOTS] = {-1};
static int g_last_run_minute[MAX_SCHEDULE_SLOTS] = {-1};

static int parse_hhmm(const char *text)
{
    int h = 0, m = 0;
    sscanf(text, "%d:%d", &h, &m);
    return h * 60 + m;
}

static bool slot_inside_start_minute(const schedule_slot_t *slot, const struct tm *tm)
{
    int now_min = tm->tm_hour * 60 + tm->tm_min;
    return now_min == parse_hhmm(slot->start);
}

static void log_skip(stop_reason_t reason)
{
    watering_event_t e = {
        .timestamp = time(NULL),
        .type = RUN_TYPE_SKIPPED,
        .duration_sec = 0,
        .reservoir_ok_at_start = true,
        .completed = false,
        .stopped_reason = reason,
    };
    history_store_log_watering(&e);
}

static void scheduler_task(void *arg)
{
    while (true) {
        system_settings_t s;
        config_store_get_settings(&s);
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        const schedule_slot_t *next = NULL;
        int now_min = tm_now.tm_hour * 60 + tm_now.tm_min;
        int best_delta = 7 * 24 * 60;
        for (int i = 0; i < MAX_SCHEDULE_SLOTS; i++) {
            const schedule_slot_t *slot = &s.schedule_slots[i];
            if (!slot->enabled || slot->start[0] == '\0' || slot->duration_sec == 0) continue;
            for (int d = 0; d < 7; d++) {
                int wday = (tm_now.tm_wday + d) % 7;
                if (!slot->days[wday]) continue;
                int delta = d * 24 * 60 + parse_hhmm(slot->start) - now_min;
                if (delta < 0) delta += 7 * 24 * 60;
                if (delta < best_delta) {
                    best_delta = delta;
                    next = slot;
                }
                break;
            }
        }
        if (next) snprintf(g_next_run, sizeof(g_next_run), "%s in %d min", next->start, best_delta);
        else snprintf(g_next_run, sizeof(g_next_run), "No enabled schedule");

        for (int i = 0; i < MAX_SCHEDULE_SLOTS; i++) {
            const schedule_slot_t *slot = &s.schedule_slots[i];
            if (!slot->enabled || !slot->days[tm_now.tm_wday] || !slot_inside_start_minute(slot, &tm_now)) continue;
            if (g_last_run_yday[i] == tm_now.tm_yday && g_last_run_minute[i] == now_min) continue;
            if (!s.auto_mode_enabled) {
                log_skip(STOP_AUTO_DISABLED);
            } else if (s.rain_delay_enabled && s.rain_delay_until > now) {
                log_skip(STOP_RAIN_DELAY);
            } else {
                float multiplier = s.seasonal_multiplier;
                if (s.weather_adjust_enabled && s.weather_last_update > 0) multiplier *= s.weather_last_multiplier;
                if (multiplier <= 0.01f) {
                    log_skip(STOP_RAIN_DELAY);
                    g_last_run_yday[i] = tm_now.tm_yday;
                    g_last_run_minute[i] = now_min;
                    continue;
                }
                uint32_t duration = (uint32_t)(slot->duration_sec * multiplier);
                if (duration > s.scheduled_max_run_sec) duration = s.scheduled_max_run_sec;
                if (duration < 1) duration = 1;
                pump_controller_scheduled_run(duration);
                g_last_run_yday[i] = tm_now.tm_yday;
                g_last_run_minute[i] = now_min;
                ESP_LOGI(TAG, "scheduled watering requested for %lu sec", (unsigned long)duration);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

esp_err_t scheduler_start(void)
{
    xTaskCreate(scheduler_task, "Scheduler Task", 4096, NULL, 5, NULL);
    return ESP_OK;
}

const char *scheduler_next_run_text(void)
{
    return g_next_run;
}
