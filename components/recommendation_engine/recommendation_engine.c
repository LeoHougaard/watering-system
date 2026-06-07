#include "recommendation_engine.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"
#include "irrigation_model.h"

static const char *TAG = "recs";
static recommendation_record_t g_recs[MAX_RECOMMENDATIONS];
static size_t g_rec_count;

static void upsert_rec(const char *id, const char *scope, int planter_id, const char *category,
                       const char *severity, float confidence, const char *message)
{
    recommendation_record_t *r = NULL;
    bool is_new = false;
    for (size_t i = 0; i < g_rec_count; i++) {
        if (strcmp(g_recs[i].id, id) == 0) r = &g_recs[i];
    }
    if (!r && g_rec_count < MAX_RECOMMENDATIONS) {
        r = &g_recs[g_rec_count++];
        is_new = true;
    }
    if (!r) return;
    snprintf(r->id, sizeof(r->id), "%s", id);
    r->timestamp = time(NULL);
    snprintf(r->scope, sizeof(r->scope), "%s", scope);
    r->planter_id = planter_id;
    snprintf(r->category, sizeof(r->category), "%s", category);
    snprintf(r->severity, sizeof(r->severity), "%s", severity);
    r->confidence = confidence;
    snprintf(r->message, sizeof(r->message), "%s", message);
    if (is_new) {
        cJSON *log = cJSON_CreateObject();
        cJSON_AddStringToObject(log, "id", r->id);
        cJSON_AddNumberToObject(log, "timestamp", (double)r->timestamp);
        cJSON_AddStringToObject(log, "scope", r->scope);
        if (r->planter_id > 0) cJSON_AddNumberToObject(log, "planter_id", r->planter_id);
        else cJSON_AddNullToObject(log, "planter_id");
        cJSON_AddStringToObject(log, "category", r->category);
        cJSON_AddStringToObject(log, "severity", r->severity);
        cJSON_AddNumberToObject(log, "confidence", r->confidence);
        cJSON_AddStringToObject(log, "message", r->message);
        char *line = cJSON_PrintUnformatted(log);
        FILE *f = fopen("/spiffs/recommendations.jsonl", "a");
        if (f && line) {
            fprintf(f, "%s\n", line);
            fclose(f);
        }
        free(line);
        cJSON_Delete(log);
    }
}

static cJSON *read_array(const char *kind)
{
    char *text = history_store_read_json_array(kind);
    cJSON *array = cJSON_Parse(text ? text : "[]");
    free(text);
    return array ? array : cJSON_CreateArray();
}

