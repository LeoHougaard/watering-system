#include "irrigation_model.h"

#include <stdio.h>
#include <string.h>

static uint32_t clamp_u32(uint32_t value, uint32_t min, uint32_t max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static uint32_t json_u32(const cJSON *item)
{
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0) return 0;
    if (item->valuedouble >= UINT32_MAX) return UINT32_MAX;
    return (uint32_t)item->valuedouble;
}

static bool valid_hhmm(const char *text)
{
    int hour, minute;
    char trailing;
    return text && strlen(text) == 5 && text[2] == ':' &&
           sscanf(text, "%2d:%2d%c", &hour, &minute, &trailing) == 2 &&
           hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

static void copy_json_string(const cJSON *json, const char *key, char *dst, size_t dst_len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, dst_len, "%s", item->valuestring);
    }
}

const char *pump_state_to_string(pump_state_t state)
{
    switch (state) {
    case PUMP_STATE_IDLE: return "IDLE";
    case PUMP_STATE_MANUAL_RUNNING: return "MANUAL_RUNNING";
    case PUMP_STATE_SCHEDULED_RUNNING: return "SCHEDULED_RUNNING";
    case PUMP_STATE_COOLDOWN: return "COOLDOWN";
    case PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR: return "LOCKED_OUT_LOW_RESERVOIR";
    case PUMP_STATE_LOCKED_OUT_MAX_RUNTIME: return "LOCKED_OUT_MAX_RUNTIME";
    case PUMP_STATE_FAULT: return "FAULT";
    default: return "FAULT";
    }
}

const char *event_type_to_string(watering_event_type_t type)
{
    switch (type) {
    case RUN_TYPE_MANUAL: return "manual";
    case RUN_TYPE_SCHEDULED: return "scheduled";
    case RUN_TYPE_SKIPPED: return "skipped";
    case RUN_TYPE_RESERVOIR_LOW: return "reservoir_low";
    default: return "unknown";
    }
}

const char *stop_reason_to_string(stop_reason_t reason)
{
    switch (reason) {
    case STOP_TIMER_COMPLETE: return "timer_complete";
    case STOP_MANUAL: return "manual_stop";
    case STOP_LOW_RESERVOIR: return "low_reservoir";
    case STOP_FAULT: return "fault";
    case STOP_COOLDOWN: return "cooldown";
    case STOP_RAIN_DELAY: return "rain_delay";
    case STOP_AUTO_DISABLED: return "auto_disabled";
    case STOP_MAX_RUNTIME: return "max_runtime";
    case STOP_MOISTURE_TARGET: return "moisture_target";
    default: return "unknown";
    }
}

void model_defaults_settings(system_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    snprintf(settings->timezone, sizeof(settings->timezone), "America/Vancouver");
    settings->auto_mode_enabled = true;
    snprintf(settings->watering_window.start, sizeof(settings->watering_window.start), "06:00");
    snprintf(settings->watering_window.end, sizeof(settings->watering_window.end), "09:00");
    for (int i = 0; i < 7; i++) settings->schedule.days[i] = true;
    settings->schedule.duration_sec = 120;
    settings->schedule_slots[0].enabled = true;
    for (int i = 0; i < 7; i++) settings->schedule_slots[0].days[i] = true;
    snprintf(settings->schedule_slots[0].start, sizeof(settings->schedule_slots[0].start), "06:00");
    settings->schedule_slots[0].duration_sec = 120;
    settings->manual_max_run_sec = 1200;
    settings->scheduled_max_run_sec = 300;
    settings->pump_cooldown_sec = 300;
    settings->reservoir_sensor_enabled = false;
    settings->reservoir_sensor_bypass = false;
    settings->reservoir_active_high = true;
    settings->reservoir_debounce_ms = 750;
    settings->moisture_sensor_enabled = false;
    settings->moisture_gpio = 1;
    settings->moisture_dry_raw = 3000;
    settings->moisture_wet_raw = 1200;
    settings->moisture_sample_interval_sec = 3600;
    settings->moisture_history_days = 365;
    settings->pump_gpio = 5;
    settings->pump_gpio_b = 6;
    settings->pump_sleep_gpio = -1;
    settings->reservoir_gpio = 4;
    snprintf(settings->auth_pin_hash, sizeof(settings->auth_pin_hash), "local-pin-not-set");
    settings->moisture_live_interval_sec = 60;
    settings->moisture_average_count = 5;
    settings->storage_policy_version = 1;
}

