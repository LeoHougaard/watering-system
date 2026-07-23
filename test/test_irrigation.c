#include "unity.h"
#include "irrigation_model.h"

TEST_CASE("default settings match acceptance defaults", "[settings]")
{
    system_settings_t s;
    model_defaults_settings(&s);
    TEST_ASSERT_TRUE(s.auto_mode_enabled);
    TEST_ASSERT_EQUAL_STRING("06:00", s.watering_window.start);
    TEST_ASSERT_EQUAL_UINT32(120, s.schedule.duration_sec);
    TEST_ASSERT_EQUAL_UINT32(1200, s.manual_max_run_sec);
    TEST_ASSERT_EQUAL_UINT32(300, s.scheduled_max_run_sec);
    TEST_ASSERT_EQUAL_UINT32(300, s.pump_cooldown_sec);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.seasonal_multiplier);
    TEST_ASSERT_EQUAL_UINT32(5, s.moisture_live_interval_sec);
    TEST_ASSERT_EQUAL_UINT32(3600, s.moisture_sample_interval_sec);
    for (int i = 0; i < 7; i++) TEST_ASSERT_TRUE(s.schedule.days[i]);
}

TEST_CASE("seven planter defaults are editable records", "[planters]")
{
    planter_config_t planters[PLANTER_COUNT];
    model_defaults_planters(planters);
    for (int i = 0; i < PLANTER_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(i + 1, planters[i].id);
        TEST_ASSERT_TRUE(planters[i].enabled);
        TEST_ASSERT_EQUAL_STRING("medium", planters[i].water_need);
        TEST_ASSERT_EQUAL_STRING("unknown", planters[i].dripper_setting);
    }
}

TEST_CASE("pump states serialize to required API strings", "[model]")
{
    TEST_ASSERT_EQUAL_STRING("IDLE", pump_state_to_string(PUMP_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("MANUAL_RUNNING", pump_state_to_string(PUMP_STATE_MANUAL_RUNNING));
    TEST_ASSERT_EQUAL_STRING("SCHEDULED_RUNNING", pump_state_to_string(PUMP_STATE_SCHEDULED_RUNNING));
    TEST_ASSERT_EQUAL_STRING("COOLDOWN", pump_state_to_string(PUMP_STATE_COOLDOWN));
    TEST_ASSERT_EQUAL_STRING("LOCKED_OUT_LOW_RESERVOIR", pump_state_to_string(PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR));
    TEST_ASSERT_EQUAL_STRING("LOCKED_OUT_MAX_RUNTIME", pump_state_to_string(PUMP_STATE_LOCKED_OUT_MAX_RUNTIME));
}

TEST_CASE("settings JSON is clamped to safe operational limits", "[settings]")
{
    system_settings_t s;
    model_defaults_settings(&s);
    cJSON *json = cJSON_Parse("{\"manual_max_run_sec\":-1,\"scheduled_max_run_sec\":999999,\"moisture_live_interval_sec\":0,\"moisture_sample_interval_sec\":1,\"moisture_history_days\":999,\"moisture_gpio\":48,\"pump_gpio\":-3,\"seasonal_multiplier\":9}");
    TEST_ASSERT_TRUE(json_to_settings(json, &s));
    cJSON_Delete(json);
    TEST_ASSERT_EQUAL_UINT32(1, s.manual_max_run_sec);
    TEST_ASSERT_EQUAL_UINT32(3600, s.scheduled_max_run_sec);
    TEST_ASSERT_EQUAL_UINT32(1, s.moisture_live_interval_sec);
    TEST_ASSERT_EQUAL_UINT32(60, s.moisture_sample_interval_sec);
    TEST_ASSERT_EQUAL_UINT32(30, s.moisture_history_days);
    TEST_ASSERT_EQUAL_INT(1, s.moisture_gpio);
    TEST_ASSERT_EQUAL_INT(5, s.pump_gpio);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, s.seasonal_multiplier);
}