static void evaluate(void)
{
    time_t now = time(NULL);
    cJSON *obs = read_array("observations");
    cJSON *water = read_array("watering");
    int dry[PLANTER_COUNT] = {0};
    int wet[PLANTER_COUNT] = {0};
    time_t last_obs = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, obs) {
        const cJSON *planter = cJSON_GetObjectItem(item, "planter_id");
        const cJSON *condition = cJSON_GetObjectItem(item, "condition");
        const cJSON *ts = cJSON_GetObjectItem(item, "timestamp");
        if (cJSON_IsNumber(ts) && (time_t)ts->valuedouble > last_obs) last_obs = (time_t)ts->valuedouble;
        if (!cJSON_IsNumber(planter)) continue;
        int planter_id = planter->valueint;
        if (planter_id < 1 || planter_id > PLANTER_COUNT || !cJSON_IsString(condition)) continue;
        if (strcmp(condition->valuestring, "too_dry") == 0 || strcmp(condition->valuestring, "wilting") == 0) dry[planter_id - 1]++;
        if (strcmp(condition->valuestring, "too_wet") == 0 || strcmp(condition->valuestring, "yellowing") == 0) wet[planter_id - 1]++;
    }
    for (int i = 0; i < PLANTER_COUNT; i++) {
        char id[40];
        char msg[192];
        if (dry[i] >= 3) {
            snprintf(id, sizeof(id), "planter-%d-dry", i + 1);
            snprintf(msg, sizeof(msg), "Planter %d has been marked dry several times. Open its dripper slightly or increase the shared schedule if several planters are dry.", i + 1);
            upsert_rec(id, "planter", i + 1, "dripper_adjustment", "medium", 0.74f, msg);
        }
        if (wet[i] >= 3) {
            snprintf(id, sizeof(id), "planter-%d-wet", i + 1);
            snprintf(msg, sizeof(msg), "Planter %d has been marked too wet several times. Close its dripper slightly and check drainage.", i + 1);
            upsert_rec(id, "planter", i + 1, "dripper_adjustment", "medium", 0.72f, msg);
        }
    }
    if (last_obs == 0 || now - last_obs > 7 * 24 * 3600) {
        upsert_rec("inspect-plants", "system", 0, "inspection_reminder", "low", 0.9f,
                   "No observations have been entered for 7 days. Inspect each planter before changing the schedule.");
    }
    int manual_after_schedule = 0;
    cJSON *a = NULL;
    cJSON_ArrayForEach(a, water) {
        const cJSON *type_a = cJSON_GetObjectItem(a, "type");
        const cJSON *ts_a = cJSON_GetObjectItem(a, "timestamp");
        if (!cJSON_IsString(type_a) || strcmp(type_a->valuestring, "scheduled") != 0 || !cJSON_IsNumber(ts_a)) continue;
        cJSON *b = NULL;
        cJSON_ArrayForEach(b, water) {
            const cJSON *type_b = cJSON_GetObjectItem(b, "type");
            const cJSON *ts_b = cJSON_GetObjectItem(b, "timestamp");
            if (cJSON_IsString(type_b) && strcmp(type_b->valuestring, "manual") == 0 && cJSON_IsNumber(ts_b)) {
                double delta = ts_b->valuedouble - ts_a->valuedouble;
                if (delta > 0 && delta <= 12 * 3600) manual_after_schedule++;
            }
        }
    }
    if (manual_after_schedule >= 3) {
        upsert_rec("schedule-increase", "system", 0, "schedule_adjustment", "medium", 0.78f,
                   "Manual watering is often started within 12 hours after a scheduled run. Consider increasing scheduled duration or watering frequency.");
    }
    cJSON_Delete(obs);
    cJSON_Delete(water);
}

static void recommendation_task(void *arg)
{
    while (true) {
        evaluate();
        vTaskDelay(pdMS_TO_TICKS(300000));
    }
}

esp_err_t recommendation_engine_start(void)
{
    evaluate();
    xTaskCreate(recommendation_task, "Recommendation Task", 6144, NULL, 4, NULL);
    return ESP_OK;
}

char *recommendation_engine_get_json(void)
{
    cJSON *array = cJSON_CreateArray();
    time_t now = time(NULL);
    for (size_t i = 0; i < g_rec_count; i++) {
        recommendation_record_t *r = &g_recs[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", r->id);
        cJSON_AddNumberToObject(o, "timestamp", (double)r->timestamp);
        cJSON_AddStringToObject(o, "scope", r->scope);
        if (r->planter_id > 0) cJSON_AddNumberToObject(o, "planter_id", r->planter_id);
        else cJSON_AddNullToObject(o, "planter_id");
        cJSON_AddStringToObject(o, "category", r->category);
        cJSON_AddStringToObject(o, "severity", r->severity);
        cJSON_AddNumberToObject(o, "confidence", r->confidence);
        cJSON_AddStringToObject(o, "message", r->message);
        cJSON_AddBoolToObject(o, "resolved", r->resolved);
        cJSON_AddBoolToObject(o, "snoozed", r->snoozed_until > now);
        cJSON_AddStringToObject(o, "why", "Based on schedule history, pump events, planter profiles, and user observations. This system does not measure soil moisture.");
        cJSON_AddItemToArray(array, o);
    }
    char *out = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    return out;
}

esp_err_t recommendation_engine_resolve(const char *id)
{
    for (size_t i = 0; i < g_rec_count; i++) if (strcmp(g_recs[i].id, id) == 0) g_recs[i].resolved = true;
    return ESP_OK;
}

esp_err_t recommendation_engine_snooze(const char *id, int hours)
{
    for (size_t i = 0; i < g_rec_count; i++) if (strcmp(g_recs[i].id, id) == 0) g_recs[i].snoozed_until = time(NULL) + hours * 3600;
    return ESP_OK;
}