cJSON *settings_to_json(const system_settings_t *s)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "timezone", s->timezone);
    cJSON_AddBoolToObject(root, "auto_mode_enabled", s->auto_mode_enabled);
    cJSON *windows = cJSON_AddArrayToObject(root, "watering_windows");
    cJSON *win = cJSON_CreateObject();
    cJSON_AddStringToObject(win, "start", s->watering_window.start);
    cJSON_AddStringToObject(win, "end", s->watering_window.end);
    cJSON_AddItemToArray(windows, win);
    cJSON *schedule = cJSON_AddObjectToObject(root, "schedule");
    const char *names[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
    cJSON *days = cJSON_AddArrayToObject(schedule, "days");
    for (int i = 0; i < 7; i++) if (s->schedule.days[i]) cJSON_AddItemToArray(days, cJSON_CreateString(names[i]));
    cJSON_AddNumberToObject(schedule, "duration_sec", s->schedule.duration_sec);
    cJSON *slots = cJSON_AddArrayToObject(root, "schedule_slots");
    for (int i = 0; i < MAX_SCHEDULE_SLOTS; i++) {
        cJSON *slot = cJSON_CreateObject();
        cJSON_AddBoolToObject(slot, "enabled", s->schedule_slots[i].enabled);
        cJSON_AddStringToObject(slot, "start", s->schedule_slots[i].start);
        cJSON_AddNumberToObject(slot, "duration_sec", s->schedule_slots[i].duration_sec);
        cJSON *slot_days = cJSON_AddArrayToObject(slot, "days");
        for (int d = 0; d < 7; d++) if (s->schedule_slots[i].days[d]) cJSON_AddItemToArray(slot_days, cJSON_CreateString(names[d]));
        cJSON_AddItemToArray(slots, slot);
    }
    cJSON_AddNumberToObject(root, "manual_max_run_sec", s->manual_max_run_sec);
    cJSON_AddNumberToObject(root, "scheduled_max_run_sec", s->scheduled_max_run_sec);
    cJSON_AddNumberToObject(root, "pump_cooldown_sec", s->pump_cooldown_sec);
    cJSON_AddBoolToObject(root, "reservoir_sensor_enabled", s->reservoir_sensor_enabled);
    cJSON_AddBoolToObject(root, "reservoir_sensor_bypass", s->reservoir_sensor_bypass);
    cJSON_AddBoolToObject(root, "reservoir_active_high", s->reservoir_active_high);
    cJSON_AddNumberToObject(root, "reservoir_debounce_ms", s->reservoir_debounce_ms);
    cJSON_AddBoolToObject(root, "moisture_sensor_enabled", s->moisture_sensor_enabled);
    cJSON_AddNumberToObject(root, "moisture_gpio", s->moisture_gpio);
    cJSON_AddNumberToObject(root, "moisture_dry_raw", s->moisture_dry_raw);
    cJSON_AddNumberToObject(root, "moisture_wet_raw", s->moisture_wet_raw);
    cJSON_AddNumberToObject(root, "moisture_sample_interval_sec", s->moisture_sample_interval_sec);
    cJSON_AddNumberToObject(root, "moisture_live_interval_sec", s->moisture_live_interval_sec);
    cJSON_AddNumberToObject(root, "moisture_average_count", s->moisture_average_count);
    cJSON_AddNumberToObject(root, "moisture_history_days", s->moisture_history_days);
    cJSON_AddNumberToObject(root, "storage_policy_version", s->storage_policy_version);
    cJSON_AddNumberToObject(root, "pump_gpio", s->pump_gpio);
    cJSON_AddNumberToObject(root, "pump_gpio_b", s->pump_gpio_b);
    cJSON_AddNumberToObject(root, "pump_sleep_gpio", s->pump_sleep_gpio);
    cJSON_AddNumberToObject(root, "reservoir_gpio", s->reservoir_gpio);
    return root;
}

