#include "scheduler.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"
#include "pump_controller.h"
#include "moisture_manager.h"

static const char *TAG = "scheduler";
static char g_next_run[64] = "calculating";
static int g_last_run_yday[MAX_SCHEDULE_SLOTS] = {-1};
static int g_last_run_minute[MAX_SCHEDULE_SLOTS] = {-1};
static struct {
    bool active;
    int target;
    int daily_minute;
    int baseline;
    uint32_t remaining;
    time_t started_at;
    time_t check_at;
    time_t expires_at;
} g_target_cycle;
static volatile bool g_cancel_target;
static char g_target_status[112] = "Waiting for daily time.";

void scheduler_cancel_target_cycle(void)
{
    g_cancel_target = true;
}

const char *scheduler_target_status(void)
{
    return config_store_moisture_target() ? g_target_status : "Schedule only.";
}

bool scheduler_target_cycle_active(void)
{
    return g_target_cycle.active;
}

static bool usable_moisture(const system_settings_t *s, moisture_reading_t r, time_t now)
{
    return s->moisture_sensor_enabled && r.enabled && r.percent >= 0 && r.percent <= 100 &&
        r.samples_collected >= s->moisture_average_count && r.updated_at > 0 &&
        r.updated_at <= now && now - r.updated_at <= 300;
}

static void target_message(const char *text)
{
    snprintf(g_target_status, sizeof(g_target_status), "%s", text);
}

static void target_pulse(time_t now, int baseline)
{
    uint32_t seconds = g_target_cycle.remaining > 120 ? 120 : g_target_cycle.remaining;
    if (pump_controller_scheduled_run(seconds) != ESP_OK) {
        g_target_cycle.active = false;
        target_message("Watering request failed.");
        return;
    }
    g_target_cycle.remaining -= seconds;
    g_target_cycle.baseline = baseline;
    g_target_cycle.check_at = now + seconds + 30 * 60;
    target_message("Watering and soaking.");
}

static void advance_target_cycle(const system_settings_t *s, time_t now)
{
    if (!g_target_cycle.active) return;
    runtime_state_t runtime = pump_controller_get_runtime();
    if (!s->auto_mode_enabled ||
        config_store_moisture_target() != g_target_cycle.target ||
        config_store_target_time() != g_target_cycle.daily_minute ||
        now >= g_target_cycle.expires_at) {
        g_target_cycle.active = false;
        target_message("Waiting for daily time.");
        return;
    }
    if (runtime.pump_state == PUMP_STATE_MANUAL_RUNNING ||
        (runtime.pump_state != PUMP_STATE_SCHEDULED_RUNNING &&
         runtime.last_run_at >= g_target_cycle.started_at && strcmp(runtime.last_run_reason, "timer_complete") != 0)) {
        g_target_cycle.active = false;
        target_message("Waiting for daily time.");
        return;
    }
    if (now < g_target_cycle.check_at) return;
    if (runtime.pump_state != PUMP_STATE_IDLE || !runtime.reservoir_ok) {
        g_target_cycle.active = false;
        target_message("Watering blocked.");
        return;
    }
    moisture_reading_t reading = moisture_manager_get_latest();
    bool usable = usable_moisture(s, reading, now);
    if (usable && reading.percent >= g_target_cycle.target) {
        g_target_cycle.active = false;
        target_message("Target reached.");
    } else if (!g_target_cycle.remaining) {
        g_target_cycle.active = false;
        target_message("Daily limit reached.");
    } else if (!usable || reading.percent < g_target_cycle.baseline + 3) {
        /* An unreliable probe must not request unlimited pulses. Complete only
           the unused part of the daily allowance. */
        uint32_t remaining = g_target_cycle.remaining;
        g_target_cycle.active = false;
        if (remaining > s->scheduled_max_run_sec) remaining = s->scheduled_max_run_sec;
        if (pump_controller_scheduled_run(remaining) == ESP_OK) {
            target_message("No probe response. Using daily allowance.");
        } else {
            target_message("Watering request failed.");
        }
        history_store_log_diagnostic(g_target_status);
    } else {
        target_pulse(now, reading.percent);
    }
}

static bool clock_is_valid(time_t now)
{
    return now >= 1700000000;
}

static int parse_hhmm(const char *text)
{
    int h = 0, m = 0;
    sscanf(text, "%d:%d", &h, &m);
    return h * 60 + m;
}

/* Target mode has one daily time. Schedule-only mode retains weekday slots. */
static const schedule_slot_t *watering_slot(const system_settings_t *s, int index, schedule_slot_t *daily)
{
    if (!config_store_moisture_target()) return &s->schedule_slots[index];
    if (index != 0) return NULL;
    unsigned minute = (unsigned)config_store_target_time() % 1440;
    *daily = (schedule_slot_t){.enabled = true, .duration_sec = s->scheduled_max_run_sec};
    snprintf(daily->start, sizeof(daily->start), "%02u:%02u", minute / 60, minute % 60);
    for (int day = 0; day < 7; day++) daily->days[day] = true;
    return daily;
}

