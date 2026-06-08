#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cJSON.h"

#define PLANTER_COUNT 7
#define MAX_RECOMMENDATIONS 16
#define MAX_FAULT_FLAGS 8
#define MAX_SCHEDULE_SLOTS 8

typedef enum {
    PUMP_STATE_IDLE,
    PUMP_STATE_MANUAL_RUNNING,
    PUMP_STATE_SCHEDULED_RUNNING,
    PUMP_STATE_COOLDOWN,
    PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR,
    PUMP_STATE_LOCKED_OUT_MAX_RUNTIME,
    PUMP_STATE_FAULT,
} pump_state_t;

typedef enum {
    RUN_TYPE_MANUAL,
    RUN_TYPE_SCHEDULED,
    RUN_TYPE_SKIPPED,
    RUN_TYPE_RESERVOIR_LOW,
} watering_event_type_t;

typedef enum {
    STOP_TIMER_COMPLETE,
    STOP_MANUAL,
    STOP_LOW_RESERVOIR,
    STOP_FAULT,
    STOP_COOLDOWN,
    STOP_RAIN_DELAY,
    STOP_AUTO_DISABLED,
    STOP_MAX_RUNTIME,
} stop_reason_t;

typedef struct {
    int id;
    char name[32];
    char plant_type[32];
    char container_size[24];
    char sun_exposure[24];
    char water_need[12];
    char dripper_setting[12];
    bool enabled;
    char notes[128];
} planter_config_t;

typedef struct {
    char start[6];
    char end[6];
} watering_window_t;

typedef struct {
    bool days[7];
    uint32_t duration_sec;
} schedule_config_t;

typedef struct {
    bool enabled;
    bool days[7];
    char start[6];
    uint32_t duration_sec;
} schedule_slot_t;

typedef struct {
    char timezone[48];
    bool auto_mode_enabled;
    watering_window_t watering_window;
    schedule_config_t schedule;
    schedule_slot_t schedule_slots[MAX_SCHEDULE_SLOTS];
    uint32_t manual_max_run_sec;
    uint32_t scheduled_max_run_sec;
    uint32_t pump_cooldown_sec;
    bool rain_delay_enabled;
    time_t rain_delay_until;
    float seasonal_multiplier;
    bool reservoir_sensor_enabled;
    bool reservoir_sensor_bypass;
    bool reservoir_active_high;
    uint32_t reservoir_debounce_ms;
    bool moisture_sensor_enabled;
    int moisture_gpio;
    uint32_t moisture_dry_raw;
    uint32_t moisture_wet_raw;
    uint32_t moisture_sample_interval_sec;
    uint32_t moisture_history_days;
    bool weather_adjust_enabled;
    bool weather_skip_on_rain;
    float weather_hot_multiplier;
    float weather_rain_multiplier;
    float weather_last_multiplier;
    float weather_last_temp_c;
    float weather_last_precip_mm;
    time_t weather_last_update;
    int pump_gpio;
    int pump_gpio_b;
    int pump_sleep_gpio;
    int reservoir_gpio;
    char auth_pin_hash[65];
} system_settings_t;

typedef struct {
    pump_state_t pump_state;
    bool reservoir_ok;
    uint32_t current_run_remaining_sec;
    int pump_gpio;
    int pump_gpio_b;
    int pump_gpio_level;
    int pump_gpio_b_level;
    int pump_sleep_gpio;
    int pump_sleep_gpio_level;
    int reservoir_gpio;
    int reservoir_raw_level;
    bool reservoir_bypass;
    bool reservoir_active_high;
    char block_reason[64];
    time_t last_run_at;
    uint32_t last_run_duration_sec;
    char last_run_reason[24];
    char fault_flags[MAX_FAULT_FLAGS][24];
    size_t fault_count;
    time_t cooldown_until;
} runtime_state_t;

typedef struct {
    time_t timestamp;
    watering_event_type_t type;
    uint32_t duration_sec;
    bool reservoir_ok_at_start;
    bool completed;
    stop_reason_t stopped_reason;
} watering_event_t;

typedef struct {
    time_t timestamp;
    int planter_id;
    char condition[24];
    char severity[12];
    char note[128];
} user_observation_t;

typedef struct {
    char id[40];
    time_t timestamp;
    char scope[12];
    int planter_id;
    char category[32];
    char severity[12];
    float confidence;
    char message[192];
    bool resolved;
    time_t snoozed_until;
} recommendation_record_t;

const char *pump_state_to_string(pump_state_t state);
const char *event_type_to_string(watering_event_type_t type);
const char *stop_reason_to_string(stop_reason_t reason);
void model_defaults_settings(system_settings_t *settings);
void model_defaults_planters(planter_config_t planters[PLANTER_COUNT]);
cJSON *settings_to_json(const system_settings_t *settings);
cJSON *runtime_to_json(const runtime_state_t *runtime);
cJSON *planter_to_json(const planter_config_t *planter);
bool json_to_planter(const cJSON *json, planter_config_t *planter);
bool json_to_settings(const cJSON *json, system_settings_t *settings);