cJSON *runtime_to_json(const runtime_state_t *runtime)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "pump_state", pump_state_to_string(runtime->pump_state));
    cJSON_AddBoolToObject(root, "reservoir_ok", runtime->reservoir_ok);
    cJSON_AddNumberToObject(root, "current_run_remaining_sec", runtime->current_run_remaining_sec);
    cJSON_AddNumberToObject(root, "pump_gpio", runtime->pump_gpio);
    cJSON_AddNumberToObject(root, "pump_gpio_b", runtime->pump_gpio_b);
    cJSON_AddNumberToObject(root, "pump_gpio_level", runtime->pump_gpio_level);
    cJSON_AddNumberToObject(root, "pump_gpio_b_level", runtime->pump_gpio_b_level);
    cJSON_AddNumberToObject(root, "pump_sleep_gpio", runtime->pump_sleep_gpio);
    cJSON_AddNumberToObject(root, "pump_sleep_gpio_level", runtime->pump_sleep_gpio_level);
    cJSON_AddNumberToObject(root, "reservoir_gpio", runtime->reservoir_gpio);
    cJSON_AddNumberToObject(root, "reservoir_raw_level", runtime->reservoir_raw_level);
    cJSON_AddBoolToObject(root, "reservoir_bypass", runtime->reservoir_bypass);
    cJSON_AddBoolToObject(root, "reservoir_active_high", runtime->reservoir_active_high);
    cJSON_AddStringToObject(root, "block_reason", runtime->block_reason);
    if (runtime->last_run_at > 0) cJSON_AddNumberToObject(root, "last_run_at", (double)runtime->last_run_at);
    else cJSON_AddNullToObject(root, "last_run_at");
    cJSON_AddNumberToObject(root, "last_run_duration_sec", runtime->last_run_duration_sec);
    cJSON_AddStringToObject(root, "last_run_reason", runtime->last_run_reason);
    cJSON *faults = cJSON_AddArrayToObject(root, "fault_flags");
    for (size_t i = 0; i < runtime->fault_count; i++) cJSON_AddItemToArray(faults, cJSON_CreateString(runtime->fault_flags[i]));
    return root;
}

