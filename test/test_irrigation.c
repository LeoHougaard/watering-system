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
