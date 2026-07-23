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

TEST_CASE("weather adjustment follows rolling water demand", "[weather]")
{
    weather_observation_t dry_hot = {
        .previous_et0_mm = 4.0f,
        .forecast_et0_mm = 5.0f,
    };
    weather_adjustment_t dry = model_weather_adjustment(&dry_hot, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.35f, dry.demand_et0_mm);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.35f, dry.multiplier);
    TEST_ASSERT_FALSE(dry.skipped_for_rain);

    weather_observation_t damp = {
        .recent_rain_mm = 2.0f,
        .previous_et0_mm = 3.0f,
        .forecast_et0_mm = 3.0f,
    };
    weather_adjustment_t reduced = model_weather_adjustment(&damp, false);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.6f, reduced.effective_rain_mm);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, reduced.multiplier);
}

TEST_CASE("rain skip requires substantial recent or confident near-term rain", "[weather]")
{
    weather_observation_t likely_rain = {
        .forecast_rain_12h_mm = 4.0f,
        .forecast_rain_24h_mm = 4.0f,
        .forecast_rain_probability_pct = 70.0f,
        .previous_et0_mm = 3.0f,
        .forecast_et0_mm = 3.0f,
    };
    weather_adjustment_t skipped = model_weather_adjustment(&likely_rain, true);
    TEST_ASSERT_TRUE(skipped.skipped_for_rain);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, skipped.multiplier);

    likely_rain.forecast_rain_probability_pct = 69.0f;
    weather_adjustment_t not_skipped = model_weather_adjustment(&likely_rain, true);
    TEST_ASSERT_FALSE(not_skipped.skipped_for_rain);
    TEST_ASSERT_TRUE(not_skipped.multiplier >= 0.5f);
}

TEST_CASE("stale weather can never alter scheduled duration", "[weather]")
{
    system_settings_t s;
    model_defaults_settings(&s);
    s.weather_adjust_enabled = true;
    s.weather_last_multiplier = 0.0f;
    s.weather_data_version = WEATHER_DATA_VERSION;
    time_t now = 1800000000;
    s.weather_last_update = now - WEATHER_DATA_MAX_AGE_SEC;
    TEST_ASSERT_TRUE(model_weather_data_is_fresh(&s, now));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, model_weather_multiplier(&s, now));

    s.weather_last_update--;
    TEST_ASSERT_FALSE(model_weather_data_is_fresh(&s, now));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, model_weather_multiplier(&s, now));
    s.weather_last_update = now + 1;
    TEST_ASSERT_FALSE(model_weather_data_is_fresh(&s, now));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, model_weather_multiplier(&s, now));
}