bool json_to_settings(const cJSON *json, system_settings_t *s)
{
    if (!cJSON_IsObject(json)) return false;
    copy_json_string(json, "timezone", s->timezone, sizeof(s->timezone));
    const cJSON *auto_mode = cJSON_GetObjectItemCaseSensitive(json, "auto_mode_enabled");
    if (cJSON_IsBool(auto_mode)) s->auto_mode_enabled = cJSON_IsTrue(auto_mode);
    const cJSON *schedule = cJSON_GetObjectItemCaseSensitive(json, "schedule");
    if (cJSON_IsObject(schedule)) {
        const cJSON *duration = cJSON_GetObjectItemCaseSensitive(schedule, "duration_sec");
        if (cJSON_IsNumber(duration)) s->schedule.duration_sec = json_u32(duration);
        const cJSON *days = cJSON_GetObjectItemCaseSensitive(schedule, "days");
        if (cJSON_IsArray(days)) {
            memset(s->schedule.days, 0, sizeof(s->schedule.days));
            const char *names[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
            cJSON *day = NULL;
            cJSON_ArrayForEach(day, days) {
                if (!cJSON_IsString(day)) continue;
                for (int i = 0; i < 7; i++) if (strcmp(day->valuestring, names[i]) == 0) s->schedule.days[i] = true;
            }
        }
    }
    const cJSON *windows = cJSON_GetObjectItemCaseSensitive(json, "watering_windows");
    const cJSON *win = cJSON_IsArray(windows) ? cJSON_GetArrayItem(windows, 0) : NULL;
    if (cJSON_IsObject(win)) {
        char start[sizeof(s->watering_window.start)] = {0};
        char end[sizeof(s->watering_window.end)] = {0};
        copy_json_string(win, "start", start, sizeof(start));
        copy_json_string(win, "end", end, sizeof(end));
        if (valid_hhmm(start)) snprintf(s->watering_window.start, sizeof(s->watering_window.start), "%s", start);
        if (valid_hhmm(end)) snprintf(s->watering_window.end, sizeof(s->watering_window.end), "%s", end);
    }
    const cJSON *slots = cJSON_GetObjectItemCaseSensitive(json, "schedule_slots");
    if (cJSON_IsArray(slots)) {
        memset(s->schedule_slots, 0, sizeof(s->schedule_slots));
        const char *names[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
        for (int i = 0; i < MAX_SCHEDULE_SLOTS; i++) {
            const cJSON *slot = cJSON_GetArrayItem(slots, i);
            if (!cJSON_IsObject(slot)) continue;
            const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(slot, "enabled");
            const cJSON *duration = cJSON_GetObjectItemCaseSensitive(slot, "duration_sec");
            const cJSON *days = cJSON_GetObjectItemCaseSensitive(slot, "days");
            if (cJSON_IsBool(enabled)) s->schedule_slots[i].enabled = cJSON_IsTrue(enabled);
            char start[sizeof(s->schedule_slots[i].start)] = {0};
            copy_json_string(slot, "start", start, sizeof(start));
            if (valid_hhmm(start)) snprintf(s->schedule_slots[i].start, sizeof(s->schedule_slots[i].start), "%s", start);
            if (cJSON_IsNumber(duration)) s->schedule_slots[i].duration_sec = json_u32(duration);
            if (cJSON_IsArray(days)) {
                cJSON *day = NULL;
                cJSON_ArrayForEach(day, days) {
                    if (!cJSON_IsString(day)) continue;
                    for (int d = 0; d < 7; d++) if (strcmp(day->valuestring, names[d]) == 0) s->schedule_slots[i].days[d] = true;
                }
            }
        }
    }
    const char *numeric[] = {"manual_max_run_sec", "scheduled_max_run_sec", "pump_cooldown_sec", "reservoir_debounce_ms", "moisture_dry_raw", "moisture_wet_raw", "moisture_sample_interval_sec", "moisture_live_interval_sec", "moisture_history_days", "moisture_average_count"};
    uint32_t *values[] = {&s->manual_max_run_sec, &s->scheduled_max_run_sec, &s->pump_cooldown_sec, &s->reservoir_debounce_ms, &s->moisture_dry_raw, &s->moisture_wet_raw, &s->moisture_sample_interval_sec, &s->moisture_live_interval_sec, &s->moisture_history_days, &s->moisture_average_count};
    for (size_t i = 0; i < sizeof(numeric) / sizeof(numeric[0]); i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, numeric[i]);
        if (cJSON_IsNumber(v)) *values[i] = json_u32(v);
    }
    const cJSON *pump_gpio = cJSON_GetObjectItemCaseSensitive(json, "pump_gpio");
    if (cJSON_IsNumber(pump_gpio) && pump_gpio->valueint >= 0 && pump_gpio->valueint <= 48) s->pump_gpio = pump_gpio->valueint;
    const cJSON *pump_gpio_b = cJSON_GetObjectItemCaseSensitive(json, "pump_gpio_b");
    if (cJSON_IsNumber(pump_gpio_b) && pump_gpio_b->valueint >= 0 && pump_gpio_b->valueint <= 48) s->pump_gpio_b = pump_gpio_b->valueint;
    const cJSON *pump_sleep_gpio = cJSON_GetObjectItemCaseSensitive(json, "pump_sleep_gpio");
    if (cJSON_IsNumber(pump_sleep_gpio) && pump_sleep_gpio->valueint >= -1 && pump_sleep_gpio->valueint <= 48) s->pump_sleep_gpio = pump_sleep_gpio->valueint;
    const cJSON *reservoir_gpio = cJSON_GetObjectItemCaseSensitive(json, "reservoir_gpio");
    if (cJSON_IsNumber(reservoir_gpio) && reservoir_gpio->valueint >= 0 && reservoir_gpio->valueint <= 48) s->reservoir_gpio = reservoir_gpio->valueint;
    const cJSON *moisture_gpio = cJSON_GetObjectItemCaseSensitive(json, "moisture_gpio");
    if (cJSON_IsNumber(moisture_gpio) && moisture_gpio->valueint >= 1 && moisture_gpio->valueint <= 10) s->moisture_gpio = moisture_gpio->valueint;
    const cJSON *res_enabled = cJSON_GetObjectItemCaseSensitive(json, "reservoir_sensor_enabled");
    if (cJSON_IsBool(res_enabled)) s->reservoir_sensor_enabled = cJSON_IsTrue(res_enabled);
    const cJSON *res_bypass = cJSON_GetObjectItemCaseSensitive(json, "reservoir_sensor_bypass");
    if (cJSON_IsBool(res_bypass)) s->reservoir_sensor_bypass = cJSON_IsTrue(res_bypass);
    if (s->reservoir_sensor_bypass) s->reservoir_sensor_enabled = true;
    if (!s->reservoir_sensor_enabled) s->reservoir_sensor_bypass = false;
    const cJSON *res_active = cJSON_GetObjectItemCaseSensitive(json, "reservoir_active_high");
    if (cJSON_IsBool(res_active)) s->reservoir_active_high = cJSON_IsTrue(res_active);
    const cJSON *moisture_enabled = cJSON_GetObjectItemCaseSensitive(json, "moisture_sensor_enabled");
    if (cJSON_IsBool(moisture_enabled)) s->moisture_sensor_enabled = cJSON_IsTrue(moisture_enabled);

    s->schedule.duration_sec = clamp_u32(s->schedule.duration_sec, 1, 3600);
    for (int i = 0; i < MAX_SCHEDULE_SLOTS; i++) {
        s->schedule_slots[i].duration_sec = clamp_u32(s->schedule_slots[i].duration_sec, 1, 3600);
        if (s->schedule_slots[i].enabled && !valid_hhmm(s->schedule_slots[i].start)) {
            s->schedule_slots[i].enabled = false;
        }
    }
    s->manual_max_run_sec = clamp_u32(s->manual_max_run_sec, 1, 3600);
    s->scheduled_max_run_sec = clamp_u32(s->scheduled_max_run_sec, 1, 3600);
    s->pump_cooldown_sec = clamp_u32(s->pump_cooldown_sec, 0, 86400);
    s->reservoir_debounce_ms = clamp_u32(s->reservoir_debounce_ms, 0, 10000);
    s->moisture_dry_raw = clamp_u32(s->moisture_dry_raw, 0, 4095);
    s->moisture_wet_raw = clamp_u32(s->moisture_wet_raw, 0, 4095);
    s->moisture_sample_interval_sec = clamp_u32(s->moisture_sample_interval_sec, 3600, 86400);
    s->moisture_live_interval_sec = clamp_u32(s->moisture_live_interval_sec, 1, 3600);
    s->moisture_history_days = clamp_u32(s->moisture_history_days, 1, 365);
    s->moisture_average_count = clamp_u32(s->moisture_average_count, 1, 20);
    return true;
}