static void format_duration(uint32_t total_seconds, char *out, size_t out_len)
{
    const uint32_t divisors[] = {86400, 3600, 60, 1};
    const char *names[] = {"day", "hour", "minute", "second"};
    size_t shown = 0;
    out[0] = '\0';

    for (size_t i = 0; i < 4 && shown < 2; i++) {
        uint32_t value = total_seconds / divisors[i];
        total_seconds %= divisors[i];
        if (value == 0) continue;
        size_t used = strlen(out);
        snprintf(out + used, out_len - used, "%s%lu %s%s", shown ? " and " : "",
                 (unsigned long)value, names[i], value == 1 ? "" : "s");
        shown++;
    }
    if (shown == 0) snprintf(out, out_len, "0 seconds");
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

void scheduler_refresh_next_run(void)
{
    system_settings_t s;
    config_store_get_settings(&s);
    time_t now = time(NULL);
    if (!clock_is_valid(now)) {
        snprintf(g_next_run, sizeof(g_next_run), "Waiting for valid clock");
        return;
    }
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char next_start[6] = "";
    int now_min = tm_now.tm_hour * 60 + tm_now.tm_min;
    int best_delta = 7 * 24 * 60;
    for (int i = 0; i < MAX_SCHEDULE_SLOTS; i++) {
        schedule_slot_t daily;
        const schedule_slot_t *slot = watering_slot(&s, i, &daily);
        if (!slot) break;
        if (!slot->enabled || slot->start[0] == '\0' || slot->duration_sec == 0) continue;
        for (int d = 0; d < 7; d++) {
            int wday = (tm_now.tm_wday + d) % 7;
            if (!slot->days[wday]) continue;
            int delta = d * 24 * 60 + parse_hhmm(slot->start) - now_min;
            if (delta < 0) delta += 7 * 24 * 60;
            if (delta >= 0 && delta < best_delta) {
                best_delta = delta;
                snprintf(next_start, sizeof(next_start), "%s", slot->start);
            }
        }
    }
    if (next_start[0]) {
        char duration[48];
        format_duration((uint32_t)best_delta * 60, duration, sizeof(duration));
        snprintf(g_next_run, sizeof(g_next_run), "%s in %s", next_start, duration);
    }
    else snprintf(g_next_run, sizeof(g_next_run), "No enabled schedule");
}

static void scheduler_task(void *arg)
{
    while (true) {
        system_settings_t s;
        config_store_get_settings(&s);
        time_t now = time(NULL);
        if (!clock_is_valid(now)) {
            scheduler_refresh_next_run();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        int now_min = tm_now.tm_hour * 60 + tm_now.tm_min;
        scheduler_refresh_next_run();
        if (g_cancel_target) {
            g_cancel_target = false;
            g_target_cycle.active = false;
            target_message("Waiting for daily time.");
        }
        advance_target_cycle(&s, now);

        for (int i = 0; i < MAX_SCHEDULE_SLOTS; i++) {
            schedule_slot_t daily;
            const schedule_slot_t *slot = watering_slot(&s, i, &daily);
            if (!slot) break;
            if (!slot->enabled || !slot->days[tm_now.tm_wday] || !slot_inside_start_minute(slot, &tm_now)) continue;
            if (g_last_run_yday[i] == tm_now.tm_yday && g_last_run_minute[i] == now_min) continue;
            g_last_run_yday[i] = tm_now.tm_yday;
            g_last_run_minute[i] = now_min;
            int target = config_store_moisture_target();
            /* Claim every daily check, including moisture skips and probe fallback. */
            if (target && config_store_claim_target_cycle(now / 60) != ESP_OK) continue;
            if (!s.auto_mode_enabled) {
                log_skip(STOP_AUTO_DISABLED);

            } else {
                uint32_t duration = slot->duration_sec;
                if (duration > s.scheduled_max_run_sec) duration = s.scheduled_max_run_sec;
                if (duration < 1) duration = 1;
                moisture_reading_t reading = moisture_manager_get_latest();
                if (target && usable_moisture(&s, reading, now)) {
                    if (g_target_cycle.active) continue;
                    if (reading.percent >= target - 5) {
                        target_message("Moisture near target. Skipped.");
                        log_skip(STOP_MOISTURE_TARGET);
                    } else {
                        runtime_state_t runtime = pump_controller_get_runtime();
                        if (runtime.pump_state != PUMP_STATE_IDLE || !runtime.reservoir_ok) continue;
                        g_target_cycle.active = true;
                        g_target_cycle.target = target;
                        g_target_cycle.daily_minute = config_store_target_time();
                        g_target_cycle.remaining = duration;
                        g_target_cycle.started_at = now;
                        g_target_cycle.expires_at = now + 3 * 60 * 60;
                        target_pulse(now, reading.percent);
                    }
                } else {
                    target_message(target ? "Probe unavailable. Using daily allowance." :
                        "Schedule only.");
                    pump_controller_scheduled_run(duration);
                }
                ESP_LOGI(TAG, "scheduled watering requested for %lu sec", (unsigned long)duration);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

esp_err_t scheduler_start(void)
{
    return xTaskCreate(scheduler_task, "Scheduler Task", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

const char *scheduler_next_run_text(void)
{
    return g_next_run;
}
